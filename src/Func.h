#ifndef HALIDE_FUNC_H
#define HALIDE_FUNC_H

/** \file
 *
 * Defines Func - the front-end handle on a halide function, and related classes.
 */

#include "IR.h"
#include "Var.h"
#include "Function.h"
#include "Param.h"
#include "Argument.h"
#include "RDom.h"
#include "JITCompiledModule.h"
#include "Image.h"
#include "Target.h"
#include "Tuple.h"
#include "Target.h"

namespace Halide {

/** A fragment of front-end syntax of the form f(x, y, z), where x,
 * y, z are Vars. It could be the left-hand side of a function
 * definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class FuncRefExpr;

/** A class that can represent Vars or RVars. Used for reorder calls
 * which can accept a mix of either. */
struct VarOrRVar {
    VarOrRVar(const Var &v) : var(v), is_rvar(false) {}
    VarOrRVar(const RVar &r) : rvar(r), is_rvar(true) {}
    VarOrRVar(const RDom &r) : rvar(RVar(r)), is_rvar(true) {}

    const std::string &name() const {
        if (is_rvar) return rvar.name();
        else return var.name();
    }

    const Var var;
    const RVar rvar;
    const bool is_rvar;
};

class FuncRefVar {
    Internal::Function func;
    int implicit_placeholder_pos;
    std::vector<std::string> args;
    std::vector<std::string> args_with_implicit_vars(const std::vector<Expr> &e) const;
public:
    FuncRefVar(Internal::Function, const std::vector<Var> &, int placeholder_pos = -1);

    /**  Use this as the left-hand-side of a definition. */
    EXPORT void operator=(Expr);

    /** Use this as the left-hand-side of a definition for a Func with
     * multiple outputs. */
    EXPORT void operator=(const Tuple &);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
     */
    EXPORT void operator+=(Expr);

    /** Define this function as a sum reduction over the given
     * expression. The expression should refer to some RDom to sum
     * over. If the function does not already have a pure definition,
     * this sets it to zero.
     */
    EXPORT void operator-=(Expr);

    /** Define this function as a product reduction. The expression
     * should refer to some RDom to take the product over. If the
     * function does not already have a pure definition, this sets it
     * to 1.
     */
    EXPORT void operator*=(Expr);

    /** Define this function as the product reduction over the inverse
     * of the expression. The expression should refer to some RDom to
     * take the product over. If the function does not already have a
     * pure definition, this sets it to 1.
     */
    EXPORT void operator/=(Expr);

    /** Override the usual assignment operator, so that
     * f(x, y) = g(x, y) defines f.
     */
    // @{
    EXPORT void operator=(const FuncRefVar &e);
    EXPORT void operator=(const FuncRefExpr &e);
    // @}

    /** Use this FuncRefVar as a call to the function, and not as the
     * left-hand-side of a definition. Only works for single-output
     * funcs.
     */
    EXPORT operator Expr() const;

    /** When a FuncRefVar refers to a function that provides multiple
     * outputs, you can access each output as an Expr using
     * operator[] */
    EXPORT Expr operator[](int) const;

    /** How many outputs does the function this refers to produce. */
    EXPORT size_t size() const;
};

/** A fragment of front-end syntax of the form f(x, y, z), where x, y,
 * z are Exprs. If could be the left hand side of a reduction
 * definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class FuncRefExpr {
    Internal::Function func;
    int implicit_placeholder_pos;
    std::vector<Expr> args;
    std::vector<Expr> args_with_implicit_vars(const std::vector<Expr> &e) const;
public:
    FuncRefExpr(Internal::Function, const std::vector<Expr> &,
                int placeholder_pos = -1);
    FuncRefExpr(Internal::Function, const std::vector<std::string> &,
                int placeholder_pos = -1);

    /** Use this as the left-hand-side of a reduction definition (see
     * \ref RDom). The function must already have a pure definition.
     */
    EXPORT void operator=(Expr);

    /** Use this as the left-hand-side of a reduction definition for a
     * Func with multiple outputs. */
    EXPORT void operator=(const Tuple &);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
     */
    EXPORT void operator+=(Expr);

    /** Define this function as a sum reduction over the given
     * expression. The expression should refer to some RDom to sum
     * over. If the function does not already have a pure definition,
     * this sets it to zero.
     */
    EXPORT void operator-=(Expr);

    /** Define this function as a product reduction. The expression
     * should refer to some RDom to take the product over. If the
     * function does not already have a pure definition, this sets it
     * to 1.
     */
    EXPORT void operator*=(Expr);

    /** Define this function as the product reduction over the inverse
     * of the expression. The expression should refer to some RDom to
     * take the product over. If the function does not already have a
     * pure definition, this sets it to 1.
     */
    EXPORT void operator/=(Expr);

    /* Override the usual assignment operator, so that
     * f(x, y) = g(x, y) defines f.
     */
    // @{
    EXPORT void operator=(const FuncRefVar &);
    EXPORT void operator=(const FuncRefExpr &);
    // @}

    /** Use this as a call to the function, and not the left-hand-side
     * of a definition. Only works for single-output Funcs. */
    EXPORT operator Expr() const;

    /** When a FuncRefExpr refers to a function that provides multiple
     * outputs, you can access each output as an Expr using
     * operator[].
     */
    EXPORT Expr operator[](int) const;

    /** How many outputs does the function this refers to produce. */
    EXPORT size_t size() const;
};

enum GPUAPI {
    GPU_DEFAULT,
    GPU_CUDA,
    GPU_OpenCL,
    GPU_GLSL
};

/** A wrapper around a schedule used for common schedule manipulations */
class ScheduleHandle {
    Internal::Schedule &schedule;
    void set_dim_type(Var var, Internal::For::ForType t);
    void dump_argument_list();
public:
    ScheduleHandle(Internal::Schedule &s) : schedule(s) {s.touched = true;}

    /** Scheduling calls that control how the domain of this update is
     * traversed. See the documentation for Func for the meanings. */
    // @{

    EXPORT ScheduleHandle &split(Var old, Var outer, Var inner, Expr factor);
    EXPORT ScheduleHandle &fuse(Var inner, Var outer, Var fused);
    EXPORT ScheduleHandle &parallel(Var var);
    EXPORT ScheduleHandle &vectorize(Var var);
    EXPORT ScheduleHandle &unroll(Var var);
    EXPORT ScheduleHandle &parallel(Var var, Expr task_size);
    EXPORT ScheduleHandle &vectorize(Var var, int factor);
    EXPORT ScheduleHandle &unroll(Var var, int factor);
    EXPORT ScheduleHandle &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);
    EXPORT ScheduleHandle &tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor);
    EXPORT ScheduleHandle &reorder(const std::vector<VarOrRVar> &vars);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w, VarOrRVar t);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w, VarOrRVar t1, VarOrRVar t2);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                                   VarOrRVar t3);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                                   VarOrRVar t3, VarOrRVar t4);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                                   VarOrRVar t3, VarOrRVar t4, VarOrRVar t5);
    EXPORT ScheduleHandle &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                                   VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                                   VarOrRVar t3, VarOrRVar t4, VarOrRVar t5,
                                   VarOrRVar t6);
    EXPORT ScheduleHandle &rename(Var old_name, Var new_name);

    EXPORT ScheduleHandle &gpu_threads(Var thread_x, GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_threads(Var thread_x, Var thread_y, GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_threads(Var thread_x, Var thread_y, Var thread_z, GPUAPI gpu_api = GPU_DEFAULT);

    EXPORT ScheduleHandle &gpu_blocks(Var block_x, GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_blocks(Var block_x, Var block_y, GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_blocks(Var block_x, Var block_y, Var block_z, GPUAPI gpu_api = GPU_DEFAULT);

    EXPORT ScheduleHandle &gpu(Var block_x, Var thread_x, GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu(Var block_x, Var block_y,
                               Var thread_x, Var thread_y,
			       GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu(Var block_x, Var block_y, Var block_z,
                               Var thread_x, Var thread_y, Var thread_z,
			       GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_tile(Var x, Expr x_size, GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_tile(Var x, Var y, Expr x_size, Expr y_size,
				    GPUAPI gpu_api = GPU_DEFAULT);
    EXPORT ScheduleHandle &gpu_tile(Var x, Var y, Var z,
                                    Expr x_size, Expr y_size, Expr z_size, GPUAPI gpu_api = GPU_DEFAULT);
    // @}

    // These calls are for legacy compatibility only.
    EXPORT ScheduleHandle &cuda_threads(Var thread_x) {
        return gpu_threads(thread_x);
    }
    EXPORT ScheduleHandle &cuda_threads(Var thread_x, Var thread_y) {
        return gpu_threads(thread_x, thread_y);
    }
    EXPORT ScheduleHandle &cuda_threads(Var thread_x, Var thread_y, Var thread_z) {
        return gpu_threads(thread_x, thread_y, thread_z);
    }

    EXPORT ScheduleHandle &cuda_blocks(Var block_x) {
        return gpu_blocks(block_x);
    }
    EXPORT ScheduleHandle &cuda_blocks(Var block_x, Var block_y) {
        return gpu_blocks(block_x, block_y);
    }
    EXPORT ScheduleHandle &cuda_blocks(Var block_x, Var block_y, Var block_z) {
        return gpu_blocks(block_x, block_y, block_z);
    }

    EXPORT ScheduleHandle &cuda(Var block_x, Var thread_x) {
        return gpu(block_x, thread_x);
    }
    EXPORT ScheduleHandle &cuda(Var block_x, Var block_y,
                                Var thread_x, Var thread_y) {
        return gpu(block_x, thread_x, block_y, thread_y);
    }
    EXPORT ScheduleHandle &cuda(Var block_x, Var block_y, Var block_z,
                                Var thread_x, Var thread_y, Var thread_z) {
        return gpu(block_x, thread_x, block_y, thread_y, block_z, thread_z);
    }
    EXPORT ScheduleHandle &cuda_tile(Var x, int x_size) {
        return gpu_tile(x, x_size);
    }
    EXPORT ScheduleHandle &cuda_tile(Var x, Var y, int x_size, int y_size) {
        return gpu_tile(x, y, x_size, y_size);
    }
    EXPORT ScheduleHandle &cuda_tile(Var x, Var y, Var z,
                                     int x_size, int y_size, int z_size) {
        return gpu_tile(x, y, z, x_size, y_size, z_size);
    }
};

/** A halide function. This class represents one stage in a Halide
 * pipeline, and is the unit by which we schedule things. By default
 * they are aggressively inlined, so you are encouraged to make lots
 * of little functions, rather than storing things in Exprs. */
class Func {

    /** A handle on the internal halide function that this
     * represents */
    Internal::Function func;

    /** When you make a reference to this function to with fewer
     * arguments that it has dimensions, the argument list is bulked
     * up with 'implicit' vars with canonical names. This lets you
     * pass around partially-applied halide functions. */
    // @{
    int add_implicit_vars(std::vector<Var> &) const;
    int add_implicit_vars(std::vector<Expr> &) const;
    // @}

    /** The lowered imperative form of this function. Cached here so
     * that recompilation for different targets doesn't require
     * re-lowering */
    Internal::Stmt lowered;

    /** A JIT-compiled version of this function that we save so that
     * we don't have to rejit every time we want to evaluated it. */
    Internal::JITCompiledModule compiled_module;

    /** The current error handler used for realizing this
     * function. May be NULL. Only relevant when jitting. */
    void (*error_handler)(void *user_context, const char *);

    /** The current custom allocator used for realizing this
     * function. May be NULL. Only relevant when jitting. */
    // @{
    void *(*custom_malloc)(void *user_context, size_t);
    void (*custom_free)(void *user_context, void *ptr);
    // @}

    /** The current custom parallel task launcher and handler for
     * realizing this function. May be NULL. */
    // @{
    int (*custom_do_par_for)(void *user_context,
                             int (*)(void *, int, uint8_t *),
                             int, int, uint8_t *);
    int (*custom_do_task)(void *user_context, int (*)(void *, int, uint8_t *),
                          int, uint8_t *);
    // @}

    /** The current custom tracing function. May be NULL. */
    // @{
    int32_t (*custom_trace)(void *, const halide_trace_event *);

    // @}

    /** The random seed to use for realizations of this function. */
    uint32_t random_seed;

    /** Pointers to current values of the automatically inferred
     * arguments (buffers and scalars) used to realize this
     * function. Only relevant when jitting. We can hold these things
     * with raw pointers instead of reference-counted handles, because
     * func indirectly holds onto them with reference-counted handles
     * via its value Expr. */
    std::vector<const void *> arg_values;

    /** Some of the arg_values need to be rebound on every call if the
     * image params change. The pointers for the scalar params will
     * still be valid though. */
    std::vector<std::pair<int, Internal::Parameter> > image_param_args;

public:
    EXPORT static void test();

    /** Declare a new undefined function with the given name */
    EXPORT explicit Func(const std::string &name);

    /** Declare a new undefined function with an
     * automatically-generated unique name */
    EXPORT Func();

    /** Declare a new function with an automatically-generated unique
     * name, and define it to return the given expression (which may
     * not contain free variables). */
    EXPORT explicit Func(Expr e);

    /** Construct a new Func to wrap an existing, already-define
     * Function object. */
    EXPORT explicit Func(Internal::Function f);

    /** Evaluate this function over some rectangular domain and return
     * the resulting buffer or buffers. Performs compilation if the
     * Func has not previously been realized and jit_compile has not
     * been called. The returned Buffer should probably be instantly
     * wrapped in an Image class of the appropriate type. That is, do
     * this:
     *
     \code
     f(x) = sin(x);
     Image<float> im = f.realize(...);
     \endcode
     *
     * not this:
     *
     \code
     f(x) = sin(x)
     Buffer im = f.realize(...)
     \endcode
     *
     * If your Func has multiple values, because you defined it using
     * a Tuple, then casting the result of a realize call to a buffer
     * or image will produce a run-time error. Instead you should do the
     * following:
     *
     \code
     f(x) = Tuple(x, sin(x));
     Realization r = f.realize(...);
     Image<int> im0 = r[0];
     Image<float> im1 = r[1];
     \endcode
     *
     */
    // @{
    EXPORT Realization realize(std::vector<int32_t> sizes, const Target &target = get_jit_target_from_environment());
    EXPORT Realization realize(int x_size, int y_size, int z_size, int w_size,
                               const Target &target = get_jit_target_from_environment());
    EXPORT Realization realize(int x_size, int y_size, int z_size,
                               const Target &target = get_jit_target_from_environment());
    EXPORT Realization realize(int x_size, int y_size,
                               const Target &target = get_jit_target_from_environment());
    EXPORT Realization realize(int x_size = 0,
                               const Target &target = get_jit_target_from_environment());
    // @}

    /** Evaluate this function into an existing allocated buffer or
     * buffers. If the buffer is also one of the arguments to the
     * function, strange things may happen, as the pipeline isn't
     * necessarily safe to run in-place. If you pass multiple buffers,
     * they must have matching sizes. */
    // @{
    EXPORT void realize(Realization dst, const Target &target = get_jit_target_from_environment());
    EXPORT void realize(Buffer dst, const Target &target = get_jit_target_from_environment());
    // @}

    /** For a given size of output, or a given output buffer,
     * determine the bounds required of all unbound ImageParams
     * referenced. Communicates the result by allocating new buffers
     * of the appropriate size and binding them to the unbound
     * ImageParams. */
    // @{
    EXPORT void infer_input_bounds(int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0);
    EXPORT void infer_input_bounds(Realization dst);
    EXPORT void infer_input_bounds(Buffer dst);
    // @}

    /** Unbind all ImageParams. Useful when iteratively re-running
     * infer_input_bounds. */
    EXPORT void unbind_image_params();

    /** Statically compile this function to llvm bitcode, with the
     * given filename (which should probably end in .bc), type
     * signature, and C function name (which defaults to the same name
     * as this halide function */
    //@{
    EXPORT void compile_to_bitcode(const std::string &filename, std::vector<Argument>, const std::string &fn_name,
                                   const Target &target = get_target_from_environment());
    EXPORT void compile_to_bitcode(const std::string &filename, std::vector<Argument>,
                                   const Target &target = get_target_from_environment());
    // @}

    /** Statically compile this function to an object file, with the
     * given filename (which should probably end in .o or .obj), type
     * signature, and C function name (which defaults to the same name
     * as this halide function. You probably don't want to use this
     * directly; call compile_to_file instead. */
    //@{
    EXPORT void compile_to_object(const std::string &filename, std::vector<Argument>, const std::string &fn_name,
                                  const Target &target = get_target_from_environment());
    EXPORT void compile_to_object(const std::string &filename, std::vector<Argument>,
                                  const Target &target = get_target_from_environment());
    // @}

    /** Emit a header file with the given filename for this
     * function. The header will define a function with the type
     * signature given by the second argument, and a name given by the
     * third. The name defaults to the same name as this halide
     * function. You don't actually have to have defined this function
     * yet to call this. You probably don't want to use this directly;
     * call compile_to_file instead. */
    EXPORT void compile_to_header(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");

    /** Statically compile this function to text assembly equivalent
     * to the object file generated by compile_to_object. This is
     * useful for checking what Halide is producing without having to
     * disassemble anything, or if you need to feed the assembly into
     * some custom toolchain to produce an object file (e.g. iOS) */
    //@{
    EXPORT void compile_to_assembly(const std::string &filename, std::vector<Argument>, const std::string &fn_name,
                                    const Target &target = get_target_from_environment());
    EXPORT void compile_to_assembly(const std::string &filename, std::vector<Argument>,
                                    const Target &target = get_target_from_environment());
    // @}
    /** Statically compile this function to C source code. This is
     * useful for providing fallback code paths that will compile on
     * many platforms. Vectorization will fail, and parallelization
     * will produce serial code. */
    EXPORT void compile_to_c(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");

    /** Write out an internal representation of lowered code. Useful
     * for analyzing and debugging scheduling. Canonical extension is
     * .stmt, which must be supplied in filename. */
    EXPORT void compile_to_lowered_stmt(const std::string &filename);

    /** Compile to object file and header pair, with the given
     * arguments. Also names the C function to match the first
     * argument.
     */
    //@{
    EXPORT void compile_to_file(const std::string &filename_prefix, std::vector<Argument> args,
                                const Target &target = get_target_from_environment());
    EXPORT void compile_to_file(const std::string &filename_prefix,
                                const Target &target = get_target_from_environment());
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a,
                                const Target &target = get_target_from_environment());
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b,
                                const Target &target = get_target_from_environment());
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c,
                                const Target &target = get_target_from_environment());
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c, Argument d,
                                const Target &target = get_target_from_environment());
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c, Argument d, Argument e,
                                const Target &target = get_target_from_environment());
    // @}

    /** Eagerly jit compile the function to machine code. This
     * normally happens on the first call to realize. If you're
     * running your halide pipeline inside time-sensitive code and
     * wish to avoid including the time taken to compile a pipeline,
     * then you can call this ahead of time. Returns the raw function
     * pointer to the compiled pipeline. Default is to use the Target
     * returned from Halide::get_jit_target_from_environment()
     */
     EXPORT void *compile_jit(const Target &target = get_jit_target_from_environment());

    /** Set the error handler function that be called in the case of
     * runtime errors during halide pipelines. If you are compiling
     * statically, you can also just define your own function with
     * signature
     \code
     extern "C" void halide_error(void *user_context, const char *);
     \endcode
     * This will clobber Halide's version.
     */
    EXPORT void set_error_handler(void (*handler)(void *, const char *));

    /** Set a custom malloc and free for halide to use. Malloc should
     * return 32-byte aligned chunks of memory, and it should be safe
     * for Halide to read slightly out of bounds (up to 8 bytes before
     * the start or beyond the end). If compiling statically, routines
     * with appropriate signatures can be provided directly
    \code
     extern "C" void *halide_malloc(void *, size_t)
     extern "C" void halide_free(void *, void *)
     \endcode
     * These will clobber Halide's versions. See \file HalideRuntime.h
     * for declarations.
     */
    EXPORT void set_custom_allocator(void *(*malloc)(void *, size_t),
                                     void (*free)(void *, void *));

    /** Set a custom task handler to be called by the parallel for
     * loop. It is useful to set this if you want to do some
     * additional bookkeeping at the granularity of parallel
     * tasks. The default implementation does this:
     \code
     extern "C" int halide_do_task(void *user_context,
                                   int (*f)(void *, int, uint8_t *),
                                   int idx, uint8_t *state) {
         return f(user_context, idx, state);
     }
     \endcode
     * If you are statically compiling, you can also just define your
     * own version of the above function, and it will clobber Halide's
     * version.
     *
     * If you're trying to use a custom parallel runtime, you probably
     * don't want to call this. See instead \ref Func::set_custom_do_par_for .
    */
    EXPORT void set_custom_do_task(
        int (*custom_do_task)(void *, int (*)(void *, int, uint8_t *),
                              int, uint8_t *));

    /** Set a custom parallel for loop launcher. Useful if your app
     * already manages a thread pool. The default implementation is
     * equivalent to this:
     \code
     extern "C" int halide_do_par_for(void *user_context,
                                      int (*f)(void *, int, uint8_t *),
                                      int min, int extent, uint8_t *state) {
         int exit_status = 0;
         parallel for (int idx = min; idx < min+extent; idx++) {
             int job_status = halide_do_task(user_context, f, idx, state);
             if (job_status) exit_status = job_status;
         }
         return exit_status;
     }
     \endcode
     *
     * However, notwithstanding the above example code, if one task
     * fails, we may skip over other tasks, and if two tasks return
     * different error codes, we may select one arbitrarily to return.
     *
     * If you are statically compiling, you can also just define your
     * own version of the above function, and it will clobber Halide's
     * version.
     */
    EXPORT void set_custom_do_par_for(
        int (*custom_do_par_for)(void *, int (*)(void *, int, uint8_t *), int,
                                 int, uint8_t *));

    /** Set custom routines to call when tracing is enabled. Call this
     * on the output Func of your pipeline. This then sets custom
     * routines for the entire pipeline, not just calls to this
     * Func.
     *
     * If you are statically compiling, you can also just define your
     * own versions of the tracing functions (see HalideRuntime.h),
     * and they will clobber Halide's versions. */
    EXPORT void set_custom_trace(Internal::JITCompiledModule::TraceFn);

    /** When this function is compiled, include code that dumps its
     * values to a file after it is realized, for the purpose of
     * debugging.
     *
     * If filename ends in ".tif" or ".tiff" (case insensitive) the file
     * is in TIFF format and can be read by standard tools. Oherwise, the
     * file format is as follows:
     *
     * All data is in the byte-order of the target platform.  First, a
     * 20 byte-header containing four 32-bit ints, giving the extents
     * of the first four dimensions.  Dimensions beyond four are
     * folded into the fourth.  Then, a fifth 32-bit int giving the
     * data type of the function. The typecodes are given by: float =
     * 0, double = 1, uint8_t = 2, int8_t = 3, uint16_t = 4, int16_t =
     * 5, uint32_t = 6, int32_t = 7, uint64_t = 8, int64_t = 9. The
     * data follows the header, as a densely packed array of the given
     * size and the given type. If given the extension .tmp, this file
     * format can be natively read by the program ImageStack. */
    EXPORT void debug_to_file(const std::string &filename);

    /** The name of this function, either given during construction,
     * or automatically generated. */
    EXPORT const std::string &name() const;

    /** Get the pure arguments. */
    EXPORT std::vector<Var> args() const;

    /** The right-hand-side value of the pure definition of this
     * function. Causes an error if there's no pure definition, or if
     * the function is defined to return multiple values. */
    EXPORT Expr value() const;

    /** The values returned by this function. An error if the function
     * has not been been defined. Returns a Tuple with one element for
     * functions defined to return a single value. */
    EXPORT Tuple values() const;

    /** Does this function have at least a pure definition. */
    EXPORT bool defined() const;

    /** Get the left-hand-side of the reduction definition. An empty
     * vector if there's no reduction definition. If there are
     * multiple reduction definitions for this function, use the
     * argument to select which one you want. */
    EXPORT const std::vector<Expr> &reduction_args(int idx = 0) const;

    /** Get the right-hand-side of a reduction definition. An error if
     * there's no reduction definition. If there are multiple
     * reduction definitions for this function, use the argument to
     * select which one you want. */
    EXPORT Expr reduction_value(int idx = 0) const;

    /** Get the right-hand-side of a reduction definition for
     * functions that returns multiple values. An error if there's no
     * reduction definition. Returns a Tuple with one element for
     * functions that return a single value. */
    EXPORT Tuple reduction_values(int idx = 0) const;

    /** Get the reduction domain for a reduction definition. */
    EXPORT RDom reduction_domain(int idx = 0) const;

    /** Is this function a reduction (i.e. does it have at least one
     * reduction definition)? */
    EXPORT bool is_reduction() const;

    /** How many reduction definitions does this function have? */
    EXPORT int num_reduction_definitions() const;

    /** Is this function an external stage? That is, was it defined
     * using define_extern? */
    EXPORT bool is_extern() const;

    /** Add an extern definition for this Func. This lets you define a
     * Func that represents an external pipeline stage. You can, for
     * example, use it to wrap a call to an extern library such as
     * fftw. */
    // @{
    EXPORT void define_extern(const std::string &function_name,
                              const std::vector<ExternFuncArgument> &params,
                              Type t,
                              int dimensionality) {
        define_extern(function_name, params, Internal::vec<Type>(t), dimensionality);
    }

    EXPORT void define_extern(const std::string &function_name,
                              const std::vector<ExternFuncArgument> &params,
                              const std::vector<Type> &types,
                              int dimensionality);
    // @}

    /** Get the types of the outputs of this Func. */
    EXPORT const std::vector<Type> &output_types() const;

    /** Get the number of outputs of this Func. Corresponds to the
     * size of the Tuple this Func was defined to return. */
    EXPORT int outputs() const;

    /** Get the name of the extern function called for an extern
     * definition. */
    EXPORT const std::string &extern_function_name() const;

    /** The dimensionality (number of arguments) of this
     * function. Zero if the function is not yet defined. */
    EXPORT int dimensions() const;

    /** Construct either the left-hand-side of a definition, or a call
     * to a functions that happens to only contain vars as
     * arguments. If the function has already been defined, and fewer
     * arguments are given than the function has dimensions, then
     * enough implicit vars are added to the end of the argument list
     * to make up the difference (see \ref Var::implicit) */
    // @{
    EXPORT FuncRefVar operator()() const;
    EXPORT FuncRefVar operator()(Var x) const;
    EXPORT FuncRefVar operator()(Var x, Var y) const;
    EXPORT FuncRefVar operator()(Var x, Var y, Var z) const;
    EXPORT FuncRefVar operator()(Var x, Var y, Var z, Var w) const;
    EXPORT FuncRefVar operator()(Var x, Var y, Var z, Var w, Var u) const;
    EXPORT FuncRefVar operator()(Var x, Var y, Var z, Var w, Var u, Var v) const;
    EXPORT FuncRefVar operator()(std::vector<Var>) const;
    // @}

    /** Either calls to the function, or the left-hand-side of a
     * reduction definition (see \ref RDom). If the function has
     * already been defined, and fewer arguments are given than the
     * function has dimensions, then enough implicit vars are added to
     * the end of the argument list to make up the difference. (see
     * \ref Var::implicit)*/
    // @{
    EXPORT FuncRefExpr operator()(Expr x) const;
    EXPORT FuncRefExpr operator()(Expr x, Expr y) const;
    EXPORT FuncRefExpr operator()(Expr x, Expr y, Expr z) const;
    EXPORT FuncRefExpr operator()(Expr x, Expr y, Expr z, Expr w) const;
    EXPORT FuncRefExpr operator()(Expr x, Expr y, Expr z, Expr w, Expr u) const;
    EXPORT FuncRefExpr operator()(Expr x, Expr y, Expr z, Expr w, Expr u, Expr v) const;
    EXPORT FuncRefExpr operator()(std::vector<Expr>) const;
    // @}

    /** Split a dimension into inner and outer subdimensions with the
     * given names, where the inner dimension iterates from 0 to
     * factor-1. The inner and outer subdimensions can then be dealt
     * with using the other scheduling calls. It's ok to reuse the old
     * variable name as either the inner or outer variable. */
    EXPORT Func &split(Var old, Var outer, Var inner, Expr factor);

    /** Join two dimensions into a single fused dimenion. The fused
     * dimension covers the product of the extents of the inner and
     * outer dimensions given. */
    EXPORT Func &fuse(Var inner, Var outer, Var fused);


    /** Mark a dimension to be traversed in parallel */
    EXPORT Func &parallel(Var var);

    /** Split a dimension by the given task_size, and the parallelize the
     * outer dimension. This creates parallel tasks that have size
     * task_size. After this call, var refers to the outer dimension of
     * the split. The inner dimension has a new anonymous name. If you
     * wish to mutate it, or schedule with respect to it, do the split
     * manually. */
    EXPORT Func &parallel(Var var, Expr task_size);

    /** Mark a dimension to be computed all-at-once as a single
     * vector. The dimension should have constant extent -
     * e.g. because it is the inner dimension following a split by a
     * constant factor. For most uses of vectorize you want the two
     * argument form. The variable to be vectorized should be the
     * innermost one. */
    EXPORT Func &vectorize(Var var);

    /** Mark a dimension to be completely unrolled. The dimension
     * should have constant extent - e.g. because it is the inner
     * dimension following a split by a constant factor. For most uses
     * of unroll you want the two-argument form. */
    EXPORT Func &unroll(Var var);

    /** Split a dimension by the given factor, then vectorize the
     * inner dimension. This is how you vectorize a loop of unknown
     * size. The variable to be vectorized should be the innermost
     * one. After this call, var refers to the outer dimension of the
     * split. */
    EXPORT Func &vectorize(Var var, int factor);

    /** Split a dimension by the given factor, then unroll the inner
     * dimension. This is how you unroll a loop of unknown size by
     * some constant factor. After this call, var refers to the outer
     * dimension of the split. */
    EXPORT Func &unroll(Var var, int factor);

    /** Statically declare that the range over which a function should
     * be evaluated is given by the second and third arguments. This
     * can let Halide perform some optimizations. E.g. if you know
     * there are going to be 4 color channels, you can completely
     * vectorize the color channel dimension without the overhead of
     * splitting it up. If bounds inference decides that it requires
     * more of this function than the bounds you have stated, a
     * runtime error will occur when you try to run your pipeline. */
    EXPORT Func &bound(Var var, Expr min, Expr extent);

    /** Split two dimensions at once by the given factors, and then
     * reorder the resulting dimensions to be xi, yi, xo, yo from
     * innermost outwards. This gives a tiled traversal. */
    EXPORT Func &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);

    /** A shorter form of tile, which reuses the old variable names as
     * the new outer dimensions */
    EXPORT Func &tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor);

    /** Reorder variables to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(const std::vector<VarOrRVar> &vars);

    /** Reorder two dimensions so that x is traversed inside y. Does
     * not affect the nesting order of other dimensions. E.g, if you
     * say foo(x, y, z, w) = bar; foo.reorder(w, x); then foo will be
     * traversed in the order (w, y, z, x), from innermost
     * outwards. */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y);

    /** Reorder three dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z);

    /** Reorder four dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w);

    /** Reorder five dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w, VarOrRVar t);

    /** Reorder six dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w, VarOrRVar t1, VarOrRVar t2);

    /** Reorder seven dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                         VarOrRVar t3);

    /** Reorder eight dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                         VarOrRVar t3, VarOrRVar t4);

    /** Reorder nine dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                         VarOrRVar t3, VarOrRVar t4, VarOrRVar t5);

    /** Reorder ten dimensions to have the given nesting order, from
     * innermost out */
    EXPORT Func &reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                         VarOrRVar w, VarOrRVar t1, VarOrRVar t2,
                         VarOrRVar t3, VarOrRVar t4, VarOrRVar t5,
                         VarOrRVar t6);

    /** Rename a dimension. Equivalent to split with a inner size of one. */
    EXPORT Func &rename(Var old_name, Var new_name);

    /** Tell Halide that the following dimensions correspond to GPU
     * thread indices. This is useful if you compute a producer
     * function within the block indices of a consumer function, and
     * want to control how that function's dimensions map to GPU
     * threads. If the selected target is not an appropriate GPU, this
     * just marks those dimensions as parallel. */
    // @{
    EXPORT Func &gpu_threads(Var thread_x, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu_threads(Var thread_x, Var thread_y, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu_threads(Var thread_x, Var thread_y, Var thread_z, GPUAPI gpuapi = GPU_DEFAULT);
    // @}

    /** \deprecated Old name for #gpu_threads. */
    // @{
    EXPORT Func &cuda_threads(Var thread_x) {
        return gpu_threads(thread_x);
    }
    EXPORT Func &cuda_threads(Var thread_x, Var thread_y) {
        return gpu_threads(thread_x, thread_y);
    }
    EXPORT Func &cuda_threads(Var thread_x, Var thread_y, Var thread_z) {
        return gpu_threads(thread_x, thread_y, thread_z);
    }
    // @}

    /** Tell Halide that the following dimensions correspond to GPU
     * block indices. This is useful for scheduling stages that will
     * run serially within each GPU block. If the selected target is
     * not ptx, this just marks those dimensions as parallel. */
    // @{
    EXPORT Func &gpu_blocks(Var block_x, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu_blocks(Var block_x, Var block_y, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu_blocks(Var block_x, Var block_y, Var block_z, GPUAPI gpuapi = GPU_DEFAULT);
    // @}

    /** \deprecated Old name for #gpu_blocks. */
    // @{
    EXPORT Func &cuda_blocks(Var block_x) {
        return gpu_blocks(block_x);
    }
    EXPORT Func &cuda_blocks(Var block_x, Var block_y) {
        return gpu_blocks(block_x, block_y);
    }
    EXPORT Func &cuda_blocks(Var block_x, Var block_y, Var block_z) {
        return gpu_blocks(block_x, block_y, block_z);
    }
    // @}

    /** Tell Halide that the following dimensions correspond to GPU
     * block indices and thread indices. If the selected target is not
     * ptx, these just mark the given dimensions as parallel. The
     * dimensions are consumed by this call, so do all other
     * unrolling, reordering, etc first. */
    // @{
    EXPORT Func &gpu(Var block_x, Var thread_x, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu(Var block_x, Var block_y,
		     Var thread_x, Var thread_y, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu(Var block_x, Var block_y, Var block_z,
                     Var thread_x, Var thread_y, Var thread_z, GPUAPI gpuapi = GPU_DEFAULT);
    // @}

    /** \deprecated Old name for #gpu. */
    // @{
    EXPORT Func &cuda(Var block_x, Var thread_x) {
        return gpu(block_x, thread_x);
    }
    EXPORT Func &cuda(Var block_x, Var block_y,
                      Var thread_x, Var thread_y) {
        return gpu(block_x, thread_x, block_y, thread_y);
    }
    EXPORT Func &cuda(Var block_x, Var block_y, Var block_z,
                      Var thread_x, Var thread_y, Var thread_z) {
        return gpu(block_x, thread_x, block_y, thread_y, block_z, thread_z);
    }
    // @}

    /** Short-hand for tiling a domain and mapping the tile indices
     * to GPU block indices and the coordinates within each tile to
     * GPU thread indices. Consumes the variables given, so do all
     * other scheduling first. */
    // @{
    EXPORT Func &gpu_tile(Var x, int x_size, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu_tile(Var x, Var y, int x_size, int y_size, GPUAPI gpuapi = GPU_DEFAULT);
    EXPORT Func &gpu_tile(Var x, Var y, Var z,
                          int x_size, int y_size, int z_size, GPUAPI gpuapi = GPU_DEFAULT);
    // @}

    /** \deprecated Old name for #gpu_tile. */
    // @{
    EXPORT Func &cuda_tile(Var x, int x_size) {
        return gpu_tile(x, x_size);
    }
    EXPORT Func &cuda_tile(Var x, Var y, int x_size, int y_size) {
        return gpu_tile(x, y, x_size, y_size);
    }
    EXPORT Func &cuda_tile(Var x, Var y, Var z,
                           int x_size, int y_size, int z_size) {
        return gpu_tile(x, y, z, x_size, y_size, z_size);
    }
    // @}

    /** Specify how the storage for the function is laid out. These
     * calls let you specify the nesting order of the dimensions. For
     * example, foo.reorder_storage(y, x) tells Halide to use
     * column-major storage for any realizations of foo, without
     * changing how you refer to foo in the code. You may want to do
     * this if you intend to vectorize across y. When representing
     * color images, foo.reorder_storage(c, x, y) specifies packed
     * storage (red, green, and blue values adjacent in memory), and
     * foo.reorder_storage(x, y, c) specifies planar storage (entire
     * red, green, and blue images one after the other in memory).
     *
     * If you leave out some dimensions, those remain in the same
     * positions in the nesting order while the specified variables
     * are reordered around them. */
    // @{
    EXPORT Func &reorder_storage(Var x, Var y);
    EXPORT Func &reorder_storage(Var x, Var y, Var z);
    EXPORT Func &reorder_storage(Var x, Var y, Var z, Var w);
    EXPORT Func &reorder_storage(Var x, Var y, Var z, Var w, Var t);
    // @}

    /** Compute this function as needed for each unique value of the
     * given var for the given calling function f.
     *
     * For example, consider the simple pipeline:
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x, y+1) + g(x+1, y) + g(x+1, y+1);
     \endcode
     *
     * If we schedule f like so:
     *
     \code
     g.compute_at(f, x);
     \endcode
     *
     * Then the C code equivalent to this pipeline will look like this
     *
     \code

     int f[height][width];
     for (int y = 0; y < height; y++) {
         for (int x = 0; x < width; x++) {
             int g[2][2];
             g[0][0] = x*y;
             g[0][1] = (x+1)*y;
             g[1][0] = x*(y+1);
             g[1][1] = (x+1)*(y+1);
             f[y][x] = g[0][0] + g[1][0] + g[0][1] + g[1][1];
         }
     }

     \endcode
     *
     * The allocation and computation of g is within f's loop over x,
     * and enough of g is computed to satisfy all that f will need for
     * that iteration. This has excellent locality - values of g are
     * used as soon as they are computed, but it does redundant
     * work. Each value of g ends up getting computed four times. If
     * we instead schedule f like so:
     *
     \code
     g.compute_at(f, y);
     \endcode
     *
     * The equivalent C code is:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][width+1];
         for (int x = 0; x < width; x++) {
             g[0][x] = x*y;
             g[1][x] = x*(y+1);
         }
         for (int x = 0; x < width; x++) {
             f[y][x] = g[0][x] + g[1][x] + g[0][x+1] + g[1][x+1];
         }
     }
     \endcode
     *
     * The allocation and computation of g is within f's loop over y,
     * and enough of g is computed to satisfy all that f will need for
     * that iteration. This does less redundant work (each point in g
     * ends up being evaluated twice), but the locality is not quite
     * as good, and we have to allocate more temporary memory to store
     * g.
     */
    EXPORT Func &compute_at(Func f, Var var);

    /** Schedule a function to be computed within the iteration over
     * some dimension of a reduction domain. Produces equivalent code
     * to the version of compute_at that takes a Var. */
    EXPORT Func &compute_at(Func f, RVar var);

    /** Compute all of this function once ahead of time. Reusing
     * the example in \ref Func::compute_at :
     *
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x, y+1) + g(x+1, y) + g(x+1, y+1);

     g.compute_root();
     \endcode
     *
     * is equivalent to
     *
     \code
     int f[height][width];
     int g[height+1][width+1];
     for (int y = 0; y < height+1; y++) {
         for (int x = 0; x < width+1; x++) {
             g[y][x] = x*y;
         }
     }
     for (int y = 0; y < height; y++) {
         for (int x = 0; x < width; x++) {
             f[y][x] = g[y][x] + g[y+1][x] + g[y][x+1] + g[y+1][x+1];
         }
     }
     \endcode
     *
     * g is computed once ahead of time, and enough is computed to
     * satisfy all uses of it. This does no redundant work (each point
     * in g is evaluated once), but has poor locality (values of g are
     * probably not still in cache when they are used by f), and
     * allocates lots of temporary memory to store g.
     */
    EXPORT Func &compute_root();

    /** Allocate storage for this function within f's loop over
     * var. Scheduling storage is optional, and can be used to
     * separate the loop level at which storage occurs from the loop
     * level at which computation occurs to trade off between locality
     * and redundant work. This can open the door for two types of
     * optimization.
     *
     * Consider again the pipeline from \ref Func::compute_at :
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x+1, y) + g(x, y+1) + g(x+1, y+1);
     \endcode
     *
     * If we schedule it like so:
     *
     \code
     g.compute_at(f, x).store_at(f, y);
     \endcode
     *
     * Then the computation of g takes place within the loop over x,
     * but the storage takes place within the loop over y:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][width+1];
         for (int x = 0; x < width; x++) {
             g[0][x] = x*y;
             g[0][x+1] = (x+1)*y;
             g[1][x] = x*(y+1);
             g[1][x+1] = (x+1)*(y+1);
             f[y][x] = g[0][x] + g[1][x] + g[0][x+1] + g[1][x+1];
         }
     }
     \endcode
     *
     * Provided the for loop over x is serial, halide then
     * automatically performs the following sliding window
     * optimization:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][width+1];
         for (int x = 0; x < width; x++) {
             if (x == 0) {
                 g[0][x] = x*y;
                 g[1][x] = x*(y+1);
             }
             g[0][x+1] = (x+1)*y;
             g[1][x+1] = (x+1)*(y+1);
             f[y][x] = g[0][x] + g[1][x] + g[0][x+1] + g[1][x+1];
         }
     }
     \endcode
     *
     * Two of the assignments to g only need to be done when x is
     * zero. The rest of the time, those sites have already been
     * filled in by a previous iteration. This version has the
     * locality of compute_at(f, x), but allocates more memory and
     * does much less redundant work.
     *
     * Halide then further optimizes this pipeline like so:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][2];
         for (int x = 0; x < width; x++) {
             if (x == 0) {
                 g[0][0] = x*y;
                 g[1][0] = x*(y+1);
             }
             g[0][(x+1)%2] = (x+1)*y;
             g[1][(x+1)%2] = (x+1)*(y+1);
             f[y][x] = g[0][x%2] + g[1][x%2] + g[0][(x+1)%2] + g[1][(x+1)%2];
         }
     }
     \endcode
     *
     * Halide has detected that it's possible to use a circular buffer
     * to represent g, and has reduced all accesses to g modulo 2 in
     * the x dimension. This optimization only triggers if the for
     * loop over x is serial, and if halide can statically determine
     * some power of two large enough to cover the range needed. For
     * powers of two, the modulo operator compiles to more efficient
     * bit-masking. This optimization reduces memory usage, and also
     * improves locality by reusing recently-accessed memory instead
     * of pulling new memory into cache.
     *
     */
    EXPORT Func &store_at(Func f, Var var);

    /** Equivalent to the version of store_at that takes a Var, but
     * schedules storage within the loop over a dimension of a
     * reduction domain */
    EXPORT Func &store_at(Func f, RVar var);

    /** Equivalent to \ref Func::store_at, but schedules storage
     * outside the outermost loop. */
    EXPORT Func &store_root();

    /** Aggressively inline all uses of this function. This is the
     * default schedule, so you're unlikely to need to call this. For
     * a reduction, that means it gets computed as close to the
     * innermost loop as possible.
     *
     * Consider once more the pipeline from \ref Func::compute_at :
     *
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x+1, y) + g(x, y+1) + g(x+1, y+1);
     \endcode
     *
     * Leaving g as inline, this compiles to code equivalent to the following C:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         for (int x = 0; x < width; x++) {
             f[y][x] = x*y + x*(y+1) + (x+1)*y + (x+1)*(y+1);
         }
     }
     \endcode
     */
    EXPORT Func &compute_inline();

    /** Get a handle on an update step of a reduction for the
     * purposes of scheduling it. Only the pure dimensions of the
     * update step can be meaningfully manipulated (see \ref RDom) */
    EXPORT ScheduleHandle update(int idx = 0);

    /** Trace all loads from this Func by emitting calls to
     * halide_trace. If the Func is inlined, this has no
     * effect. */
    EXPORT Func &trace_loads();

    /** Trace all stores to the buffer backing this Func by emitting
     * calls to halide_trace. If the Func is inlined, this call
     * has no effect. */
    EXPORT Func &trace_stores();

    /** Trace all realizations of this Func by emitting calls to
     * halide_trace. */
    EXPORT Func &trace_realizations();

    /** Get a handle on the internal halide function that this Func
     * represents. Useful if you want to do introspection on Halide
     * functions */
    Internal::Function function() const {
        return func;
    }

    /** Get a handle on the output buffer for this Func. Only relevant
     * if this is the output Func in a pipeline. Useful for making
     * static promises about strides, mins, and extents. */
    // @{
    EXPORT OutputImageParam output_buffer() const;
    EXPORT std::vector<OutputImageParam> output_buffers() const;
    // @}

    /** Casting a function to an expression is equivalent to calling
     * the function with zero arguments. Implicit variables will be
     * injected according to the function's dimensionality
     * (see \ref Var::implicit).
     *
     * This lets you write things like:
     *
     \code
     Func f, g;
     Var x;
     g(x) = ...
     f(_) = g * 2;
     \endcode
    */
    operator Expr() const {
        return (*this)(_);
    }

    /** Use a Func as an argument to an external stage. */
    operator ExternFuncArgument() const {
        return ExternFuncArgument(func);
    }

};

 /** JIT-Compile and run enough code to evaluate a Halide
  * expression. This can be thought of as a scalar version of
  * \ref Func::realize */
template<typename T>
  T evaluate(Expr e) {
  assert(e.type() == type_of<T>() && "Type of argument to evaluate does not match templated type\n");
  Func f;
  f(_) = e;
  Image<T> im = f.realize();
  return im(0);
 }

/** JIT-compile and run enough code to evaluate a Halide Tuple. */
// @{
 template<typename A, typename B>
   void evaluate(Tuple t, A *a, B *b) {
   assert(t[0].type() == type_of<A>() && "Type of argument to evaluate does not match templated type\n");
   assert(t[1].type() == type_of<B>() && "Type of argument to evaluate does not match templated type\n");
   Func f;
   f(_) = t;
   Realization r = f.realize();
   *a = Image<A>(r[0])(0);
   *b = Image<B>(r[1])(0);
 }

 template<typename A, typename B, typename C>
   void evaluate(Tuple t, A *a, B *b, C *c) {
   assert(t[0].type() == type_of<A>() && "Type of argument to evaluate does not match templated type\n");
   assert(t[1].type() == type_of<B>() && "Type of argument to evaluate does not match templated type\n");
   assert(t[2].type() == type_of<C>() && "Type of argument to evaluate does not match templated type\n");
   Func f;
   f(_) = t;
   Realization r = f.realize();
   *a = Image<A>(r[0])(0);
   *b = Image<B>(r[1])(0);
   *c = Image<C>(r[2])(0);
 }

 template<typename A, typename B, typename C, typename D>
   void evaluate(Tuple t, A *a, B *b, C *c, D *d) {
   assert(t[0].type() == type_of<A>() && "Type of argument to evaluate does not match templated type\n");
   assert(t[1].type() == type_of<B>() && "Type of argument to evaluate does not match templated type\n");
   assert(t[2].type() == type_of<C>() && "Type of argument to evaluate does not match templated type\n");
   assert(t[3].type() == type_of<D>() && "Type of argument to evaluate does not match templated type\n");
   Func f;
   f(_) = t;
   Realization r = f.realize();
   *a = Image<A>(r[0])(0);
   *b = Image<B>(r[1])(0);
   *c = Image<C>(r[2])(0);
   *d = Image<D>(r[3])(0);
 }
 // @}


/** JIT-Compile and run enough code to evaluate a Halide
 * expression. This can be thought of as a scalar version of \ref
 * Func::realize . Can use GPU if target from environment specifies
 * one.*/
template<typename T>
T evaluate_may_gpu(Expr e) {
    assert(e.type() == type_of<T>() && "Type of argument to evaluate does not match templated type\n");
    Image<T> im;
    Func f;
    f(_) = e;

    if (get_target_from_environment().has_gpu_feature()) {
        Func g;
	Var x;
	g(x, _) = f(_);
	g.gpu_tile(x, 1);
	im = g.realize(1);
	im.copy_to_host();
    } else {
	im = f.realize();
    }
    return im(0);
}

/** JIT-compile and run enough code to evaluate a Halide Tuple. Can
 *  use GPU if target from environment specifies one. */
// @{
template<typename A, typename B>
void evaluate_may_gpu(Tuple t, A *a, B *b) {
    assert(t[0].type() == type_of<A>() && "Type of argument to evaluate does not match templated type\n");
    assert(t[1].type() == type_of<B>() && "Type of argument to evaluate does not match templated type\n");

    bool has_gpu_feature = get_target_from_environment().has_gpu_feature();
    Func f;
    f(_) = t;
    Image<A> result_a(has_gpu_feature ? 1 : 0);
    Image<B> result_b(has_gpu_feature ? 1 : 0);
    Realization r(result_a, result_b);
    if (has_gpu_feature) {
        Func g;
	Var x;
	g(x, _) = f(_);
	g.gpu_tile(x, 1);
	g.realize(r);
	result_a.copy_to_host();
	result_b.copy_to_host();
    } else {
	f.realize(r);
    }
    *a = result_a(0);
    *b = result_b(0);
}

template<typename A, typename B, typename C>
void evaluate_may_gpu(Tuple t, A *a, B *b, C *c) {
    assert(t[0].type() == type_of<A>() && "Type of argument to evaluate does not match templated type\n");
    assert(t[1].type() == type_of<B>() && "Type of argument to evaluate does not match templated type\n");
    assert(t[2].type() == type_of<C>() && "Type of argument to evaluate does not match templated type\n");

    bool has_gpu_feature = get_target_from_environment().has_gpu_feature();
    Func f;
    f(_) = t;
    Image<A> result_a(has_gpu_feature ? 1 : 0);
    Image<B> result_b(has_gpu_feature ? 1 : 0);
    Image<C> result_c(has_gpu_feature ? 1 : 0);
    Realization r(result_a, result_b, result_c);
    if (has_gpu_feature) {
        Func g;
	Var x;
	g(x, _) = f(_);
	g.gpu_tile(x, 1);
	g.realize(r);
	result_a.copy_to_host();
	result_b.copy_to_host();
	result_c.copy_to_host();
    } else {
	f.realize(r);
    }
    *a = result_a(0);
    *b = result_b(0);
    *c = result_c(0);
}

template<typename A, typename B, typename C, typename D>
void evaluate_may_gpu(Tuple t, A *a, B *b, C *c, D *d) {
    assert(t[0].type() == type_of<A>() && "Type of argument to evaluate does not match templated type\n");
    assert(t[1].type() == type_of<B>() && "Type of argument to evaluate does not match templated type\n");
    assert(t[2].type() == type_of<C>() && "Type of argument to evaluate does not match templated type\n");
    assert(t[3].type() == type_of<D>() && "Type of argument to evaluate does not match templated type\n");

    bool has_gpu_feature = get_target_from_environment().has_gpu_feature();
    Func f;
    f(_) = t;
    Image<A> result_a(has_gpu_feature ? 1 : 0);
    Image<B> result_b(has_gpu_feature ? 1 : 0);
    Image<C> result_c(has_gpu_feature ? 1 : 0);
    Image<D> result_d(has_gpu_feature ? 1 : 0);
    Realization r(result_a, result_b, result_c, result_d);
    if (has_gpu_feature) {
        Func g;
	Var x;
	g(x, _) = f(_);
	g.gpu_tile(x, 1);
	g.realize(r);
	result_a.copy_to_host();
	result_b.copy_to_host();
	result_c.copy_to_host();
	result_d.copy_to_host();
    } else {
	f.realize(r);
    }
    *a = result_a(0);
    *b = result_b(0);
    *c = result_c(0);
    *d = result_d(0);
}
// @}

}


#endif
