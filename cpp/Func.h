#ifndef FUNC_H
#define FUNC_H

#include "IR.h"
#include "Var.h"
#include "IntrusivePtr.h"

namespace Halide {
        
    namespace Internal {
        class Function;
        class Schedule;
    }

    /* A fragment of front-end syntax of the form f(x, y, z), where x,
     * y, z are Vars. It could be the left-hand side of a function
     * definition, or it could be a call to a function. We don't know
     * yet.
     */
    class FuncRefVar {
        Internal::IntrusivePtr<Internal::Function> func;
        vector<Var> args;
    public:
        FuncRefVar(Internal::IntrusivePtr<Internal::Function>, const vector<Var> &);
        
        // Use this as the left-hand-side of a definition
        void operator=(Expr);
        
        // Use this as a call to the function
        operator Expr();
    };
    
    /* A fragment of front-end syntax of the form f(x, y, z), where x,
     * y, z are Exprs. If could be the left hand side of a reduction
     * definition, or it could be a call to a function. We don't know
     * yet.
     */
    class FuncRefExpr {
        Internal::IntrusivePtr<Internal::Function> func;
        vector<Expr> args;
    public:
        FuncRefExpr(Internal::IntrusivePtr<Internal::Function>, const vector<Expr> &);
        
        // Use this as the left-hand-side of a reduction definition
        void operator=(Expr);
        
        // Use this as a call to the function
        operator Expr();
    };

    /* A halide function. Define it, call it, schedule it. */
    class Func {
        Internal::IntrusivePtr<Internal::Function> func;
        void set_dim_type(Var var, For::ForType t);

    public:        
        static void test();

        Func(Internal::IntrusivePtr<Internal::Function> f);
        Func(const string &name);
        Func();

        Stmt lower(const map<string, Func> &env);
        
        const string &name() const;
        const vector<Var> &args() const;
        Expr value() const;
        const Internal::Schedule &schedule() const;

        FuncRefVar operator()(Var x);
        FuncRefVar operator()(Var x, Var y);
        FuncRefVar operator()(Var x, Var y, Var z);
        FuncRefVar operator()(Var x, Var y, Var z, Var w);
        FuncRefExpr operator()(Expr x);
        FuncRefExpr operator()(Expr x, Expr y);
        FuncRefExpr operator()(Expr x, Expr y, Expr z);
        FuncRefExpr operator()(Expr x, Expr y, Expr z, Expr w);
        
        void define(const vector<Var> &args, Expr value);
    
        Func &split(Var old, Var outer, Var inner, Expr factor);
        Func &parallel(Var var);
        Func &vectorize(Var var);
        Func &unroll(Var var);
        Func &compute_at(Func f, Var var);
        Func &compute_root();
        Func &store_at(Func f, Var var);
        Func &store_root();
        Func &compute_inline();
    };
}

#endif
