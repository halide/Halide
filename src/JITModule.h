#ifndef HALIDE_JIT_MODULE_H
#define HALIDE_JIT_MODULE_H

/** \file
 * Defines the struct representing lifetime and dependencies of
 * a JIT compiled halide pipeline
 */

#include <map>
#include <memory>
#include <vector>

#include "IntrusivePtr.h"
#include "Target.h"
#include "Type.h"
#include "WasmExecutor.h"
#include "runtime/HalideRuntime.h"

namespace llvm {
class Module;
}

namespace Halide {

struct ExternCFunction;
struct JITExtern;
class Module;

struct JITUserContext;

/** A set of custom overrides of runtime functions. These only apply
 * when JIT-compiling code. If you are doing AOT compilation, see
 * HalideRuntime.h for instructions on how to replace runtime
 * functions. */
struct JITHandlers {
    /** Set the function called to print messages from the runtime. */
    void (*custom_print)(JITUserContext *, const char *){nullptr};

    /** A custom malloc and free for halide to use. Malloc should
     * return 32-byte aligned chunks of memory, and it should be safe
     * for Halide to read slightly out of bounds (up to 8 bytes before
     * the start or beyond the end). */
    // @{
    void *(*custom_malloc)(JITUserContext *, size_t){nullptr};
    void (*custom_free)(JITUserContext *, void *){nullptr};
    // @}

    /** A custom task handler to be called by the parallel for
     * loop. It is useful to set this if you want to do some
     * additional bookkeeping at the granularity of parallel
     * tasks. The default implementation does this:
     \code
     extern "C" int halide_do_task(JITUserContext *user_context,
                                   int (*f)(void *, int, uint8_t *),
                                   int idx, uint8_t *state) {
         return f(user_context, idx, state);
     }
     \endcode
     *
     * If you're trying to use a custom parallel runtime, you probably
     * don't want to call this. See instead custom_do_par_for.
    */
    int (*custom_do_task)(JITUserContext *, int (*)(JITUserContext *, int, uint8_t *), int, uint8_t *){nullptr};

    /** A custom parallel for loop launcher. Useful if your app
     * already manages a thread pool. The default implementation is
     * equivalent to this:
     \code
     extern "C" int halide_do_par_for(JITUserContext *user_context,
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
     */
    int (*custom_do_par_for)(JITUserContext *, int (*)(JITUserContext *, int, uint8_t *), int, int, uint8_t *){nullptr};

    /** The error handler function that be called in the case of
     * runtime errors during halide pipelines. */
    void (*custom_error)(JITUserContext *, const char *){nullptr};

    /** A custom routine to call when tracing is enabled. Call this
     * on the output Func of your pipeline. This then sets custom
     * routines for the entire pipeline, not just calls to this
     * Func. */
    int32_t (*custom_trace)(JITUserContext *, const halide_trace_event_t *){nullptr};

    /** A method to use for Halide to resolve symbol names dynamically
     * in the calling process or library from within the Halide
     * runtime. Equivalent to dlsym with a null first argument. */
    void *(*custom_get_symbol)(const char *name){nullptr};

    /** A method to use for Halide to dynamically load libraries from
     * within the runtime. Equivalent to dlopen. Returns a handle to
     * the opened library. */
    void *(*custom_load_library)(const char *name){nullptr};

    /** A method to use for Halide to dynamically find a symbol within
     * an opened library. Equivalent to dlsym. Takes a handle
     * returned by custom_load_library as the first argument. */
    void *(*custom_get_library_symbol)(void *lib, const char *name){nullptr};

    /** A custom method for the Halide runtime acquire a cuda
     * context. The cuda context is treated as a void * to avoid a
     * dependence on the cuda headers. If the create argument is set
     * to true, a context should be created if one does not already
     * exist. */
    int32_t (*custom_cuda_acquire_context)(JITUserContext *user_context, void **cuda_context_ptr, bool create){nullptr};

    /** The Halide runtime calls this when it is done with a cuda
     * context. The default implementation does nothing. */
    int32_t (*custom_cuda_release_context)(JITUserContext *user_context){nullptr};

    /** A custom method for the Halide runtime to acquire a cuda
     * stream to use. The cuda context and stream are both modelled
     * as a void *, to avoid a dependence on the cuda headers. */
    int32_t (*custom_cuda_get_stream)(JITUserContext *user_context, void *cuda_context, void **stream_ptr){nullptr};
};

namespace Internal {
struct JITErrorBuffer;
}

/** A context to be passed to Pipeline::realize. Inherit from this to
 * pass your own custom context object. Modify the handlers field to
 * override runtime functions per-call to realize. */
struct JITUserContext {
    Internal::JITErrorBuffer *error_buffer{nullptr};
    JITHandlers handlers;
};

namespace Internal {

class JITModuleContents;
struct LoweredFunc;

struct JITModule {
    IntrusivePtr<JITModuleContents> jit_module;

    struct Symbol {
        void *address = nullptr;
        Symbol() = default;
        explicit Symbol(void *address)
            : address(address) {
        }
    };

    JITModule();
    JITModule(const Module &m, const LoweredFunc &fn,
              const std::vector<JITModule> &dependencies = std::vector<JITModule>());

    /** Take a list of JITExterns and generate trampoline functions
     * which can be called dynamically via a function pointer that
     * takes an array of void *'s for each argument and the return
     * value.
     */
    static JITModule make_trampolines_module(const Target &target,
                                             const std::map<std::string, JITExtern> &externs,
                                             const std::string &suffix,
                                             const std::vector<JITModule> &deps);

    /** The exports map of a JITModule contains all symbols which are
     * available to other JITModules which depend on this one. For
     * runtime modules, this is all of the symbols exported from the
     * runtime. For a JITted Func, it generally only contains the main
     * result Func of the compilation, which takes its name directly
     * from the Func declaration. One can also make a module which
     * contains no code itself but is just an exports maps providing
     * arbitrary pointers to functions or global variables to JITted
     * code. */
    const std::map<std::string, Symbol> &exports() const;

    /** A pointer to the raw halide function. Its true type depends
     * on the Argument vector passed to CodeGen_LLVM::compile. Image
     * parameters become (halide_buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the halide_buffer_t defining the output. This will be nullptr for
     * a JITModule which has not yet been compiled or one that is not
     * a Halide Func compilation at all. */
    void *main_function() const;

    /** Returns the Symbol structure for the routine documented in
     * main_function. Returning a Symbol allows access to the LLVM
     * type as well as the address. The address and type will be nullptr
     * if the module has not been compiled. */
    Symbol entrypoint_symbol() const;

    /** Returns the Symbol structure for the argv wrapper routine
     * corresponding to the entrypoint. The argv wrapper is callable
     * via an array of void * pointers to the arguments for the
     * call. Returning a Symbol allows access to the LLVM type as well
     * as the address. The address and type will be nullptr if the module
     * has not been compiled. */
    Symbol argv_entrypoint_symbol() const;

    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref main_function . This will
     * be nullptr for a JITModule which has not yet been compiled or one
     * that is not a Halide Func compilation at all. */
    // @{
    typedef int (*argv_wrapper)(const void *const *args);
    argv_wrapper argv_function() const;
    // @}

    /** Add another JITModule to the dependency chain. Dependencies
     * are searched to resolve symbols not found in the current
     * compilation unit while JITting. */
    void add_dependency(JITModule &dep);
    /** Registers a single Symbol as available to modules which depend
     * on this one. The Symbol structure provides both the address and
     * the LLVM type for the function, which allows type safe linkage of
     * extenal routines. */
    void add_symbol_for_export(const std::string &name, const Symbol &extern_symbol);
    /** Registers a single function as available to modules which
     * depend on this one. This routine converts the ExternSignature
     * info into an LLVM type, which allows type safe linkage of
     * external routines. */
    void add_extern_for_export(const std::string &name,
                               const ExternCFunction &extern_c_function);

    /** Look up a symbol by name in this module or its dependencies. */
    Symbol find_symbol_by_name(const std::string &) const;

    /** Take an llvm module and compile it. The requested exports will
        be available via the exports method. */
    void compile_module(std::unique_ptr<llvm::Module> mod,
                        const std::string &function_name, const Target &target,
                        const std::vector<JITModule> &dependencies = std::vector<JITModule>(),
                        const std::vector<std::string> &requested_exports = std::vector<std::string>());

    /** See JITSharedRuntime::memoization_cache_set_size */
    void memoization_cache_set_size(int64_t size) const;

    /** See JITSharedRuntime::memoization_cache_evict */
    void memoization_cache_evict(uint64_t eviction_key) const;

    /** See JITSharedRuntime::reuse_device_allocations */
    void reuse_device_allocations(bool) const;

    /** Return true if compile_module has been called on this module. */
    bool compiled() const;
};

class JITSharedRuntime {
public:
    // Note only the first llvm::Module passed in here is used. The same shared runtime is used for all JIT.
    static std::vector<JITModule> get(llvm::Module *m, const Target &target, bool create = true);
    static void populate_jit_handlers(JITUserContext *jit_user_context, const JITHandlers &handlers);
    static JITHandlers set_default_handlers(const JITHandlers &handlers);

    /** Set the maximum number of bytes used by memoization caching.
     * If you are compiling statically, you should include HalideRuntime.h
     * and call halide_memoization_cache_set_size() instead.
     */
    static void memoization_cache_set_size(int64_t size);

    /** Evict all cache entries that were tagged with the given
     * eviction_key in the memoize scheduling directive. If you are
     * compiling statically, you should include HalideRuntime.h and
     * call halide_memoization_cache_evict() instead.
     */
    static void memoization_cache_evict(uint64_t eviction_key);

    /** Set whether or not Halide may hold onto and reuse device
     * allocations to avoid calling expensive device API allocation
     * functions. If you are compiling statically, you should include
     * HalideRuntime.h and call halide_reuse_device_allocations
     * instead. */
    static void reuse_device_allocations(bool);

    static void release_all();
};

void *get_symbol_address(const char *s);

struct JITCache {
    Target jit_target;
    // Arguments for all inputs and outputs
    std::vector<Argument> arguments;
    std::map<std::string, JITExtern> jit_externs;
    JITModule jit_module;
    WasmModule wasm_module;

    JITCache() = default;
    JITCache(Target jit_target,
             std::vector<Argument> arguments,
             std::map<std::string, JITExtern> jit_externs,
             JITModule jit_module,
             WasmModule wasm_module);

    Target get_compiled_jit_target() const;

    int call_jit_code(const void *const *args);

    void finish_profiling(JITUserContext *context);
};

struct JITErrorBuffer {
    enum { MaxBufSize = 4096 };
    char buf[MaxBufSize];
    std::atomic<size_t> end{0};

    void concat(const char *message);

    std::string str() const;

    static void handler(JITUserContext *ctx, const char *message);
};

struct JITFuncCallContext {
    JITErrorBuffer error_buffer;
    JITUserContext *context;
    bool custom_error_handler;

    JITFuncCallContext(JITUserContext *context, const JITHandlers &pipeline_handlers);

    void finalize(int exit_status);
};

}  // namespace Internal
}  // namespace Halide

#endif
