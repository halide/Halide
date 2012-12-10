#ifndef BOUNDS_H
#define BOUNDS_H

#include "IRVisitor.h"
#include "Scope.h"
#include <utility>

namespace Halide {
    namespace Internal {
    
        using std::pair;
        
        /* Given an expression in some variables, and a map from those
         * variables to their bounds (in the form of (minimum possible
         * value, maximum possible value)), compute two expressions that
         * give the minimum possible value and the maximum possible value
         * of this expression. Ir, if the expression is unbounded, return
         * false. 
         *
         * This is for tasks such as deducing the region of a buffer
         * loaded by a chunk of code.
         */
        bool bounds_of_expr_in_scope(Expr expr, const Scope<pair<Expr, Expr> > &scope, Expr *min, Expr *max);    
        
        // TODO: Other useful things in src/bounds.ml, such as region of a func required by a stmt
        
        //class Bounds : public IRVisitor {
        // TODO
        //};
    }
}

#endif
