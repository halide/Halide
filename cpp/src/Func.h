#ifndef HALIDE_FUNC_H
#define HALIDE_FUNC_H

#include "IR.h"
#include "Var.h"
#include "IntrusivePtr.h"
#include "Function.h"
#include "Param.h"
#include "Argument.h"
#include "RDom.h"

namespace Halide {
        
/* A fragment of front-end syntax of the form f(x, y, z), where x,
 * y, z are Vars. It could be the left-hand side of a function
 * definition, or it could be a call to a function. We don't know
 * yet.
 */
class FuncRefVar {
    Internal::Function func;
    std::vector<std::string> args;
    void add_implicit_vars(std::vector<std::string> &args, Expr e);
public:
    FuncRefVar(Internal::Function, const std::vector<Var> &);
        
    // Use this as the left-hand-side of a definition
    void operator=(Expr);

    // Short-hands for common reduction patterns
    void operator+=(Expr);
    void operator-=(Expr);
    void operator*=(Expr);
    void operator/=(Expr);

    // Override the usual assignment operator, so that f(x, y) = g(x, y) works
    void operator=(const FuncRefVar &e) {*this = Expr(e);}
        
    // Use this as a call to the function
    operator Expr() const;
};
    
/* A fragment of front-end syntax of the form f(x, y, z), where x,
 * y, z are Exprs. If could be the left hand side of a reduction
 * definition, or it could be a call to a function. We don't know
 * yet.
 */
class FuncRefExpr {
    Internal::Function func;
    std::vector<Expr> args;
    void add_implicit_vars(std::vector<Expr> &args, Expr e);
public:
    FuncRefExpr(Internal::Function, const std::vector<Expr> &);
    FuncRefExpr(Internal::Function, const std::vector<std::string> &);
        
    // Use this as the left-hand-side of a reduction definition
    void operator=(Expr);

    // Short-hands for common reduction patterns
    void operator+=(Expr);
    void operator-=(Expr);
    void operator*=(Expr);
    void operator/=(Expr);

    // Override the usual assignment operator, so that f(x, y) = g(x, y) works
    void operator=(const FuncRefExpr &e) {*this = Expr(e);}
        
    // Use this as a call to the function
    operator Expr() const;
};

// A wrapper around a schedule used for common schedule manipulations
class ScheduleHandle {
    Internal::Schedule &schedule;
    void set_dim_type(Var var, Internal::For::ForType t);
public:
    ScheduleHandle(Internal::Schedule &s) : schedule(s) {}
    ScheduleHandle &split(Var old, Var outer, Var inner, Expr factor);
    ScheduleHandle &parallel(Var var);
    ScheduleHandle &vectorize(Var var);
    ScheduleHandle &unroll(Var var);
    ScheduleHandle &vectorize(Var var, int factor);
    ScheduleHandle &unroll(Var var, int factor);
    ScheduleHandle &bound(Var var, Expr min, Expr extent);
    ScheduleHandle &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);
    ScheduleHandle &tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor);
    ScheduleHandle &reorder(Var x, Var y);
    ScheduleHandle &reorder(Var x, Var y, Var z);
    ScheduleHandle &reorder(Var x, Var y, Var z, Var w);
    ScheduleHandle &reorder(Var x, Var y, Var z, Var w, Var t);
};

/* A halide function. Define it, call it, schedule it. */
class Func {
    Internal::Function func;

    void add_implicit_vars(std::vector<Var> &);
    void add_implicit_vars(std::vector<Expr> &);

    // Save this state when we jit, so that we can run the function again cheaply
    Internal::JITCompiledModule compiled_module;
    void (*error_handler)(char *);
    void *(*custom_malloc)(size_t);
    void (*custom_free)(void *);

    std::vector<const void *> arg_values;

    // Some of those void *'s need to be rebound on every call if the image params change
    std::vector<std::pair<int, Internal::Parameter> > image_param_args;

public:        
    static void test();

    Func(Internal::Function f);
    Func(const std::string &name);
    Func();
    Func(Expr e);

    Buffer realize(int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0);
    void realize(Buffer dst);

    void compile_to_bitcode(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");
    void compile_to_object(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");
    void compile_to_header(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");
    void compile_to_assembly(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");    
    
    /** Compile to object file and header pair, with the given
     * arguments. Also names the C function to match the first
     * argument. 
     */
    //@{
    void compile_to_file(const std::string &filename_prefix, std::vector<Argument> args);
    void compile_to_file(const std::string &filename_prefix);
    void compile_to_file(const std::string &filename_prefix, Argument a);
    void compile_to_file(const std::string &filename_prefix, Argument a, Argument b);
    void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c);
    void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c, Argument d);
    void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c, Argument d, Argument e);
    // @}

    /** Eagerly jit compile the function to machine code. This
     * normally happens on the first call to realize. If you're
     * running your halide pipeline inside time-sensitive code and
     * wish to avoid including the time taken to compile a pipeline,
     * then you can call this ahead of time. */
    void compile_jit();

    /** Set the error handler function that be called in the case of
     * runtime errors during subsequent calls to realize */
    void set_error_handler(void (*handler)(char *));

    /** Set a custom malloc and free to use for subsequent calls to
     * realize. Malloc should return 32-byte aligned chunks of memory,
     * with 32-bytes extra allocated on the start and end so that
     * vector loads can spill off the end slightly. Metadata (e.g. the
     * base address of the region allocated) can go in this margin -
     * it is only read, not written. */
    void set_custom_allocator(void *(*malloc)(size_t), void (*free)(void *));

    const std::string &name() const;
    Expr value() const;
    int dimensions() const;

    /** Either the left-hand-side of a definition, or a call to a
     * functions that happens to only contain vars as arguments */
    // @{
    FuncRefVar operator()();
    FuncRefVar operator()(Var x);
    FuncRefVar operator()(Var x, Var y);
    FuncRefVar operator()(Var x, Var y, Var z);
    FuncRefVar operator()(Var x, Var y, Var z, Var w);
    FuncRefVar operator()(std::vector<Var>);
    // @}

    /** Either calls to the function, or the left-hand-side of a
     * reduction definition */
    // @{
    FuncRefExpr operator()(Expr x);
    FuncRefExpr operator()(Expr x, Expr y);
    FuncRefExpr operator()(Expr x, Expr y, Expr z);
    FuncRefExpr operator()(Expr x, Expr y, Expr z, Expr w);
    FuncRefExpr operator()(std::vector<Expr>);
    // @}

    /* Scheduling calls that control how the domain of this function is traversed */
    Func &split(Var old, Var outer, Var inner, Expr factor);
    Func &parallel(Var var);
    Func &vectorize(Var var);
    Func &unroll(Var var);
    Func &vectorize(Var var, int factor);
    Func &unroll(Var var, int factor);
    Func &bound(Var var, Expr min, Expr extent);
    Func &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);
    Func &tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor);
    Func &reorder(Var x, Var y);
    Func &reorder(Var x, Var y, Var z);
    Func &reorder(Var x, Var y, Var z, Var w);
    Func &reorder(Var x, Var y, Var z, Var w, Var t);

    /* Scheduling calls that control when and where this function is computed */
    Func &compute_at(Func f, Var var);
    Func &compute_at(Func f, RVar var);
    Func &compute_root();
    Func &store_at(Func f, Var var);
    Func &store_at(Func f, RVar var);
    Func &store_root();
    Func &compute_inline();

    Internal::Stmt lower();

    ScheduleHandle update();

    operator Expr() {
        return (*this)();
    }

    void operator=(Expr e) {
        (*this)() = e;
    }

};


}

#endif
