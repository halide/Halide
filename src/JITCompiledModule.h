#ifndef HALIDE_JIT_COMPILED_MODULE_H
#define HALIDE_JIT_COMPILED_MODULE_H

/** \file
 * Defines the struct representing a JIT compiled halide pipeline 
 */

#include "IntrusivePtr.h"

namespace llvm {
class Module;
}

namespace Halide {
namespace Internal {

class JITModuleHolder;
class CodeGen;

/** Function pointers into a compiled halide module. These function
 * pointers are meaningless once the last copy of a JITCompiledModule
 * is deleted, so don't cache them. */
struct JITCompiledModule {
    /** A pointer to the raw halide function. It's true type depends
     * on the Argument vector passed to CodeGen::compile. Image
     * parameters become (buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the buffer_t defining the output. */
    void *function;

    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref function */
    void (*wrapped_function)(const void **);

    /** JITed helpers to interact with device-mapped buffer_t
     * objects. These pointers may be NULL if not compiling for a
     * gpu-like target. */
    // @{
    void (*copy_to_host)(struct buffer_t*);
    void (*copy_to_dev)(struct buffer_t*);
    void (*free_dev_buffer)(struct buffer_t*);
    // @}

    /** The type of a halide runtime error handler function */
    typedef void (*ErrorHandler)(char *);

    /** Set the runtime error handler for this module */
    void (*set_error_handler)(ErrorHandler);

    /** Set a custom malloc and free for this module to use. See 
     * \ref Func::set_custom_allocator */
    void (*set_custom_allocator)(void *(*malloc)(size_t), void (*free)(void *));

    /** Set a custom parallel for loop launcher. See 
     * \ref Func::set_custom_do_par_for */
    void (*set_custom_do_par_for)(void (*custom_do_par_for)(void (*)(int, unsigned char *), int, int, unsigned char *));

    /** Set a custom do parallel task. See
     * \ref Func::set_custom_do_task */
    void (*set_custom_do_task)(void (*custom_do_task)(void (*)(int, unsigned char *), int, unsigned char *));

    /** Shutdown the thread pool maintained by this JIT module. This
     * is also done automatically when the last reference to this
     * module is destroyed. */
    void (*shutdown_thread_pool)();

    // The JIT Module Allocator holds onto the memory storing the functions above.
    IntrusivePtr<JITModuleHolder> module;

    JITCompiledModule() : 
        function(NULL), 
        wrapped_function(NULL),
        copy_to_host(NULL), 
        copy_to_dev(NULL), 
        free_dev_buffer(NULL), 
        set_error_handler(NULL), 
        set_custom_allocator(NULL), 
        set_custom_do_par_for(NULL), 
        set_custom_do_task(NULL), 
        shutdown_thread_pool(NULL) {}
                
    /** Take an llvm module and compile it. Populates the function
     * pointer members above with the result. */
    void compile_module(CodeGen *cg, llvm::Module *mod, const std::string &function_name);

};
        
}
}


#endif
