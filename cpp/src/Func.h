#ifndef FUNC_H
#define FUNC_H

#include "IR.h"
#include "Var.h"
#include "IntrusivePtr.h"
#include "Function.h"

namespace Halide {
        
/* A fragment of front-end syntax of the form f(x, y, z), where x,
 * y, z are Vars. It could be the left-hand side of a function
 * definition, or it could be a call to a function. We don't know
 * yet.
 */
class FuncRefVar {
    Internal::Function func;
    vector<string> args;
public:
    FuncRefVar(Internal::Function, const vector<Var> &);
        
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
    Internal::Function func;
    vector<Expr> args;
public:
    FuncRefExpr(Internal::Function, const vector<Expr> &);
        
    // Use this as the left-hand-side of a reduction definition
    void operator=(Expr);
        
    // Use this as a call to the function
    operator Expr();
};

/* A halide function. Define it, call it, schedule it. */
class Func {
    Internal::Function func;
    void set_dim_type(Var var, For::ForType t);

    // Save this state when we jit, so that we can run the function again cheaply
    void (*function_ptr)(const void **);
    vector<const void *> arg_values;

public:        
    static void test();

    Func(Internal::Function f);
    Func(const string &name);
    Func();

    Buffer realize(int x_size, int y_size = 1, int z_size = 1, int w_size = 1);
    void realize(Buffer dst);

    const string &name() const;
    Expr value() const;

    FuncRefVar operator()(Var x);
    FuncRefVar operator()(Var x, Var y);
    FuncRefVar operator()(Var x, Var y, Var z);
    FuncRefVar operator()(Var x, Var y, Var z, Var w);
    FuncRefExpr operator()(Expr x);
    FuncRefExpr operator()(Expr x, Expr y);
    FuncRefExpr operator()(Expr x, Expr y, Expr z);
    FuncRefExpr operator()(Expr x, Expr y, Expr z, Expr w);
        
    Func &split(Var old, Var outer, Var inner, Expr factor);
    Func &parallel(Var var);
    Func &vectorize(Var var);
    Func &unroll(Var var);
    Func &vectorize(Var var, int factor);
    Func &unroll(Var var, int factor);
    Func &compute_at(Func f, Var var);
    Func &compute_root();
    Func &store_at(Func f, Var var);
    Func &store_root();
    Func &compute_inline();
    Func &bound(Var var, Expr min, Expr extent);
    Func &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);
    Func &reorder(Var x, Var y);
    Func &reorder(Var x, Var y, Var z);
    Func &reorder(Var x, Var y, Var z, Var w);
    Func &reorder(Var x, Var y, Var z, Var w, Var t);

    Stmt lower();
};
}

#endif
