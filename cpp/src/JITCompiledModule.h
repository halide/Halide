#ifndef HALIDE_JIT_COMPILED_MODULE_H
#define HALIDE_JIT_COMPILED_MODULE_H

/** \file
 * Defines the struct representing a JIT compiled halide pipeline 
 */

#include "IntrusivePtr.h"

namespace Halide {
namespace Internal {

class JITModuleHolder;

/** Function pointers into a compiled halide module. */
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

    /** The type of a halide runtime error handler function */
    typedef void (*ErrorHandler)(char *);

    /** Set the runtime error handler for this module */
    void (*set_error_handler)(ErrorHandler);

    /** Set a custom malloc and free for this module to use. See 
     * \ref Func::set_custom_allocator */
    void (*set_custom_allocator)(void *(*malloc)(size_t), void (*free)(void *));

    /** Set a custom parallel for loop launcher. See 
     * \ref Func::set_custom_do_par_for */
    void (*set_custom_do_par_for)(void (*custom_do_par_for)(void (*)(int, uint8_t *), int, int, uint8_t *));

    /** Set a custom do parallel task. See
     * \ref Func::set_custom_do_task */
    void (*set_custom_do_task)(void (*custom_do_task)(void (*)(int, uint8_t *), int, uint8_t *));

    // The JIT Module Allocator holds onto the memory storing the functions above.
    IntrusivePtr<JITModuleHolder> module;

    JITCompiledModule() : function(NULL), 
                          wrapped_function(NULL), 
                          set_error_handler(NULL), 
                          set_custom_allocator(NULL), 
                          set_custom_do_par_for(NULL), 
                          set_custom_do_task(NULL), 
                          module(NULL) {}
};
        
}
}


#endif
