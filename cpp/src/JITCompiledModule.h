#ifndef HALIDE_JIT_COMPILED_MODULE_H
#define HALIDE_JIT_COMPILED_MODULE_H

/** \file
 * Defines the struct representing a JIT compiled halide pipeline 
 */

namespace Halide {
namespace Internal {

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

    /** Set a custom malloc and free for this module to use. Malloc
     * should return 32-byte aligned chunks of memory, with 32-bytes
     * extra allocated on the start and end so that vector loads can
     * spill off the end slightly. Metadata (e.g. the base address of
     * the region allocated) can go in this margin - it is only read,
     * not written. */
    void (*set_custom_allocator)(void *(*malloc)(size_t), void (*free)(void *));

    JITCompiledModule() : function(NULL), wrapped_function(NULL), set_error_handler(NULL), set_custom_allocator(NULL) {}
};
        
}
}


#endif
