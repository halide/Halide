#ifndef HALIDE_JIT_MODULE_H
#define HALIDE_JIT_MODULE_H

/** \file
 * Defines the struct representing a JIT compiled halide pipeline
 */

#include "IntrusivePtr.h"
#include "runtime/HalideRuntime.h"

#include <map>

namespace llvm {
class Module;
class Type;
}

namespace Halide {
namespace Internal {

#if 0
struct JITHooks {
    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref function */
    int (*wrapped_function)(const void **);

    /** JITed helpers to interact with device-mapped buffer_t
     * objects. These pointers may be NULL if not compiling for a
     * gpu-like target. */
    // @{
    int (*copy_to_host)(void *user_context, struct buffer_t*);
    int (*copy_to_dev)(void *user_context, struct buffer_t*);
    int (*free_dev_buffer)(void *user_context, struct buffer_t*);
    // @}

    /** The type of a halide runtime error handler function */
    typedef void (*ErrorHandler)(void *user_context, const char *);

    /** Set the runtime error handler for this module */
    void (*set_error_handler)(ErrorHandler);

    /** Set a custom malloc and free for this module to use. See
     * \ref Func::set_custom_allocator */
    void (*set_custom_allocator)(void *(*malloc)(void *user_context, size_t),
                                 void (*free)(void *user_context, void *ptr));

    /** Set a custom parallel for loop launcher. See
     * \ref Func::set_custom_do_par_for */
    typedef int (*HalideTask)(void *user_context, int, uint8_t *);
    void (*set_custom_do_par_for)(int (*custom_do_par_for)(void *user_context, HalideTask,
                                                           int, int, uint8_t *));

    /** Set a custom do parallel task. See
     * \ref Func::set_custom_do_task */
    void (*set_custom_do_task)(int (*custom_do_task)(void *user_context, HalideTask,
                                                     int, uint8_t *));

    /** Set a custom trace function. See \ref Func::set_custom_trace. */
    typedef int (*TraceFn)(void *, const halide_trace_event *);
    void (*set_custom_trace)(TraceFn);

    /** Set a custom print function for this module. See
     * \ref Func::set_custom_print. */
    void (*set_custom_print)(void (*custom_print)(void *, const char *));

    /** Shutdown the thread pool maintained by this JIT module. This
     * is also done automatically when the last reference to this
     * module is destroyed. */
    void (*shutdown_thread_pool)();

    /** Set the maximum number of bytes occupied by the cache for compute_cached. */
    void (*memoization_cache_set_size)(uint64_t size);

    JITHooks() :
        wrapped_function(NULL),
        copy_to_host(NULL),
        copy_to_dev(NULL),
        free_dev_buffer(NULL),
        set_error_handler(NULL),
        set_custom_allocator(NULL),
        set_custom_do_par_for(NULL),
        set_custom_do_task(NULL),
        set_custom_trace(NULL),
        set_custom_print(NULL),
        shutdown_thread_pool(NULL),
        memoization_cache_set_size(NULL) {}
};
#endif

class JITModuleContents;
class CodeGen;

struct JITModule {
    IntrusivePtr<JITModuleContents> jit_module;

#if 0
    /** Holds a cleanup routine and context parameter. */
    struct CleanupRoutine {
        void (*fn)(void *);
        void *context;

        CleanupRoutine() : fn(NULL), context(NULL) {}
        CleanupRoutine(void (*fn)(void *), void *context) : fn(fn), context(context) {}
    };
#endif

    struct Symbol {
        void *address;
        llvm::Type *llvm_type;
    };

    JITModule() { }
    JITModule(const std::map<std::string, Symbol> &exports);

    const std::map<std::string, Symbol> &Exports() const;

    /** A pointer to the raw halide function. It's true type depends
     * on the Argument vector passed to CodeGen::compile. Image
     * parameters become (buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the buffer_t defining the output. This will be NULL for
     * a JITModule which has not yet been compiled or one that is not
     * a Halide Func compilation at all. */
    void *main_function() const;

    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref main_function . This will
     * be NULL for a JITModule which has not yet been compiled or one
     * that is not a Halide Func compilation at all. */
    int (*jit_wrapper_function() const)(const void **);

    // TODO: This should likely be a constructor.
    /** Take an llvm module and compile it. The requested exports will
        be available via the Exports method. */
    void compile_module(CodeGen *cg,
                        llvm::Module *mod, const std::string &function_name,
                        const std::vector<JITModule> &dependencies,
                        const std::vector<std::string> &requested_exports);

    /** Make extern declarations fo rall exports of this JITModule in another llvm::Module */
    void make_externs(llvm::Module *mod);
};

class JITSharedRuntime {
    static bool inited;
    static JITModule host_shared_jit_runtime;
public:
    // Note only the first CodeGen passed in here is used. The same shared runtime is used for all JIT.
    static JITModule &Get(CodeGen *cg);
};

}
}

#endif
