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
    string prefix; // Producer name + stage

    using IRVisitor::visit;

    void visit(const For *op) override {
        if (starts_with(op->name, prefix)) {
            if (op->for_type == ForType::GPUBlock) {
                nblocks++;
            } else if (op->for_type == ForType::GPUThread) {
                nthreads++;
            } else if (op->for_type == ForType::GPULane) {
                nlanes++;
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const IfThenElse *op) override {
        op->condition.accept(this);

        int old_nblocks = nblocks;
        int old_nthreads = nthreads;
        int old_nlanes = nlanes;
        op->then_case.accept(this);

        if (op->else_case.defined()) {
            int then_nblocks = nblocks;
            int then_nthreads = nthreads;
            int then_nlanes = nlanes;
            nblocks = old_nblocks;
            nthreads = old_nthreads;
            nlanes = old_nlanes;
            op->else_case.accept(this);
            nblocks = std::max(then_nblocks, nblocks);
            nthreads = std::max(then_nthreads, nthreads);
            nlanes = std::max(then_nlanes, nlanes);
        }
    }

public:
    CountGPUBlocksThreads(const string &p) : prefix(p) {}
    int nblocks = 0;
    int nthreads = 0;
    int nlanes = 0;
};

class CanonicalizeGPUVars : public IRMutator {
    map<string, string> gpu_vars;

    using IRMutator::visit;

    string gpu_name(vector<string> v, const string &new_var) {
        v.push_back(new_var);

        std::ostringstream stream;
        for (size_t i = 0; i < v.size(); ++i) {
            stream << v[i];
            if (i != v.size() - 1) {
                stream << ".";
            }
        }
        return stream.str();
    }

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

            vector<string> v = split_string(op->name, ".");
            internal_assert(v.size() > 2);

            CountGPUBlocksThreads counter(v[0] + "." + v[1]);
            op->body.accept(&counter);
            internal_assert(counter.nblocks <= 4)
                << op->name << " can only have maximum of 4 block dimensions\n";
            internal_assert(counter.nthreads <= 4)
                << op->name << " can only have maximum of 4 thread dimensions\n";

            if (op->for_type == ForType::GPUBlock) {
                name = gpu_name(v, get_block_name(counter.nblocks));
                debug(5) << "Replacing " << op->name << " with GPU block name " << name << "\n";
            } else if (op->for_type == ForType::GPUThread) {
                name = gpu_name(v, get_thread_name(counter.nlanes + counter.nthreads));
                debug(5) << "Replacing " << op->name << " with GPU thread name " << name << "\n";
            } else if (op->for_type == ForType::GPULane) {
                user_assert(counter.nlanes == 0) << "Cannot nest multiple loops over gpu lanes: " << name << "\n";
                name = gpu_name(v, get_thread_name(0));
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
            return For::make(name, min, extent, op->for_type, op->device_api, body);
        }
    }


    Stmt visit(const LetStmt *op) override {
        vector<std::pair<string, Expr>> lets;
        Stmt result;

        do {
            lets.emplace_back(op->name, mutate(op->value));
            result = op->body;
        } while((op = op->body.as<LetStmt>()));

        result = mutate(result);

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            string name = canonicalize_let(it->first);
            if (name != it->first) {
                Expr new_var = Variable::make(Int(32), name);
                result = substitute(it->first, name, result);
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
