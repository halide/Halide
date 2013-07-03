#ifndef HALIDE_FUNC_H
#define HALIDE_FUNC_H

/** \file
 *
 * Defines Func - the front-end handle on a halide function, and related classes.
 */

#include "IR.h"
#include "Var.h"
#include "IntrusivePtr.h"
#include "Function.h"
#include "Param.h"
#include "Argument.h"
#include "RDom.h"
#include "JITCompiledModule.h"
#include "Image.h"
#include "Util.h"

namespace Halide {

/** A fragment of front-end syntax of the form f(x, y, z), where x,
 * y, z are Vars. It could be the left-hand side of a function
 * definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class FuncRefVar {
    Internal::Function func;
    std::vector<std::string> args;
    void add_implicit_vars(std::vector<std::string> &args, Expr e) const;
public:
    FuncRefVar(Internal::Function, const std::vector<Var> &);

    /**  Use this as the left-hand-side of a definition */
    EXPORT void operator=(Expr);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
     */
    EXPORT void operator+=(Expr);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
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
    void operator=(const FuncRefVar &e) {*this = Expr(e);}

    /** Use this FuncRefVar as a call to the function, and not as the
     * left-hand-side of a definition.
     */
    EXPORT operator Expr() const;
};

/** A fragment of front-end syntax of the form f(x, y, z), where x, y,
 * z are Exprs. If could be the left hand side of a reduction
 * definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class FuncRefExpr {
    Internal::Function func;
    std::vector<Expr> args;
    void add_implicit_vars(std::vector<Expr> &args, Expr e) const;
public:
    FuncRefExpr(Internal::Function, const std::vector<Expr> &);
    FuncRefExpr(Internal::Function, const std::vector<std::string> &);

    /** Use this as the left-hand-side of a reduction definition (see
     * \ref RDom). The function must already have a pure definition.
     */
    EXPORT void operator=(Expr);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
     */
    EXPORT void operator+=(Expr);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
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
    void operator=(const FuncRefExpr &e) {*this = Expr(e);}

    /** Use this as a call to the function, and not the left-hand-side
     * of a definition. */
    EXPORT operator Expr() const;
};

/** A wrapper around a schedule used for common schedule manipulations */
class ScheduleHandle {
    Internal::Schedule &schedule;
    void set_dim_type(Var var, Internal::For::ForType t);
    void dump_argument_list();
public:
    ScheduleHandle(Internal::Schedule &s) : schedule(s) {}

    /** Split a dimension into inner and outer subdimensions with the
     * given names, where the inner dimension iterates from 0 to
     * factor-1. The inner and outer subdimensions can then be dealt
     * with using the other scheduling calls. It's ok to reuse the old
     * variable name as either the inner or outer variable. */
    EXPORT ScheduleHandle &split(Var old, Var outer, Var inner, Expr factor);

    /** Mark a dimension to be traversed in parallel */
    EXPORT ScheduleHandle &parallel(Var var);

    /** Mark a dimension to be computed all-at-once as a single
     * vector. The dimension should have constant extent -
     * e.g. because it is the inner dimension following a split by a
     * constant factor. For most uses of vectorize you want the two
     * argument form. The variable to be vectorized should be the
     * innermost one. */
    EXPORT ScheduleHandle &vectorize(Var var);

    /** Mark a dimension to be completely unrolled. The dimension
     * should have constant extent - e.g. because it is the inner
     * dimension following a split by a constant factor. For most uses
     * of unroll you want the two-argument form. */
    EXPORT ScheduleHandle &unroll(Var var);

    /** Split a dimension by the given factor, then vectorize the
     * inner dimension. This is how you vectorize a loop of unknown
     * size. The variable to be vectorized should be the innermost
     * one. After this call, var refers to the outer dimension of the
     * split. */
    EXPORT ScheduleHandle &vectorize(Var var, int factor);

    /** Split a dimension by the given factor, then unroll the inner
     * dimension. This is how you unroll a loop of unknown size by
     * some constant factor. After this call, var refers to the outer
     * dimension of the split. */
    EXPORT ScheduleHandle &unroll(Var var, int factor);

    /** Statically declare that the range over which a function should
     * be evaluated is given by the second and third arguments. This
     * can let Halide perform some optimizations. E.g. if you know
     * there are going to be 4 color channels, you can completely
     * vectorize the color channel dimension without the overhead of
     * splitting it up. If bounds inference decides that it requires
     * more of this function than the bounds you have stated, a
     * runtime error will occur when you try to run your pipeline. */
    EXPORT ScheduleHandle &bound(Var var, Expr min, Expr extent);

    /** Split two dimensions at once by the given factors, and then
     * reorder the resulting dimensions to be xi, yi, xo, yo from
     * innermost outwards. This gives a tiled traversal. */
    EXPORT ScheduleHandle &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);

    /** A shorter form of tile, which reuses the old variable names as
     * the new outer dimensions */
    EXPORT ScheduleHandle &tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor);

    /** Reorder two dimensions so that x is traversed inside y. Does
     * not affect the nesting order of other dimensions. E.g, if you
     * say foo(x, y, z, w) = bar; foo.reorder(w, x); then foo will be
     * traversed in the order (w, y, z, x), from innermost
     * outwards. */
    EXPORT ScheduleHandle &reorder(Var x, Var y);

    /** Reorder three dimensions to have the given nesting order, from
     * innermost out */
    EXPORT ScheduleHandle &reorder(Var x, Var y, Var z);

    /** Reorder four dimensions to have the given nesting order, from
     * innermost out */
    EXPORT ScheduleHandle &reorder(Var x, Var y, Var z, Var w);

    /** Reorder five dimensions to have the given nesting order, from
     * innermost out */
    EXPORT ScheduleHandle &reorder(Var x, Var y, Var z, Var w, Var t);

    /** Rename a dimension. Equivalent to split with a inner size of one. */
    EXPORT ScheduleHandle &rename(Var old_name, Var new_name);

    /** Tell Halide that the following dimensions correspond to cuda
     * thread indices. This is useful if you compute a producer
     * function within the block indices of a consumer function, and
     * want to control how that function's dimensions map to cuda
     * threads. If the selected target is not ptx, this just marks
     * those dimensions as parallel. */
    // @{
    EXPORT ScheduleHandle &cuda_threads(Var thread_x);
    EXPORT ScheduleHandle &cuda_threads(Var thread_x, Var thread_y);
    EXPORT ScheduleHandle &cuda_threads(Var thread_x, Var thread_y, Var thread_z);
    // @}

    /** Tell Halide that the following dimensions correspond to cuda
     * block indices. This is useful for scheduling stages that will
     * run serially within each cuda block. If the selected target is
     * not ptx, this just marks those dimensions as parallel. */
    // @{
    EXPORT ScheduleHandle &cuda_blocks(Var block_x);
    EXPORT ScheduleHandle &cuda_blocks(Var block_x, Var block_y);
    EXPORT ScheduleHandle &cuda_blocks(Var block_x, Var block_y, Var block_z);
    // @}

    /** Tell Halide that the following dimensions correspond to cuda
     * block indices and thread indices. If the selected target is not
     * ptx, these just mark the given dimensions as parallel. The
     * dimensions are consumed by this call, so do all other
     * unrolling, reordering, etc first. */
    // @{
    EXPORT ScheduleHandle &cuda(Var block_x, Var thread_x);
    EXPORT ScheduleHandle &cuda(Var block_x, Var block_y,
                                Var thread_x, Var thread_y);
    EXPORT ScheduleHandle &cuda(Var block_x, Var block_y, Var block_z,
                                Var thread_x, Var thread_y, Var thread_z);
    // @}

    /** Short-hand for tiling a domain and mapping the tile indices
     * to cuda block indices and the coordinates within each tile to
     * cuda thread indices. Consumes the variables given, so do all
     * other scheduling first. */
    // @{
    EXPORT ScheduleHandle &cuda_tile(Var x, int x_size);
    EXPORT ScheduleHandle &cuda_tile(Var x, Var y, int x_size, int y_size);
    EXPORT ScheduleHandle &cuda_tile(Var x, Var y, Var z,
                                     int x_size, int y_size, int z_size);
    // @}

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
    void add_implicit_vars(std::vector<Var> &) const;
    void add_implicit_vars(std::vector<Expr> &) const;
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
    void (*error_handler)(char *);

    /** The current custom allocator used for realizing this
     * function. May be NULL. Only relevant when jitting. */
    // @{
    void *(*custom_malloc)(size_t);
    void (*custom_free)(void *);
    // @}

    /** The current custom parallel task launcher and handler for
     * realizing this function. May be NULL. */
    // @{
    void (*custom_do_par_for)(void (*)(int, uint8_t *), int, int, uint8_t *);
    void (*custom_do_task)(void (*)(int, uint8_t *), int, uint8_t *);
    // @}

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
    static void test();

    /** Declare a new undefined function with the given name */
    EXPORT explicit Func(const std::string &name);

    /** Declare a new undefined function with an
     * automatically-generated unique name */
    EXPORT Func();

    /** Declare a new function with an automatically-generated unique
     * name, and define it to return the given expression (which may
     * not contain free variables). */
    EXPORT explicit Func(Expr e);

    /** Generate a new uniquely-named function that returns the given
     * buffer. Has the same dimensionality as the buffer. Useful for
     * passing Images to c++ functions that expect Funcs */
    //@{
    //EXPORT Func(Buffer image);
    /*
    template<typename T> Func(Image<T> image) {
        (*this) = Func(Buffer(image));
    }
    */
    //@}

    /** Evaluate this function over some rectangular domain and return
     * the resulting buffer. The buffer should probably be instantly
     * wrapped in an Image class of the appropriate type. That is, do
     * this:
     *
     * Image<float> im = f.realize(...);
     *
     * not this:
     *
     * Buffer im = f.realize(...)
     *
     */
    EXPORT Buffer realize(int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0);

    /** Evaluate this function into an existing allocated buffer. If
     * the buffer is also one of the arguments to the function,
     * strange things may happen, as the pipeline isn't necessarily
     * safe to run in-place. */
    EXPORT void realize(Buffer dst);

    /** For a given size of output, or a given output buffer,
     * determine the bounds required of all unbound ImageParams
     * referenced. Communicates the result by allocating new buffers
     * of the appropriate size and binding them to the unbound
     * ImageParams. */
    // @{
    EXPORT void infer_input_bounds(int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0);
    EXPORT void infer_input_bounds(Buffer dst);
    // @}

    /** Statically compile this function to llvm bitcode, with the
     * given filename (which should probably end in .bc), type
     * signature, and C function name (which defaults to the same name
     * as this halide function */
    EXPORT void compile_to_bitcode(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");

    /** Statically compile this function to an object file, with the
     * given filename (which should probably end in .o or .obj), type
     * signature, and C function name (which defaults to the same name
     * as this halide function. You probably don't want to use this directly - instead call compile_to_file.  */
    EXPORT void compile_to_object(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");

    /** Emit a header file with the given filename for this
     * function. The header will define a function with the type
     * signature given by the second argument, and a name given by the
     * third. The name defaults to the same name as this halide
     * function. You don't actually have to have defined this function
     * yet to call this. You probably don't want to use this directly
     * - instead call compile_to_file. */
    EXPORT void compile_to_header(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");

    /** Statically compile this function to text assembly equivalent
     * to the object file generated by compile_to_object. This is
     * useful for checking what Halide is producing without having to
     * disassemble anything, or if you need to feed the assembly into
     * some custom toolchain to produce an object file (e.g. iOS) */
    EXPORT void compile_to_assembly(const std::string &filename, std::vector<Argument>, const std::string &fn_name = "");
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
    EXPORT void compile_to_file(const std::string &filename_prefix, std::vector<Argument> args);
    EXPORT void compile_to_file(const std::string &filename_prefix);
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a);
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b);
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c);
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c, Argument d);
    EXPORT void compile_to_file(const std::string &filename_prefix, Argument a, Argument b, Argument c, Argument d, Argument e);
    // @}

    /** Eagerly jit compile the function to machine code. This
     * normally happens on the first call to realize. If you're
     * running your halide pipeline inside time-sensitive code and
     * wish to avoid including the time taken to compile a pipeline,
     * then you can call this ahead of time. Returns the raw function
     * pointer to the compiled pipeline. */
    EXPORT void *compile_jit();

    /** Set the error handler function that be called in the case of
     * runtime errors during halide pipelines. If you are compiling
     * statically, you can also just define your own function with
     * signature
     \code
     extern "C" halide_error(char *)
     \endcode
     * This will clobber Halide's version.
     */
    EXPORT void set_error_handler(void (*handler)(char *));

    /** Set a custom malloc and free for halide to use. Malloc should
     * return 32-byte aligned chunks of memory, with 32-bytes extra
     * allocated on the start and end so that vector loads can spill
     * off the end slightly. Metadata (e.g. the base address of the
     * region allocated) can go in this margin - it is only read, not
     * written. If you are compiling statically, you can also just
     * define your own functions with signatures
     \code
     extern "C" void *malloc(size_t)
     extern "C" void halide_free(void *)
     \endcode
     * These will clobber Halide's versions.
     */
    EXPORT void set_custom_allocator(void *(*malloc)(size_t), void (*free)(void *));

    /** Set a custom task handler to be called by the parallel for
     * loop. It is useful to set this if you want to do some
     * additional bookkeeping at the granularity of parallel
     * tasks. The default implementation does this:
     \code
     extern "C" void halide_do_task(void (*f)(int, uint8_t *), int idx, uint8_t *state) {
         f(idx, state);
     }
     \endcode
     * If you are statically compiling, you can also just define your
     * own version of the above function, and it will clobber Halide's
     * version.
     *
     * If you're trying to use a custom parallel runtime, you probably
     * don't want to call this. See instead \ref Func::set_custom_do_par_for .
    */
    EXPORT void set_custom_do_task(void (*custom_do_task)(void (*)(int, uint8_t *), int, uint8_t *));

    /** Set a custom parallel for loop launcher. Useful if your app
     * already manages a thread pool. The default implementation is
     * equivalent to this:
     \code
     extern "C" void halide_do_par_for(void (*f)(int uint8_t *), int min, int extent, uint8_t *state) {
         parallel for (int idx = min; idx < min+extent; idx++) {
             halide_do_task(f, idx, state);
         }
     }
     \endcode
     * If you are statically compiling, you can also just define your
     * own version of the above function, and it will clobber Halide's
     * version.
     */
    EXPORT void set_custom_do_par_for(void (*custom_do_par_for)(void (*)(int, uint8_t *), int, int, uint8_t *));

    /** When this function is compiled, include code that dumps its values
     * to a file after it is realized, for the purpose of debugging.
     * The file covers the realized extent at the point in the schedule that
     * debug_to_file appears.
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
     * function. May be undefined if the function has no pure
     * definition yet. */
    EXPORT Expr value() const;

    /** Get the left-hand-side of the reduction definition. An empty
     * vector if there's no reduction definition. */
    EXPORT const std::vector<Expr> &reduction_args() const;

    /** Get the right-hand-side of the reduction definition. Returns
     * undefined Expr if there's no reduction definition. */
    EXPORT Expr reduction_value() const;

    /** Get the reduction domain for the reduction definition. Returns
     * an undefined RDom if there's no reduction definition. */
    EXPORT RDom reduction_domain() const;

    /** Is this function a reduction? */
    EXPORT bool is_reduction() const;

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

    /** Scheduling calls that control how the domain of this function
     * is traversed. See the documentation for ScheduleHandle for the
     * meanings. */
    // @{
    EXPORT Func &split(Var old, Var outer, Var inner, Expr factor);
    EXPORT Func &parallel(Var var);
    EXPORT Func &vectorize(Var var);
    EXPORT Func &unroll(Var var);
    EXPORT Func &vectorize(Var var, int factor);
    EXPORT Func &unroll(Var var, int factor);
    EXPORT Func &bound(Var var, Expr min, Expr extent);
    EXPORT Func &tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor);
    EXPORT Func &tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor);
    EXPORT Func &reorder(Var x, Var y);
    EXPORT Func &reorder(Var x, Var y, Var z);
    EXPORT Func &reorder(Var x, Var y, Var z, Var w);
    EXPORT Func &reorder(Var x, Var y, Var z, Var w, Var t);
    EXPORT Func &rename(Var old_name, Var new_name);
    EXPORT Func &cuda_threads(Var thread_x);
    EXPORT Func &cuda_threads(Var thread_x, Var thread_y);
    EXPORT Func &cuda_threads(Var thread_x, Var thread_y, Var thread_z);
    EXPORT Func &cuda_blocks(Var block_x);
    EXPORT Func &cuda_blocks(Var block_x, Var block_y);
    EXPORT Func &cuda_blocks(Var block_x, Var block_y, Var block_z);
    EXPORT Func &cuda(Var block_x, Var thread_x);
    EXPORT Func &cuda(Var block_x, Var block_y,
                      Var thread_x, Var thread_y);
    EXPORT Func &cuda(Var block_x, Var block_y, Var block_z,
                      Var thread_x, Var thread_y, Var thread_z);
    EXPORT Func &cuda_tile(Var x, int x_size);
    EXPORT Func &cuda_tile(Var x, Var y,
                           int x_size, int y_size);
    EXPORT Func &cuda_tile(Var x, Var y, Var z,
                           int x_size, int y_size, int z_size);
    // @}

    /** Scheduling calls that control how the storage for the function
     * is laid out. Right now you can only reorder the dimensions. */
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

    /** Get a handle on the update step of a reduction for the
     * purposes of scheduling it. Only the pure dimensions of the
     * update step can be meaningfully manipulated (see \ref RDom) */
    EXPORT ScheduleHandle update();

    /** Get a handle on the internal halide function that this Func
     * represents. Useful if you want to do introspection on Halide
     * functions */
    Internal::Function function() const {
        return func;
    }

    /** Get a handle on the output buffer for this Func. Only relevant
     * if this is the output Func in a pipeline. Useful for making
     * static promises about strides, mins, and extents. */
    OutputImageParam output_buffer() const;

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
     f() = g * 2;
     \endcode
    */
    operator Expr() const {
        return (*this)();
    }

    /* These operators are being removed because they break putting
     * Funcs in STL containers, the python bindings, and have
     * generally caused confusion.

    ** Define a function to take a number of arguments according to
     * the implicit variables present in the given expression, and
     * return the given expression. The expression may not have free
     * variables. *
    void operator=(Expr e) {
        (*this)() = e;
    }

    ** Define a function to simply call another function. Note that
     * this is not equivalent to the standard c++ operator=. We opt
     * instead for consistency with Halide function definition, of
     * which this is a degenerate case. *
    void operator=(const Func &f) {
        (*this)() = f();
    }

    */
};


}

#endif
