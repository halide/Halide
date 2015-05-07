#ifndef HALIDE_PIPELINE_H
#define HALIDE_PIPELINE_H

/** \file
 *
 * Defines the front-end class representing an entire Halide imaging
 * pipeline.
 */

#include <vector>

#include "Buffer.h"
#include "IntrusivePtr.h"
#include "Image.h"
#include "JITModule.h"
#include "Module.h"
#include "Tuple.h"
#include "Target.h"

namespace Halide {

class Argument;
class Func;
class PipelineContents;

namespace Internal {
class IRMutator;
}

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
}

/** A custom lowering pass. See Pipeline::add_custom_lowering_pass. */
struct CustomLoweringPass {
    Internal::IRMutator *pass;
    void (*deleter)(Internal::IRMutator *);
};

/** A struct specifying a collection of outputs. Used as an argument
 * to Pipeline::compile_to and Func::compile_to */
struct Outputs {
    /** The name of the emitted object file. Empty if no object file
     * output is desired. */
    std::string object_name;

    /** The name of the emitted text assembly file. Empty if no
     * assembly file output is desired. */
    std::string assembly_name;

    /** The name of the emitted llvm bitcode. Empty if no llvm bitcode
     * output is desired. */
    std::string bitcode_name;

    /** Make a new Outputs struct that emits everything this one does
     * and also an object file with the given name. */
    Outputs object(const std::string &object_name) {
        Outputs updated = *this;
        updated.object_name = object_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also an assembly file with the given name. */
    Outputs assembly(const std::string &assembly_name) {
        Outputs updated = *this;
        updated.assembly_name = assembly_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also an llvm bitcode file with the given name. */
    Outputs bitcode(const std::string &bitcode_name) {
        Outputs updated = *this;
        updated.bitcode_name = bitcode_name;
        return updated;
    }
};

/** A class representing a Halide pipeline. Constructed from the Func
 * or Funcs that it outputs. */
class Pipeline {
    Internal::IntrusivePtr<PipelineContents> contents;

    std::vector<Buffer> validate_arguments(const std::vector<Argument> &args);
    std::vector<const void *> prepare_jit_call_arguments(Realization dst, const Target &target);

public:
    /** Make an undefined Pipeline object. */
    EXPORT Pipeline();

    /** Make a pipeline that computes the given Func. Schedules the
     * Func compute_root(). */
    EXPORT Pipeline(Func output);

    /** Make a pipeline that computes the givens Funcs as
     * outputs. Schedules the Funcs compute_root(). */
    EXPORT Pipeline(const std::vector<Func> &outputs);

    /** Get the Funcs this pipeline outputs. */
    EXPORT std::vector<Func> outputs();

    /** Compile and generate multiple target files with single call.
     * Deduces target files based on filenames specified in
     * output_files struct.
     */
    EXPORT void compile_to(const Outputs &output_files,
                           const std::vector<Argument> &args,
                           const std::string &fn_name,
                           const Target &target);

    /** Statically compile a pipeline to llvm bitcode, with the given
     * filename (which should probably end in .bc), type signature,
     * and C function name. If you're compiling a pipeline with a
     * single output Func, see also Func::compile_to_bitcode. */
    EXPORT void compile_to_bitcode(const std::string &filename,
                                   const std::vector<Argument> &args,
                                   const std::string &fn_name,
                                   const Target &target = get_target_from_environment());

    /** Statically compile a pipeline with multiple output functions to an
     * object file, with the given filename (which should probably end in
     * .o or .obj), type signature, and C function name (which defaults to
     * the same name as this halide function. You probably don't want to
     * use this directly; call compile_to_file instead. */
    EXPORT void compile_to_object(const std::string &filename,
                                  const std::vector<Argument> &,
                                  const std::string &fn_name,
                                  const Target &target = get_target_from_environment());

    /** Emit a header file with the given filename for a pipeline. The
     * header will define a function with the type signature given by
     * the second argument, and a name given by the third. You don't
     * actually have to have defined any of these functions yet to
     * call this. You probably don't want to use this directly; call
     * compile_to_file instead. */
    EXPORT void compile_to_header(const std::string &filename,
                                  const std::vector<Argument> &,
                                  const std::string &fn_name,
                                  const Target &target = get_target_from_environment());

    /** Statically compile a pipeline to text assembly equivalent to
     * the object file generated by compile_to_object. This is useful
     * for checking what Halide is producing without having to
     * disassemble anything, or if you need to feed the assembly into
     * some custom toolchain to produce an object file. */
    EXPORT void compile_to_assembly(const std::string &filename,
                                    const std::vector<Argument> &args,
                                    const std::string &fn_name,
                                    const Target &target = get_target_from_environment());

    /** Statically compile a pipeline to C source code. This is useful
     * for providing fallback code paths that will compile on many
     * platforms. Vectorization will fail, and parallelization will
     * produce serial code. */
    EXPORT void compile_to_c(const std::string &filename,
                             const std::vector<Argument> &,
                             const std::string &fn_name,
                             const Target &target = get_target_from_environment());

    /** Write out an internal representation of lowered code. Useful
     * for analyzing and debugging scheduling. Can emit html or plain
     * text. */
    EXPORT void compile_to_lowered_stmt(const std::string &filename,
                                        const std::vector<Argument> &args,
                                        StmtOutputFormat fmt = Text,
                                        const Target &target = get_target_from_environment());

    /** Write out the loop nests specified by the schedule for this
     * Pipeline's Funcs. Helpful for understanding what a schedule is
     * doing. */
    EXPORT void print_loop_nest();

    /** Compile to object file and header pair, with the given
     * arguments. Also names the C function to match the filename
     * argument. */
    EXPORT void compile_to_file(const std::string &filename_prefix,
                                const std::vector<Argument> &args,
                                const Target &target = get_target_from_environment());

    /** Create an internal representation of lowered code as a self
     * contained Module suitable for further compilation. */
    EXPORT Module compile_to_module(const std::vector<Argument> &args,
                                    const std::string &fn_name,
                                    const Target &target = get_target_from_environment());

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
    EXPORT void set_custom_trace(int (*trace_fn)(void *, const halide_trace_event *));

    /** Set the function called to print messages from the runtime.
     * If you are compiling statically, you can also just define your
     * own function with signature
     \code
     extern "C" void halide_print(void *user_context, const char *);
     \endcode
     * This will clobber Halide's version.
     */
    EXPORT void set_custom_print(void (*handler)(void *, const char *));

    /** Get a struct containing the currently set custom functions
     * used by JIT. */
    EXPORT const Internal::JITHandlers &jit_handlers();

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
        // cast it to a deleter that takes a IRMutator *. The custom
        // deleter lives in user code, so that deletion is on the same
        // heap as construction (I hate Windows).
        void (*deleter)(Internal::IRMutator *) =
            (void (*)(Internal::IRMutator *))(&delete_lowering_pass<T>);
        add_custom_lowering_pass(pass, deleter);
    }

    /** Add a custom pass to be used during lowering, with the
     * function that will be called to delete it also passed in. Set
     * it to NULL if you wish to retain ownership of the object. */
    EXPORT void add_custom_lowering_pass(Internal::IRMutator *pass,
                                         void (*deleter)(Internal::IRMutator *));

    /** Remove all previously-set custom lowering passes */
    EXPORT void clear_custom_lowering_passes();

    /** Get the custom lowering passes. */
    EXPORT const std::vector<CustomLoweringPass> &custom_lowering_passes();

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

    template<typename T>
    NO_INLINE void realize(Image<T> dst, const Target &target = get_jit_target_from_environment()) {
        // Images are expected to exist on-host.
        realize(Buffer(dst), target);
        dst.copy_to_host();
    }
    // @}

    /** For a given size of output, or a given set of output buffers,
     * determine the bounds required of all unbound ImageParams
     * referenced. Communicates the result by allocating new buffers
     * of the appropriate size and binding them to the unbound
     * ImageParams. */
    // @{
    EXPORT void infer_input_bounds(int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0);
    EXPORT void infer_input_bounds(Realization dst);
    EXPORT void infer_input_bounds(Buffer dst);
    // @}

    /** Infer the arguments to the Pipeline, sorted into a canonical order:
     * all buffers (sorted alphabetically by name), followed by all non-buffers
     * (sorted alphabetically by name).
     This lets you write things like:
     \code
     pipeline.compile_to_assembly("/dev/stdout", pipeline.infer_arguments());
     \endcode
     */
    EXPORT std::vector<Argument> infer_arguments();

    /** Check if this pipeline object is defined. That is, does it
     * have any outputs? */
    EXPORT bool defined() const;

    /** Invalidate any internal cached state, e.g. because Funcs have
     * been rescheduled. */
    EXPORT void invalidate_cache();

};

}

#endif
