#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::ostringstream;

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

    void visit(const Realize *realize) {
        Stmt body = mutate(realize->body);

        // Compute the size
        Expr size = 1;
        for (size_t i = 0; i < realize->bounds.size(); i++) {
            size *= realize->bounds[i].second;
        }


        size = mutate(size);

        stmt = new Allocate(realize->buffer, realize->type, size, body);

        // Compute the strides 
        for (int i = (int)realize->bounds.size()-1; i > 0; i--) {
            ostringstream stride_name;
            stride_name << realize->buffer << ".stride." << i;
            ostringstream prev_stride_name;
            prev_stride_name << realize->buffer << ".stride." << (i-1);
            ostringstream prev_extent_name;
            prev_extent_name << realize->buffer << ".extent." << (i-1);
            Expr prev_stride = new Variable(Int(32), prev_stride_name.str());
            Expr prev_extent = new Variable(Int(32), prev_extent_name.str());
            stmt = new LetStmt(stride_name.str(), prev_stride * prev_extent, stmt);
        }
        // Innermost stride is one
        stmt = new LetStmt(realize->buffer + ".stride.0", 1, stmt);           

        // Assign the mins and extents stored
        for (int i = realize->bounds.size(); i > 0; i--) { 
            ostringstream min_name, extent_name;
            min_name << realize->buffer << ".min." << (i-1);
            extent_name << realize->buffer << ".extent." << (i-1);
            stmt = new LetStmt(min_name.str(), realize->bounds[i-1].first, stmt);
            stmt = new LetStmt(extent_name.str(), realize->bounds[i-1].second, stmt);
        }
    }

    void visit(const Provide *provide) {
        Expr idx = mutate(flatten_args(provide->buffer, provide->args));
        Expr val = mutate(provide->value);
        stmt = new Store(provide->buffer, val, idx); 
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

Stmt do_storage_flattening(Stmt s) {
    return FlattenDimensions().mutate(s);
}

}
}
