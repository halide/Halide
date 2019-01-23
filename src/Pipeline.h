#ifndef HALIDE_PIPELINE_H
#define HALIDE_PIPELINE_H

/** \file
 *
 * Defines the front-end class representing an entire Halide imaging
 * pipeline.
 */

#include <vector>

#include "AutoSchedule.h"
#include "ExternalCode.h"
#include "IntrusivePtr.h"
#include "JITModule.h"
#include "Module.h"
#include "ParamMap.h"
#include "Target.h"
#include "Tuple.h"

namespace Halide {

struct Argument;
class Func;
struct Outputs;
struct PipelineContents;

namespace Internal {
class IRMutator2;
}  // namespace Internal

/**
 * Used to determine if the output printed to file should be as a normal string
 * or as an HTML file which can be opened in a browerser and manipulated via JS and CSS.*/
enum StmtOutputFormat {
    Text,
    HTML
};

namespace {
// Helper for deleting custom lowering passes. In the header so that
// it goes in user code on windows, where you can have multiple heaps.
template<typename T>
void delete_lowering_pass(T *pass) {
    delete pass;
}
}  // namespace

/** A custom lowering pass. See Pipeline::add_custom_lowering_pass. */
struct CustomLoweringPass {
    Internal::IRMutator2 *pass;
    std::function<void()> deleter;
};

struct JITExtern;

/** A class representing a Halide pipeline. Constructed from the Func
 * or Funcs that it outputs. */
class Pipeline {
public:
    struct RealizationArg {
        // Only one of the following may be non-null
        Realization *r{nullptr};
        halide_buffer_t *buf{nullptr};
        std::unique_ptr<std::vector<Buffer<>>> buffer_list;

        RealizationArg(Realization &r) : r(&r) { }
        RealizationArg(Realization &&r) : r(&r) { }
        RealizationArg(halide_buffer_t *buf) : buf(buf) { }
        template<typename T, int D>
        RealizationArg(Runtime::Buffer<T, D> &dst) : buf(dst.raw_buffer()) { }
        template <typename T>
        HALIDE_NO_USER_CODE_INLINE RealizationArg(Buffer<T> &dst) : buf(dst.raw_buffer()) { }
        template<typename T, typename ...Args,
                 typename = typename std::enable_if<Internal::all_are_convertible<Buffer<>, Args...>::value>::type>
            RealizationArg(Buffer<T> &a, Args&&... args) {
            buffer_list.reset(new std::vector<Buffer<>>({a, args...}));
        }
        RealizationArg(RealizationArg &&from) = default;

        size_t size() {
            if (r != nullptr) {
                return r->size();
            } else if (buffer_list) {
                return buffer_list->size();
            }
            return 1;
        }
    };

    Internal::IntrusivePtr<PipelineContents> contents;

    std::vector<Argument> infer_arguments(Internal::Stmt body);

    struct JITCallArgs; // Opaque structure to optimize away dynamic allocation in this path.

    // For the three method below, precisely one of the first two args should be non-null
    void prepare_jit_call_arguments(RealizationArg &output, const Target &target, const ParamMap &param_map,
                                    void *user_context, bool is_bounds_inference, JITCallArgs &args_result);

    static std::vector<Internal::JITModule> make_externs_jit_module(const Target &target,
                                                                    std::map<std::string, JITExtern> &externs_in_out);

    static std::function<std::string(Pipeline, const Target &, const MachineParams &)> custom_auto_scheduler;

 public:
    /** Make an undefined Pipeline object. */
    Pipeline();

    /** Make a pipeline that computes the given Func. Schedules the
     * Func compute_root(). */
    Pipeline(Func output);

    /** Make a pipeline that computes the givens Funcs as
     * outputs. Schedules the Funcs compute_root(). */
    Pipeline(const std::vector<Func> &outputs);

    /** Get the Funcs this pipeline outputs. */
    std::vector<Func> outputs() const;

    /** Generate a schedule for the pipeline. */
    //@{
    std::string auto_schedule(const Target &target,
                              const MachineParams &arch_params = MachineParams::generic());
    //@}

    /** Globally set the autoscheduler method to use whenever
     * autoscheduling any Pipeline. Uses the built-in autoscheduler if
     * passed nullptr. */
    static void set_custom_auto_scheduler(std::function<std::string(Pipeline, const Target &, const MachineParams &)> auto_scheduler);

    /** Return handle to the index-th Func within the pipeline based on the
     * topological order. */
    Func get_func(size_t index);

    /** Compile and generate multiple target files with single call.
     * Deduces target files based on filenames specified in
     * output_files struct.
     */
    void compile_to(const Outputs &output_files,
                    const std::vector<Argument> &args,
                    const std::string &fn_name,
                    const Target &target);

    /** Statically compile a pipeline to llvm bitcode, with the given
     * filename (which should probably end in .bc), type signature,
     * and C function name. If you're compiling a pipeline with a
     * single output Func, see also Func::compile_to_bitcode. */
    void compile_to_bitcode(const std::string &filename,
                            const std::vector<Argument> &args,
                            const std::string &fn_name,
                            const Target &target = get_target_from_environment());

    /** Statically compile a pipeline to llvm assembly, with the given
     * filename (which should probably end in .ll), type signature,
     * and C function name. If you're compiling a pipeline with a
     * single output Func, see also Func::compile_to_llvm_assembly. */
    void compile_to_llvm_assembly(const std::string &filename,
                                  const std::vector<Argument> &args,
                                  const std::string &fn_name,
                                  const Target &target = get_target_from_environment());

    /** Statically compile a pipeline with multiple output functions to an
     * object file, with the given filename (which should probably end in
     * .o or .obj), type signature, and C function name (which defaults to
     * the same name as this halide function. You probably don't want to
     * use this directly; call compile_to_static_library or compile_to_file instead. */
    void compile_to_object(const std::string &filename,
                           const std::vector<Argument> &,
                           const std::string &fn_name,
                           const Target &target = get_target_from_environment());

    /** Emit a header file with the given filename for a pipeline. The
     * header will define a function with the type signature given by
     * the second argument, and a name given by the third. You don't
     * actually have to have defined any of these functions yet to
     * call this. You probably don't want to use this directly; call
     * compile_to_static_library or compile_to_file instead. */
    void compile_to_header(const std::string &filename,
                           const std::vector<Argument> &,
                           const std::string &fn_name,
                           const Target &target = get_target_from_environment());

    /** Statically compile a pipeline to text assembly equivalent to
     * the object file generated by compile_to_object. This is useful
     * for checking what Halide is producing without having to
     * disassemble anything, or if you need to feed the assembly into
     * some custom toolchain to produce an object file. */
    void compile_to_assembly(const std::string &filename,
                             const std::vector<Argument> &args,
                             const std::string &fn_name,
                             const Target &target = get_target_from_environment());

    /** Statically compile a pipeline to C source code. This is useful
     * for providing fallback code paths that will compile on many
     * platforms. Vectorization will fail, and parallelization will
     * produce serial code. */
    void compile_to_c(const std::string &filename,
                      const std::vector<Argument> &,
                      const std::string &fn_name,
                      const Target &target = get_target_from_environment());

    /** Emit a Python extension glue .c file. */
    void compile_to_python_extension(const std::string &filename,
                                     const std::vector<Argument> &args,
                                     const std::string &fn_name,
                                     const Target &target = get_target_from_environment());

    /** Write out an internal representation of lowered code. Useful
     * for analyzing and debugging scheduling. Can emit html or plain
     * text. */
    void compile_to_lowered_stmt(const std::string &filename,
                                 const std::vector<Argument> &args,
                                 StmtOutputFormat fmt = Text,
                                 const Target &target = get_target_from_environment());

    /** Write out the loop nests specified by the schedule for this
     * Pipeline's Funcs. Helpful for understanding what a schedule is
     * doing. */
    void print_loop_nest();

    /** Compile to object file and header pair, with the given
     * arguments. */
    void compile_to_file(const std::string &filename_prefix,
                         const std::vector<Argument> &args,
                         const std::string &fn_name,
                         const Target &target = get_target_from_environment());

    /** Compile to static-library file and header pair, with the given
     * arguments. */
    void compile_to_static_library(const std::string &filename_prefix,
                                   const std::vector<Argument> &args,
                                   const std::string &fn_name,
                                   const Target &target = get_target_from_environment());

    /** Compile to static-library file and header pair once for each target;
     * each resulting function will be considered (in order) via halide_can_use_target_features()
     * at runtime, with the first appropriate match being selected for subsequent use.
     * This is typically useful for specializations that may vary unpredictably by machine
     * (e.g., SSE4.1/AVX/AVX2 on x86 desktop machines).
     * All targets must have identical arch-os-bits.
     */
    void compile_to_multitarget_static_library(const std::string &filename_prefix,
                                               const std::vector<Argument> &args,
                                               const std::vector<Target> &targets);

    /** Create an internal representation of lowered code as a self
     * contained Module suitable for further compilation. */
    Module compile_to_module(const std::vector<Argument> &args,
                             const std::string &fn_name,
                             const Target &target = get_target_from_environment(),
                             const LinkageType linkage_type = LinkageType::ExternalPlusMetadata);

   /** Eagerly jit compile the function to machine code. This
     * normally happens on the first call to realize. If you're
     * running your halide pipeline inside time-sensitive code and
     * wish to avoid including the time taken to compile a pipeline,
     * then you can call this ahead of time. Returns the raw function
     * pointer to the compiled pipeline. Default is to use the Target
     * returned from Halide::get_jit_target_from_environment()
     */
     void *compile_jit(const Target &target = get_jit_target_from_environment());

    /** Set the error handler function that be called in the case of
     * runtime errors during halide pipelines. If you are compiling
     * statically, you can also just define your own function with
     * signature
     \code
     extern "C" void halide_error(void *user_context, const char *);
     \endcode
     * This will clobber Halide's version.
     */
    void set_error_handler(void (*handler)(void *, const char *));

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
    void set_custom_allocator(void *(*malloc)(void *, size_t),
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
    void set_custom_do_task(
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
    void set_custom_do_par_for(
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
    void set_custom_trace(int (*trace_fn)(void *, const halide_trace_event_t *));

    /** Set the function called to print messages from the runtime.
     * If you are compiling statically, you can also just define your
     * own function with signature
     \code
     extern "C" void halide_print(void *user_context, const char *);
     \endcode
     * This will clobber Halide's version.
     */
    void set_custom_print(void (*handler)(void *, const char *));

    /** Install a set of external C functions or Funcs to satisfy
     * dependencies introduced by HalideExtern and define_extern
     * mechanisms. These will be used by calls to realize,
     * infer_bounds, and compile_jit. */
    void set_jit_externs(const std::map<std::string, JITExtern> &externs);

    /** Return the map of previously installed externs. Is an empty
     * map unless set otherwise. */
    const std::map<std::string, JITExtern> &get_jit_externs();

    /** Get a struct containing the currently set custom functions
     * used by JIT. */
    const Internal::JITHandlers &jit_handlers();

    /** Add a custom pass to be used during lowering. It is run after
     * all other lowering passes. Can be used to verify properties of
     * the lowered Stmt, instrument it with extra code, or otherwise
     * modify it. The Func takes ownership of the pass, and will call
     * delete on it when the Func goes out of scope. So don't pass a
     * stack object, or share pass instances between multiple
     * Funcs. */
    template<typename T>
    void add_custom_lowering_pass(T *pass) {
        // Template instantiate a custom deleter for this type, then
        // wrap in a lambda. The custom deleter lives in user code, so
        // that deletion is on the same heap as construction (I hate Windows).
        add_custom_lowering_pass(pass, [pass]() { delete_lowering_pass<T>(pass); });
    }

    /** Add a custom pass to be used during lowering, with the
     * function that will be called to delete it also passed in. Set
     * it to nullptr if you wish to retain ownership of the object. */
    void add_custom_lowering_pass(Internal::IRMutator2 *pass, std::function<void()> deleter);

    /** Remove all previously-set custom lowering passes */
    void clear_custom_lowering_passes();

    /** Get the custom lowering passes. */
    const std::vector<CustomLoweringPass> &custom_lowering_passes();

    /** See Func::realize */
    // @{
    Realization realize(std::vector<int32_t> sizes, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());
    Realization realize(int x_size, int y_size, int z_size, int w_size, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());
    Realization realize(int x_size, int y_size, int z_size, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());
    Realization realize(int x_size, int y_size, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());
    Realization realize(int x_size, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());
    Realization realize(const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());
    // @}

    /** Evaluate this Pipeline into an existing allocated buffer or
     * buffers. If the buffer is also one of the arguments to the
     * function, strange things may happen, as the pipeline isn't
     * necessarily safe to run in-place. The realization should
     * contain one Buffer per tuple component per output Func. For
     * each individual output Func, all Buffers must have the same
     * shape, but the shape can vary across the different output
     * Funcs. This form of realize does *not* automatically copy data
     * back from the GPU. */
    void realize(RealizationArg output, const Target &target = Target(),
                 const ParamMap &param_map = ParamMap::empty_map());

    /** For a given size of output, or a given set of output buffers,
     * determine the bounds required of all unbound ImageParams
     * referenced. Communicates the result by allocating new buffers
     * of the appropriate size and binding them to the unbound
     * ImageParams. */
    // @{
    void infer_input_bounds(int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0,
                            const ParamMap &param_map = ParamMap::empty_map());
    void infer_input_bounds(RealizationArg output,
                            const ParamMap &param_map = ParamMap::empty_map());
    // @}

    /** Infer the arguments to the Pipeline, sorted into a canonical order:
     * all buffers (sorted alphabetically by name), followed by all non-buffers
     * (sorted alphabetically by name).
     This lets you write things like:
     \code
     pipeline.compile_to_assembly("/dev/stdout", pipeline.infer_arguments());
     \endcode
     */
    std::vector<Argument> infer_arguments();

    /** Check if this pipeline object is defined. That is, does it
     * have any outputs? */
    bool defined() const;

    /** Invalidate any internal cached state, e.g. because Funcs have
     * been rescheduled. */
    void invalidate_cache();

private:

    std::string generate_function_name() const;
};

struct ExternSignature {
private:
    Type ret_type_;       // Only meaningful if is_void_return is false; must be default value otherwise
    bool is_void_return_{false};
    std::vector<Type> arg_types_;

public:
    ExternSignature() = default;

    ExternSignature(const Type &ret_type, bool is_void_return, const std::vector<Type> &arg_types)
        : ret_type_(ret_type),
          is_void_return_(is_void_return),
          arg_types_(arg_types) {
        internal_assert(!(is_void_return && ret_type != Type()));
    }

    template <typename RT, typename... Args>
    ExternSignature(RT (*f)(Args... args))
        : ret_type_(type_of<RT>()),
          is_void_return_(std::is_void<RT>::value),
          arg_types_({type_of<Args>()...}) {
    }

    const Type &ret_type() const {
        internal_assert(!is_void_return());
        return ret_type_;
    }

    bool is_void_return() const {
        return is_void_return_;
    }

    const std::vector<Type> &arg_types() const {
        return arg_types_;
    }
};

struct ExternCFunction {
private:
    void *address_{nullptr};
    ExternSignature signature_;

public:
    ExternCFunction() = default;

    ExternCFunction(void *address, const ExternSignature &signature)
        : address_(address), signature_(signature) {}

    template <typename RT, typename... Args>
    ExternCFunction(RT (*f)(Args... args)) : ExternCFunction((void *)f, ExternSignature(f)) {}

    void *address() const { return address_; }
    const ExternSignature &signature() const { return signature_; }
};

struct JITExtern {
private:
    // Note that exactly one of pipeline_ and extern_c_function_
    // can be set in a given JITExtern instance.
    Pipeline pipeline_;
    ExternCFunction extern_c_function_;

public:
    JITExtern(Pipeline pipeline);
    JITExtern(Func func);
    JITExtern(const ExternCFunction &extern_c_function);

    template <typename RT, typename... Args>
    JITExtern(RT (*f)(Args... args)) : JITExtern(ExternCFunction(f)) {}

    const Pipeline &pipeline() const { return pipeline_; }
    const ExternCFunction &extern_c_function() const { return extern_c_function_; }
};

}  // namespace Halide

#endif
