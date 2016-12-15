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
using std::vector;
using std::string;

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
    string prefix;

    using IRVisitor::visit;

    void visit(const For *op) {
        if (starts_with(op->name, prefix)) {
            if (op->for_type == ForType::GPUBlock) {
                nblocks++;
            } else if (op->for_type == ForType::GPUThread) {
                nthreads++;
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const IfThenElse *op) {
        op->condition.accept(this);

        int old_nblocks = nblocks;
        int old_nthreads = nthreads;
        op->then_case.accept(this);

        if (op->else_case.defined()) {
            int then_nblocks = nblocks;
            int then_nthreads = nthreads;
            nblocks = old_nblocks;
            nthreads = old_nthreads;
            op->else_case.accept(this);
            nblocks = std::max(then_nblocks, nblocks);
            nthreads = std::max(then_nthreads, nthreads);
        }
    }

public:
    CountGPUBlocksThreads(const string &p) : prefix(p), nblocks(0), nthreads(0) {}
    int nblocks;
    int nthreads;
};

class FindGPUVarsReplacement : public IRVisitor {
    using IRVisitor::visit;

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

    void visit(const For *op) {
        if ((op->for_type == ForType::GPUBlock) ||
            (op->for_type == ForType::GPUThread)) {

            vector<string> v = split_string(op->name, ".");
            internal_assert(v.size() > 2);

            CountGPUBlocksThreads counter(v[0] + "." + v[1]);
            op->body.accept(&counter);
            internal_assert(counter.nblocks <= 4)
                << op->name << " can only have maximum of 4 block dimensions\n";
            internal_assert(counter.nthreads <= 4)
                << op->name << " can only have maximum of 4 thread dimensions\n";

            string name;
            if (op->for_type == ForType::GPUBlock) {
                name = gpu_name(v, get_block_name(counter.nblocks));
                debug(5) << "Replacing " << op->name << " with GPU block name " << name << "\n";
            } else if (op->for_type == ForType::GPUThread) {
                name = gpu_name(v, get_thread_name(counter.nthreads));
                debug(5) << "Replacing " << op->name << " with GPU thread name " << name << "\n";
            }

            gpu_vars.emplace(op->name, name);
            replacements.emplace(op->name, Variable::make(Int(32), name));
        }

        op->body.accept(this);
    }

public:
    map<string, string> gpu_vars;
    map<string, Expr> replacements;
};

class CanonicalizeGPUVars : public IRMutator {
    const map<string, string> &gpu_vars;

    using IRMutator::visit;

    string find_replacement(const string &suffix, const string &name) {
        vector<string> v = split_string(name, suffix);
        internal_assert(v.size() == 2);
        const auto &iter = gpu_vars.find(v[0]);
        if (iter != gpu_vars.end()) {
            string new_name = iter->second + suffix;
            replacements.emplace(name, Variable::make(Int(32), new_name));
            return new_name;
        }
        return name;
    }

    string canonicalize_name(const string &name) {
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

    void visit(const For *op) {
        string name = op->name;
        const auto &iter = gpu_vars.find(op->name);
        if (iter != gpu_vars.end()) {
            name = iter->second;
        }

        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        Stmt body = mutate(op->body);
        if ((name == op->name) &&
            min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(name, min, extent, op->for_type, op->device_api, body);
        }
    }

    void visit(const LetStmt *op) {
        string name = canonicalize_name(op->name);
        Expr value = mutate(op->value);
        Stmt body = mutate(op->body);
        if ((name == op->name) &&
            value.same_as(op->value) &&
            body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(name, value, body);
        }
    }

public:
    CanonicalizeGPUVars(const map<string, string> &g, const map<string, Expr> &r)
        : gpu_vars(g), replacements(r) {}

    map<string, Expr> replacements;
};

} // anonymous namespace

Stmt canonicalize_gpu_vars(Stmt s) {
    FindGPUVarsReplacement finder;
    s.accept(&finder);

    CanonicalizeGPUVars canonicalizer(finder.gpu_vars, finder.replacements);
    s = canonicalizer.mutate(s);

    // Now we need to substitute in the new vars
    s = substitute(canonicalizer.replacements, s);

    return s;
}

}
}
