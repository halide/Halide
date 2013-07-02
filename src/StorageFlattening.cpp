#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;

class FlattenDimensions : public IRMutator {
public:
    FlattenDimensions(const map<string, Function> &e) : env(e) {}
    Scope<int> scope;
private:
    const map<string, Function> &env;

    Expr flatten_args(const string &name, const vector<Expr> &args) {
        Expr idx = 0;
        vector<Expr> mins(args.size()), strides(args.size());

        for (size_t i = 0; i < args.size(); i++) {
            char dim = '0' + i;
            string stride_name = name + ".stride." + dim;
            string min_name = name + ".min." + dim;
            string stride_name_constrained = stride_name + ".constrained";
            string min_name_constrained = min_name + ".constrained";
            if (scope.contains(stride_name_constrained)) {
                stride_name = stride_name_constrained;
            }
            if (scope.contains(min_name_constrained)) {
                min_name = min_name_constrained;
            }
            strides[i] = Variable::make(Int(32), stride_name);
            mins[i] = Variable::make(Int(32), min_name);
        }

        if (env.find(name) != env.end()) {
            // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
            // strategy makes sense when we expect x to cancel with
            // something in xmin.  We use this for internal allocations
            for (size_t i = 0; i < args.size(); i++) {
                idx += (args[i] - mins[i]) * strides[i];
            }
        } else {
            // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
            // ystride*ymin)]. The idea here is that the last term
            // will be pulled outside the inner loop. We use this for
            // external buffers, where the mins and strides are likely
            // to be symbolic
            Expr base = 0;
            for (size_t i = 0; i < args.size(); i++) {
                idx += args[i] * strides[i];
                base += mins[i] * strides[i];
            }
            idx -= base;
        }

        return idx;
    }

    using IRMutator::visit;

    void visit(const Realize *realize) {
        Stmt body = mutate(realize->body);

        // Compute the size
        Expr size = 1;
        for (size_t i = 0; i < realize->bounds.size(); i++) {
            size *= realize->bounds[i].extent;
        }

        vector<int> storage_permutation;
        {
            map<string, Function>::const_iterator iter = env.find(realize->name);
            assert(iter != env.end() && "Realize node refers to function not in environment");
            const vector<string> &storage_dims = iter->second.schedule().storage_dims;
            const vector<string> &args = iter->second.args();
            for (size_t i = 0; i < storage_dims.size(); i++) {
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j] == storage_dims[i]) {
                        storage_permutation.push_back((int)j);
                    }
                }
                assert(storage_permutation.size() == i+1);
            }
        }

        assert(storage_permutation.size() == realize->bounds.size());

        size = mutate(size);

        stmt = Allocate::make(realize->name, realize->type, size, body);

        // Compute the strides
        for (int i = (int)realize->bounds.size()-1; i > 0; i--) {
            int prev_j = storage_permutation[i-1];
            int j = storage_permutation[i];
            ostringstream stride_name;
            stride_name << realize->name << ".stride." << j;
            ostringstream prev_stride_name;
            prev_stride_name << realize->name << ".stride." << prev_j;
            ostringstream prev_extent_name;
            prev_extent_name << realize->name << ".extent." << prev_j;
            Expr prev_stride = Variable::make(Int(32), prev_stride_name.str());
            Expr prev_extent = Variable::make(Int(32), prev_extent_name.str());
            stmt = LetStmt::make(stride_name.str(), prev_stride * prev_extent, stmt);
        }
        // Innermost stride is one
        ostringstream stride_0_name;
        if (!storage_permutation.empty()) {
            stride_0_name << realize->name << ".stride." << storage_permutation[0];
        } else {
            stride_0_name << realize->name << ".stride.0";
        }
        stmt = LetStmt::make(stride_0_name.str(), 1, stmt);

        // Assign the mins and extents stored
        for (size_t i = realize->bounds.size(); i > 0; i--) {
            ostringstream min_name, extent_name;
            min_name << realize->name << ".min." << (i-1);
            extent_name << realize->name << ".extent." << (i-1);
            stmt = LetStmt::make(min_name.str(), realize->bounds[i-1].min, stmt);
            stmt = LetStmt::make(extent_name.str(), realize->bounds[i-1].extent, stmt);
        }
    }

    void visit(const Provide *provide) {
        Expr idx = mutate(flatten_args(provide->name, provide->args));
        Expr val = mutate(provide->value);
        stmt = Store::make(provide->name, val, idx);
    }

    void visit(const Call *call) {
        if (call->call_type == Call::Extern || call->call_type == Call::Intrinsic) {
            vector<Expr> args(call->args.size());
            bool changed = false;
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(call->args[i]);
                if (!args[i].same_as(call->args[i])) changed = true;
            }
            if (!changed) {
                expr = call;
            } else {
                expr = Call::make(call->type, call->name, args, call->call_type);
            }
        } else {
            Expr idx = mutate(flatten_args(call->name, call->args));
            expr = Load::make(call->type, call->name, idx, call->image, call->param);
        }
    }

    void visit(const LetStmt *let) {
        // Discover constrained versions of things.
        bool constrained_version_exists = ends_with(let->name, ".constrained");
        if (constrained_version_exists) {
            scope.push(let->name, 0);
        }

        IRMutator::visit(let);

        if (constrained_version_exists) {
            scope.pop(let->name);
        }
    }
};

Stmt storage_flattening(Stmt s, const map<string, Function> &env) {
    return FlattenDimensions(env).mutate(s);
}

}
}
