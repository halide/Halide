#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

class FlattenDimensions : public IRMutator {
    Expr flatten_args(const string &name, const vector<Expr> &args) {
        Expr idx = 0;
        for (size_t i = 0; i < args.size(); i++) {
            ostringstream stride_name, min_name;
            stride_name << name << ".stride." << i;
            min_name << name << ".min." << i;
            Expr stride = new Variable(Int(32), stride_name.str());
            Expr min = new Variable(Int(32), min_name.str());
            idx += (args[i] - min) * stride;
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


        size = mutate(size);

        stmt = new Allocate(realize->name, realize->type, size, body);

        // Compute the strides 
        for (int i = (int)realize->bounds.size()-1; i > 0; i--) {
            ostringstream stride_name;
            stride_name << realize->name << ".stride." << i;
            ostringstream prev_stride_name;
            prev_stride_name << realize->name << ".stride." << (i-1);
            ostringstream prev_extent_name;
            prev_extent_name << realize->name << ".extent." << (i-1);
            Expr prev_stride = new Variable(Int(32), prev_stride_name.str());
            Expr prev_extent = new Variable(Int(32), prev_extent_name.str());
            stmt = new LetStmt(stride_name.str(), prev_stride * prev_extent, stmt);
        }
        // Innermost stride is one
        stmt = new LetStmt(realize->name + ".stride.0", 1, stmt);           

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

Stmt storage_flattening(Stmt s) {
    return FlattenDimensions().mutate(s);
}

}
}
