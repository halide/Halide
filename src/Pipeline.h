#ifndef HALIDE_PIPELINE_H
#define HALIDE_PIPELINE_H

/** \file
 *
 * Defines the front-end class representing an entire Halide imaging
 * pipeline.
 */

#include <initializer_list>
#include <map>
#include <memory>
#include <vector>

#include "ExternalCode.h"
#include "IROperator.h"
#include "IntrusivePtr.h"
#include "JITModule.h"
#include "Module.h"
#include "ParamMap.h"
#include "Realization.h"
#include "Target.h"
#include "Tuple.h"

namespace Halide {

struct Argument;
class Func;
struct PipelineContents;

/** A struct representing the machine parameters to generate the auto-scheduled
 * code for. */
struct MachineParams {
    /** Maximum level of parallelism avalaible. */
    int parallelism;
    /** Size of the last-level cache (in bytes). */
    uint64_t last_level_cache_size;
    /** Indicates how much more expensive is the cost of a load compared to
     * the cost of an arithmetic operation at last level cache. */
    float balance;

    explicit MachineParams(int parallelism, uint64_t llc, float balance)
        : parallelism(parallelism), last_level_cache_size(llc), balance(balance) {
    }

    /** Default machine parameters for generic CPU architecture. */
    static MachineParams generic();

    /** Convert the MachineParams into canonical string form. */
    std::string to_string() const;

    /** Reconstruct a MachineParams from canonical string form. */
    explicit MachineParams(const std::string &s);
};

namespace Internal {
class IRMutator;
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
    Internal::IRMutator *pass;
    std::function<void()> deleter;
};

struct JITExtern;

struct AutoSchedulerResults {
    std::string scheduler_name;          // name of the autoscheduler used
    Target target;                       // Target specified to the autoscheduler
    std::string machine_params_string;   // MachineParams specified to the autoscheduler (in string form)
    std::string schedule_source;         // The C++ source code of the generated schedule
    std::vector<uint8_t> featurization;  // The featurization of the pipeline (if any)
};

class Pipeline;

using AutoSchedulerFn = std::function<void(const Pipeline &, const Target &, const MachineParams &, AutoSchedulerResults *outputs)>;

/** A class representing a Halide pipeline. Constructed from the Func
 * or Funcs that it outputs. */
class Pipeline {
public:
    struct RealizationArg {
        // Only one of the following may be non-null
        Realization *r{nullptr};
        halide_buffer_t *buf{nullptr};
        std::unique_ptr<std::vector<Buffer<>>> buffer_list;

        RealizationArg(Realization &r)
            : r(&r) {
        }
        RealizationArg(Realization &&r)
            : r(&r) {
        }
        RealizationArg(halide_buffer_t *buf)
            : buf(buf) {
        }
        template<typename T, int Dims>
        RealizationArg(Runtime::Buffer<T, Dims> &dst)
            : buf(dst.raw_buffer()) {
        }
        template<typename T, int Dims>
        HALIDE_NO_USER_CODE_INLINE RealizationArg(Buffer<T, Dims> &dst)
            : buf(dst.raw_buffer()) {
        }
        template<typename T, int Dims, typename... Args,
                 typename = typename std::enable_if<Internal::all_are_convertible<Buffer<>, Args...>::value>::type>
        RealizationArg(Buffer<T, Dims> &a, Args &&...args)
            : buffer_list(std::make_unique<std::vector<Buffer<>>>(std::initializer_list<Buffer<>>{a, std::forward<Args>(args)...})) {
        }
        RealizationArg(RealizationArg &&from) = default;

        size_t size() const {
            if (r != nullptr) {
                return r->size();
            } else if (buffer_list) {
                return buffer_list->size();
            }
            return 1;
        }
    };

private:
    Internal::IntrusivePtr<PipelineContents> contents;

    struct JITCallArgs;  // Opaque structure to optimize away dynamic allocation in this path.

    // For the three method below, precisely one of the first two args should be non-null
    void prepare_jit_call_arguments(RealizationArg &output, const Target &target, const ParamMap &param_map,
                                    JITUserContext **user_context, bool is_bounds_inference, JITCallArgs &args_result);

    static std::vector<Internal::JITModule> make_externs_jit_module(const Target &target,
                                                                    std::map<std::string, JITExtern> &externs_in_out);

    static std::map<std::string, AutoSchedulerFn> &get_autoscheduler_map();

    static std::string &get_default_autoscheduler_name();

    static AutoSchedulerFn find_autoscheduler(const std::string &autoscheduler_name);

    int call_jit_code(const Target &target, const JITCallArgs &args);

    // Get the value of contents->jit_target, but reality-check that the contents
    // sensibly match the value. Return Target() if not jitted.
    Target get_compiled_jit_target() const;

public:
    /** Make an undefined Pipeline object. */
    Pipeline();

    /** Make a pipeline that computes the given Func. Schedules the
     * Func compute_root(). */
    Pipeline(const Func &output);

    /** Make a pipeline that computes the givens Funcs as
     * outputs. Schedules the Funcs compute_root(). */
    Pipeline(const std::vector<Func> &outputs);

    std::vector<Argument> infer_arguments(const Internal::Stmt &body);

    /** Get the Funcs this pipeline outputs. */
    std::vector<Func> outputs() const;

    /** Generate a schedule for the pipeline using the currently-default autoscheduler. */
    AutoSchedulerResults auto_schedule(const Target &target,
                                       const MachineParams &arch_params = MachineParams::generic());

    /** Generate a schedule for the pipeline using the specified autoscheduler. */
    AutoSchedulerResults auto_schedule(const std::string &autoscheduler_name,
                                       const Target &target,
                                       const MachineParams &arch_params = MachineParams::generic());

    /** Add a new the autoscheduler method with the given name. Does not affect the current default autoscheduler.
     * It is an error to call this with the same name multiple times. */
    static void add_autoscheduler(const std::string &autoscheduler_name, const AutoSchedulerFn &autoscheduler);

    /** Globally set the default autoscheduler method to use whenever
     * autoscheduling any Pipeline when no name is specified. If the autoscheduler_name isn't in the
     * current table of known autoschedulers, assert-fail.
     *
     * At this time, well-known autoschedulers include:
     *  "Mullapudi2016" -- heuristics-based; the first working autoscheduler; currently built in to libHalide
     *                     see http://graphics.cs.cmu.edu/projects/halidesched/
     *  "Adams2019"     -- aka "the ML autoscheduler"; currently located in apps/autoscheduler
     *                     see https://halide-lang.org/papers/autoscheduler2019.html
     *  "Li2018"        -- aka "the gradient autoscheduler"; currently located in apps/gradient_autoscheduler.
     *                     see https://people.csail.mit.edu/tzumao/gradient_halide
     */
    static void set_default_autoscheduler_name(const std::string &autoscheduler_name);

    /** Return handle to the index-th Func within the pipeline based on the
     * topological order. */
    Func get_func(size_t index);

    /** Compile and generate multiple target files with single call.
     * Deduces target files based on filenames specified in
     * output_files map.
     */
    void compile_to(const std::map<OutputFileType, std::string> &output_files,
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

    /** Like compile_to_multitarget_static_library(), except that the object files
     * are all output as object files (rather than bundled into a static library).
     *
     * `suffixes` is an optional list of strings to use for as the suffix for each object
     * file. If nonempty, it must be the same length as `targets`. (If empty, Target::to_string()
     * will be used for each suffix.)
     *
     * Note that if `targets.size()` > 1, the wrapper code (to select the subtarget)
     * will be generated with the filename `${filename_prefix}_wrapper.o`
     *
     * Note that if `targets.size()` > 1 and `no_runtime` is not specified, the runtime
     * will be generated with the filename `${filename_prefix}_runtime.o`
     */
    void compile_to_multitarget_object_files(const std::string &filename_prefix,
                                             const std::vector<Argument> &args,
                                             const std::vector<Target> &targets,
                                             const std::vector<std::string> &suffixes);

    /** Create an internal representation of lowered code as a self
     * contained Module suitable for further compilation. */
    Module compile_to_module(const std::vector<Argument> &args,
                             const std::string &fn_name,
                             const Target &target = get_target_from_environment(),
                             LinkageType linkage_type = LinkageType::ExternalPlusMetadata);

    /** Eagerly jit compile the function to machine code. This
     * normally happens on the first call to realize. If you're
     * running your halide pipeline inside time-sensitive code and
     * wish to avoid including the time taken to compile a pipeline,
     * then you can call this ahead of time. Default is to use the Target
     * returned from Halide::get_jit_target_from_environment()
     */
    void compile_jit(const Target &target = get_jit_target_from_environment());

    /** Install a set of external C functions or Funcs to satisfy
     * dependencies introduced by HalideExtern and define_extern
     * mechanisms. These will be used by calls to realize,
     * infer_bounds, and compile_jit. */
    void set_jit_externs(const std::map<std::string, JITExtern> &externs);

    /** Return the map of previously installed externs. Is an empty
     * map unless set otherwise. */
    const std::map<std::string, JITExtern> &get_jit_externs();

    /** Get a struct containing the currently set custom functions
     * used by JIT. This can be mutated. Changes will take effect the
     * next time this Pipeline is realized. */
    JITHandlers &jit_handlers();

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
    void add_custom_lowering_pass(Internal::IRMutator *pass, std::function<void()> deleter);

    /** Remove all previously-set custom lowering passes */
    void clear_custom_lowering_passes();

    /** Get the custom lowering passes. */
    const std::vector<CustomLoweringPass> &custom_lowering_passes();

    /** See Func::realize */
    Realization realize(std::vector<int32_t> sizes = {}, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());

    /** Same as above, but takes a custom user-provided context to be
     * passed to runtime functions. A nullptr context is legal, and is
     * equivalent to calling the variant of realize that does not take
     * a context. */
    Realization realize(JITUserContext *context,
                        std::vector<int32_t> sizes = {},
                        const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());

    /** Evaluate this Pipeline into an existing allocated buffer or
     * buffers. If the buffer is also one of the arguments to the
     * function, strange things may happen, as the pipeline isn't
     * necessarily safe to run in-place. The realization should
     * contain one Buffer per tuple component per output Func. For
     * each individual output Func, all Buffers must have the same
     * shape, but the shape can vary across the different output
     * Funcs. This form of realize does *not* automatically copy data
     * back from the GPU. */
    void realize(RealizationArg output,
                 const Target &target = Target(),
                 const ParamMap &param_map = ParamMap::empty_map());

    /** Same as above, but takes a custom user-provided context to be
     * passed to runtime functions. A nullptr context is legal, and
     * is equivalent to calling the variant of realize that does not
     * take a context. */
    void realize(JITUserContext *context,
                 RealizationArg output,
                 const Target &target = Target(),
                 const ParamMap &param_map = ParamMap::empty_map());

    /** For a given size of output, or a given set of output buffers,
     * determine the bounds required of all unbound ImageParams
     * referenced. Communicates the result by allocating new buffers
     * of the appropriate size and binding them to the unbound
     * ImageParams. */
    // @{
    void infer_input_bounds(const std::vector<int32_t> &sizes,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    void infer_input_bounds(RealizationArg output,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    // @}

    /** Variants of infer_inputs_bounds that take a custom user context */
    // @{
    void infer_input_bounds(JITUserContext *context,
                            const std::vector<int32_t> &sizes,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    void infer_input_bounds(JITUserContext *context,
                            RealizationArg output,
                            const Target &target = get_jit_target_from_environment(),
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

    /** Add a top-level precondition to the generated pipeline,
     * expressed as a boolean Expr. The Expr may depend on parameters
     * only, and may not call any Func or use a Var. If the condition
     * is not true at runtime, the pipeline will call halide_error
     * with the remaining arguments, and return
     * halide_error_code_requirement_failed. Requirements are checked
     * in the order added. */
    void add_requirement(const Expr &condition, std::vector<Expr> &error);

    /** Generate begin_pipeline and end_pipeline tracing calls for this pipeline. */
    void trace_pipeline();

    template<typename... Args>
    inline HALIDE_NO_USER_CODE_INLINE void add_requirement(const Expr &condition, Args &&...args) {
        std::vector<Expr> collected_args;
        Internal::collect_print_args(collected_args, std::forward<Args>(args)...);
        add_requirement(condition, collected_args);
    }

private:
    std::string generate_function_name() const;
};

struct ExternSignature {
private:
    Type ret_type_;  // Only meaningful if is_void_return is false; must be default value otherwise
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

    template<typename RT, typename... Args>
    explicit ExternSignature(RT (*f)(Args... args))
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

    friend std::ostream &operator<<(std::ostream &stream, const ExternSignature &sig) {
        if (sig.is_void_return_) {
            stream << "void";
        } else {
            stream << sig.ret_type_;
        }
        stream << " (*)(";
        bool comma = false;
        for (const auto &t : sig.arg_types_) {
            if (comma) {
                stream << ", ";
            }
            stream << t;
            comma = true;
        }
        stream << ")";
        return stream;
    }
};

struct ExternCFunction {
private:
    void *address_{nullptr};
    ExternSignature signature_;

public:
    ExternCFunction() = default;

    ExternCFunction(void *address, const ExternSignature &signature)
        : address_(address), signature_(signature) {
    }

    template<typename RT, typename... Args>
    ExternCFunction(RT (*f)(Args... args))
        : ExternCFunction((void *)f, ExternSignature(f)) {
    }

    void *address() const {
        return address_;
    }
    const ExternSignature &signature() const {
        return signature_;
    }
};

struct JITExtern {
private:
    // Note that exactly one of pipeline_ and extern_c_function_
    // can be set in a given JITExtern instance.
    Pipeline pipeline_;
    ExternCFunction extern_c_function_;

public:
    explicit JITExtern(Pipeline pipeline);
    explicit JITExtern(const Func &func);
    explicit JITExtern(const ExternCFunction &extern_c_function);

    template<typename RT, typename... Args>
    explicit JITExtern(RT (*f)(Args... args))
        : JITExtern(ExternCFunction(f)) {
    }

    const Pipeline &pipeline() const {
        return pipeline_;
    }
    const ExternCFunction &extern_c_function() const {
        return extern_c_function_;
    }
};

}  // namespace Halide

#endif
