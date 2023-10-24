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
string thread_names[] = {"__thread_id_x", "__thread_id_y", "__thread_id_z", "__thread_id_w"};
string block_names[] = {"__block_id_x", "__block_id_y", "__block_id_z", "__block_id_w"};

string get_thread_name(int index) {
    internal_assert(index >= 0 && index < 4);
    return thread_names[index];
}

string get_block_name(int index) {
    internal_assert(index >= 0 && index < 4);
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
            internal_assert(counter.nblocks <= 4)
                << op->name << " can only have maximum of 4 block dimensions\n";
            internal_assert(counter.nthreads <= 4)
                << op->name << " can only have maximum of 4 thread dimensions\n";

            if (op->for_type == ForType::GPUBlock) {
                name += "." + get_block_name(counter.nblocks);
                debug(5) << "Replacing " << op->name << " with GPU block name " << name << "\n";
            } else if (op->for_type == ForType::GPUThread) {
                name += "." + get_thread_name(counter.nthreads);
                debug(5) << "Replacing " << op->name << " with GPU thread name " << name << "\n";
            } else if (op->for_type == ForType::GPULane) {
                user_assert(counter.nlanes == 0) << "Cannot nest multiple loops over gpu lanes: " << name << "\n";
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

}  // anonymous namespace

Stmt canonicalize_gpu_vars(Stmt s) {
    CanonicalizeGPUVars canonicalizer;
    s = canonicalizer.mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
