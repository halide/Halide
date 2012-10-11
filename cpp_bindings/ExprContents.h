#ifndef HALIDE_EXPR_CONTENTS_H
#define HALIDE_EXPR_CONTENTS_H

#include "Expr.h"
#include "Func.h"
#include "Reduction.h"

namespace Halide {
    MLVal makeVar(const MLVal &name);
    MLVal makeFuncCall(const MLVal &, const MLVal &, const MLVal &);

    struct ExprContents {
        ExprContents(MLVal n, Type t) : node(n), type(t), isVar(false), isRVar(false), isImmediate(false), implicitArgs(0) {}
        ExprContents(const FuncRef &f);

        // Declare that this expression is the child of another for bookkeeping
        void child(Expr );

        // The ML-value of the expression
        MLVal node;
        
        // The (dynamic) type of the expression
        Type type;
        
        // The list of argument buffers contained within subexpressions            
        std::vector<DynImage> images;
        
        // The list of free variables found
        std::vector<Var> vars;

        // A reduction domain that this depends on
        RDom rdom;
        
        // The list of functions directly called        
        std::vector<Func> funcs;
        
        // The list of uniforms referred to
        std::vector<DynUniform> uniforms;

        // The list of uniform images referred to
        std::vector<UniformImage> uniformImages;
        
        // Sometimes it's useful to be able to tell if an expression is a simple var or not, or if it's an immediate.
        bool isVar, isRVar, isImmediate;
        
        // The number of arguments that remain implicit
        int implicitArgs;
    };
}

#endif