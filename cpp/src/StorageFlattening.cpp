#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
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
private:
    const map<string, Function> &env;

    Expr flatten_args(const string &name, const vector<Expr> &args) {
        Expr idx = 0;
        // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
        // strategy makes sense when we expect x to cancel with
        // something in xmin.  We use this for internal allocations
        if (env.find(name) != env.end()) {
            for (size_t i = 0; i < args.size(); i++) {
                ostringstream stride_name, min_name;
                stride_name << name << ".stride." << i;
                min_name << name << ".min." << i;
                Expr stride = new Variable(Int(32), stride_name.str());
                Expr min = new Variable(Int(32), min_name.str());
                idx += (args[i] - min) * stride;
            }
        } else {
            // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
            // ystride*ymin)]. The idea here is that the last term
            // will be pulled outside the inner loop. We use this for
            // external buffers, where the mins and strides are likely
            // to be symbolic
            Expr base = 0;
            for (size_t i = 0; i < args.size(); i++) {
                ostringstream stride_name, min_name;
                stride_name << name << ".stride." << i;
                min_name << name << ".min." << i;
                Expr stride = new Variable(Int(32), stride_name.str());
                Expr min = new Variable(Int(32), min_name.str());
                idx += args[i] * stride;            
                base += min * stride;
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

        stmt = new Allocate(realize->name, realize->type, size, body);

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
            Expr prev_stride = new Variable(Int(32), prev_stride_name.str());
            Expr prev_extent = new Variable(Int(32), prev_extent_name.str());
            stmt = new LetStmt(stride_name.str(), prev_stride * prev_extent, stmt);
        }
        // Innermost stride is one
        ostringstream stride_0_name;
        if (!storage_permutation.empty()) {
            stride_0_name << realize->name << ".stride." << storage_permutation[0];
        } else {
            stride_0_name << realize->name << ".stride.0";
        }
        stmt = new LetStmt(stride_0_name.str(), 1, stmt);           

        // Assign the mins and extents stored
        for (int i = realize->bounds.size(); i > 0; i--) { 
            ostringstream min_name, extent_name;
            min_name << realize->name << ".min." << (i-1);
            extent_name << realize->name << ".extent." << (i-1);
            stmt = new LetStmt(min_name.str(), realize->bounds[i-1].min, stmt);
            stmt = new LetStmt(extent_name.str(), realize->bounds[i-1].extent, stmt);
        }
    }

    void visit(const Provide *provide) {
        Expr idx = mutate(flatten_args(provide->name, provide->args));
        Expr val = mutate(provide->value);
        stmt = new Store(provide->name, val, idx); 
    }

    void visit(const Call *call) {            
        if (call->call_type == Call::Extern) {
            vector<Expr> args(call->args.size());
            bool changed = false;
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(call->args[i]);
                if (!args[i].same_as(call->args[i])) changed = true;
            }
            if (!changed) {
                expr = call;
            } else {
                expr = new Call(call->type, call->name, args);
            }
        } else {
            Expr idx = mutate(flatten_args(call->name, call->args));
            expr = new Load(call->type, call->name, idx, call->image, call->param);
        }
    }

};

Stmt storage_flattening(Stmt s, const map<string, Function> &env) {
    return FlattenDimensions(env).mutate(s);
}

}
}
