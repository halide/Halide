#include <cmath>
#include <sstream>

#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "IR.h"
#include "IRMutator.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {
string thread_names[] = {"__thread_id_x", "__thread_id_y", "__thread_id_z"};
string block_names[] = {"__block_id_x", "__block_id_y", "__block_id_z"};

string get_thread_name(int index) {
    internal_assert(index >= 0 && index < 3);
    return thread_names[index];
}

string get_block_name(int index) {
    internal_assert(index >= 0 && index < 3);
    return block_names[index];
}

class CountGPUBlocksThreads : public IRVisitor {
    using IRVisitor::visit;

    // Counters that track the number of blocks, threads, and lanes loops that
    // we're inside of, respectively. Lanes loops also count as threads loops.
    int nb = 0, nt = 0, nl = 0;

    void visit(const For *op) override {
        // Figure out how much to increment each counter by based on the loop
        // type.
        int db = op->for_type == ForType::GPUBlock;
        int dl = op->for_type == ForType::GPULane;
        int dt = op->for_type == ForType::GPUThread;

        // The threads counter includes lanes loops
        dt += dl;

        // Increment counters
        nb += db;
        nl += dl;
        nt += dt;

        // Update the maximum counter values seen.
        nblocks = std::max(nb, nblocks);
        nthreads = std::max(nt, nthreads);
        nlanes = std::max(nl, nlanes);

        // Visit the body
        IRVisitor::visit(op);

        // Decrement counters
        nb -= db;
        nl -= dl;
        nt -= dt;
    }

public:
    // The maximum values hit by the counters above, which tells us the nesting
    // depth of each type of loop within a Stmt.
    int nblocks = 0;
    int nthreads = 0;
    int nlanes = 0;
};

class CanonicalizeGPUVars : public IRMutator {
    map<string, string> gpu_vars;

    using IRMutator::visit;

    string find_replacement(const string &suffix, const string &name) {
        vector<string> v = split_string(name, suffix);
        internal_assert(v.size() == 2);
        const auto &iter = gpu_vars.find(v[0]);
        if (iter != gpu_vars.end()) {
            return iter->second + suffix;
        }
        return name;
    }

    string canonicalize_let(const string &name) {
        if (ends_with(name, ".loop_max")) {
            return find_replacement(".loop_max", name);
        } else if (ends_with(name, ".loop_min")) {
            return find_replacement(".loop_min", name);
        } else if (ends_with(name, ".loop_extent")) {
            return find_replacement(".loop_extent", name);
        } else {
            return name;
        }
    }

    Stmt visit(const For *op) override {
        string name = op->name;
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        Stmt body = mutate(op->body);

        if ((op->for_type == ForType::GPUBlock) ||
            (op->for_type == ForType::GPUThread) ||
            (op->for_type == ForType::GPULane)) {

            CountGPUBlocksThreads counter;
            op->body.accept(&counter);

            if (op->for_type == ForType::GPUBlock) {
                name += "." + get_block_name(counter.nblocks);
                debug(5) << "Replacing " << op->name << " with GPU block name " << name << "\n";
            } else if (op->for_type == ForType::GPUThread) {
                name += "." + get_thread_name(counter.nthreads);
                debug(5) << "Replacing " << op->name << " with GPU thread name " << name << "\n";
            } else if (op->for_type == ForType::GPULane) {
                name += "." + get_thread_name(0);
            }

            if (name != op->name) {
                // Canonicalize the GPU for loop name
                gpu_vars.emplace(op->name, name);
                Expr new_var = Variable::make(Int(32), name);
                min = substitute(op->name, new_var, min);
                extent = substitute(op->name, new_var, extent);
                body = substitute(op->name, new_var, body);
            }
        }

        if ((name == op->name) &&
            min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body)) {
            return op;
        } else {
            return For::make(name, min, extent, op->for_type, op->partition_policy, op->device_api, body);
        }
    }

    Stmt visit(const LetStmt *op) override {
        vector<std::pair<string, Expr>> lets;
        Stmt result;

        do {
            lets.emplace_back(op->name, mutate(op->value));
            result = op->body;
        } while ((op = op->body.as<LetStmt>()));

        result = mutate(result);

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            string name = canonicalize_let(it->first);
            if (name != it->first) {
                Expr new_var = Variable::make(Int(32), name);
                result = substitute(it->first, new_var, result);
            }
            result = LetStmt::make(name, it->second, result);
        }

        return result;
    }

    Stmt visit(const IfThenElse *op) override {
        Expr condition = mutate(op->condition);

        map<string, string> old_gpu_vars;
        old_gpu_vars.swap(gpu_vars);
        Stmt then_case = mutate(op->then_case);

        gpu_vars = old_gpu_vars;
        Stmt else_case = mutate(op->else_case);

        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(condition, then_case, else_case);
        }
    }
};

std::string loop_nest_summary_to_node(const IRNode *root, const IRNode *target) {
    class Summary : public IRVisitor {
    public:
        std::vector<std::ostringstream> stack;
        Summary(const IRNode *target)
            : target(target) {
        }

    protected:
        const IRNode *target;
        bool done = false;

        using IRVisitor::visit;

        void visit(const For *op) override {
            if (done) {
                return;
            }
            stack.emplace_back();
            stack.back() << op->for_type << " " << op->name;
            if (op == target) {
                done = true;
            } else {
                IRVisitor::visit(op);
                if (!done) {
                    stack.pop_back();
                }
            }
        }

        void visit(const Realize *op) override {
            if (done) {
                return;
            }
            stack.emplace_back();
            stack.back() << "store_at for " << op->name;
            IRVisitor::visit(op);
            if (!done) {
                stack.pop_back();
            }
        }

        void visit(const HoistedStorage *op) override {
            if (done) {
                return;
            }
            stack.emplace_back();
            stack.back() << "hoisted storage for " << op->name;
            IRVisitor::visit(op);
            if (!done) {
                stack.pop_back();
            }
        }

        void visit(const ProducerConsumer *op) override {
            if (done) {
                return;
            }
            if (op->is_producer) {
                stack.emplace_back();
                stack.back() << "compute_at for " << op->name;
                IRVisitor::visit(op);
                if (!done) {
                    stack.pop_back();
                }
            } else {
                IRVisitor::visit(op);
            }
        }
    } summary{target};

    root->accept(&summary);

    std::ostringstream result;
    std::string prefix = "";
    result << "The loop nest is:\n";
    for (const auto &str : summary.stack) {
        result << prefix << str.str() << ":\n";
        prefix += " ";
    }
    return result.str();
};

// Check the user's GPU schedule is valid. Throws an error if it is not, so no
// return value required.
class ValidateGPUSchedule : public IRVisitor {

    using IRVisitor::visit;

    const IRNode *root = nullptr;

    int in_blocks = 0;
    int in_threads = 0;
    int in_lanes = 0;

    std::string innermost_blocks_loop, innermost_threads_loop;
    std::ostringstream blocks_not_ok_reason;

    void clear_blocks_not_ok_reason() {
        std::ostringstream empty;
        blocks_not_ok_reason.swap(empty);
    }

    void visit(const For *op) override {
        if (!root) {
            root = op;
        }
        bool should_clear = false;
        if (in_blocks && op->for_type != ForType::GPUBlock && blocks_not_ok_reason.tellp() == 0) {
            blocks_not_ok_reason << op->for_type << " loop over " << op->name;
            should_clear = true;
        }
        if (op->for_type == ForType::GPUBlock) {
            user_assert(blocks_not_ok_reason.tellp() == 0)
                << blocks_not_ok_reason.str() << " is inside GPU block loop over "
                << innermost_blocks_loop << " but outside GPU block loop over " << op->name
                << ". Funcs cannot be scheduled in between GPU block loops. "
                << loop_nest_summary_to_node(root, op);
            user_assert(in_blocks < 3)
                << "GPU block loop over " << op->name << " is inside three other GPU block loops. "
                << "The maximum number of nested GPU block loops is 3. "
                << loop_nest_summary_to_node(root, op);
            user_assert(in_threads == 0)
                << "GPU block loop over " << op->name << " is inside GPU thread loop over "
                << innermost_threads_loop << ". "
                << loop_nest_summary_to_node(root, op);
            in_blocks++;
            ScopedValue<std::string> s(innermost_blocks_loop, op->name);
            IRVisitor::visit(op);
            in_blocks--;
        } else if (op->for_type == ForType::GPUThread) {
            user_assert(in_lanes == 0)
                << "GPU thread loop over " << op->name << " is inside a loop over GPU lanes. "
                << "GPU thread loops must be outside any GPU lane loop. "
                << loop_nest_summary_to_node(root, op);
            user_assert(in_threads < 3)
                << "GPU thread loop over " << op->name << " is inside three other GPU thread loops. "
                << "The maximum number of nested GPU thread loops is 3. "
                << loop_nest_summary_to_node(root, op);
            user_assert(in_blocks)
                << "GPU thread loop over " << op->name << " must be inside a GPU block loop. "
                << loop_nest_summary_to_node(root, op);
            in_threads++;
            ScopedValue<std::string> s(innermost_threads_loop, op->name);
            IRVisitor::visit(op);
            in_threads--;
        } else if (op->for_type == ForType::GPULane) {
            user_assert(in_threads < 3)
                << "GPU lane loop over " << op->name << " is inside three other GPU thread or lane loops. "
                << "The maximum number of nested GPU thread or lane loops is 3. "
                << loop_nest_summary_to_node(root, op);
            user_assert(in_lanes == 0)
                << "GPU lane loop over " << op->name << " is inside another GPU lane loop. GPU lane loops "
                << "may not be nested. "
                << loop_nest_summary_to_node(root, op);
            in_lanes++;
            ScopedValue<std::string> s(innermost_threads_loop, op->name);
            IRVisitor::visit(op);
            in_lanes--;
        } else {
            IRVisitor::visit(op);
        }
        if (should_clear) {
            clear_blocks_not_ok_reason();
        }
    }

    void visit(const Realize *op) override {
        if (!root) {
            root = op;
        }
        if (in_blocks && blocks_not_ok_reason.tellp() == 0) {
            blocks_not_ok_reason << "store_at location for " << op->name;
            IRVisitor::visit(op);
            clear_blocks_not_ok_reason();
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const ProducerConsumer *op) override {
        if (!root) {
            root = op;
        }
        if (op->is_producer && in_blocks && blocks_not_ok_reason.tellp() == 0) {
            blocks_not_ok_reason << "compute_at location for " << op->name;
            IRVisitor::visit(op);
            clear_blocks_not_ok_reason();
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const HoistedStorage *op) override {
        if (!root) {
            root = op;
        }
        if (in_blocks && blocks_not_ok_reason.tellp() == 0) {
            blocks_not_ok_reason << "hoist_storage location for " << op->name;
            IRVisitor::visit(op);
            clear_blocks_not_ok_reason();
        } else {
            IRVisitor::visit(op);
        }
    }
};

}  // anonymous namespace

Stmt canonicalize_gpu_vars(Stmt s) {
    ValidateGPUSchedule validator;
    s.accept(&validator);
    CanonicalizeGPUVars canonicalizer;
    s = canonicalizer.mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
