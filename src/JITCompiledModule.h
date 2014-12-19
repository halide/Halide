#ifndef HALIDE_JIT_COMPILED_MODULE_H
#define HALIDE_JIT_COMPILED_MODULE_H

/** \file
 * Defines the struct representing a JIT compiled halide pipeline
 */

#include "IntrusivePtr.h"
#include "runtime/HalideRuntime.h"
#include "LLVM_Output.h"

namespace llvm {
class Module;
}

namespace Halide {
namespace Internal {

class JITModuleHolder;

/** Function pointers into a compiled halide module. These function
 * pointers are meaningless once the last copy of a JITCompiledModule
 * is deleted, so don't cache them. */
struct JITCompiledModule {
    /** A pointer to the raw halide function. It's true type depends
     * on the Argument vector passed to CodeGen_LLVM::compile. Image
     * parameters become (buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the buffer_t defining the output. */
    void *function;

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

    // The JIT Module Allocator holds onto the memory storing the functions above.
    IntrusivePtr<JITModuleHolder> module;

    /** Take an llvm module and compile it. Populates the function
     * pointer members above with the result. */
    JITCompiledModule(llvm::Module *module = NULL, const std::string &fn = "");

    /** Holds a cleanup routine and context parameter. */
    struct CleanupRoutine {
        void (*fn)(void *);
        void *context;

        CleanupRoutine() : fn(NULL), context(NULL) {}
        CleanupRoutine(void (*fn)(void *), void *context) : fn(fn), context(context) {}
    };
};

}
}


#endif
