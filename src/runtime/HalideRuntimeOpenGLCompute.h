#ifndef HALIDE_HALIDERUNTIMEOPENGLCOMPUTE_H
#define HALIDE_HALIDERUNTIMEOPENGLCOMPUTE_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide OpenGL Compute runtime.
 */

#define HALIDE_RUNTIME_OPENGLCOMPUTE

extern const struct halide_device_interface_t *halide_openglcompute_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide Glsl runtime. Do not call them. */
// @{

/** This function sets up OpenGL context, loads relevant GL functions, then
 *  compiles src OpenGL compute shader into OpenGL program and stores it for future use.
 */
extern int halide_openglcompute_initialize_kernels(void *user_context, void **state_ptr,
                                            const char *src, int size);

/** This function triggers execution of OpenGL program built around compute shader.
 *  Execution of the shader is parallelized into given number of blocks and threads.
 *
 *  This function doesn't wait for the completion of the shader, but it sets memory
 *  barrier which forces successive retrieval of output data to wait until shader is done.
 */
extern int halide_openglcompute_run(void *user_context,
                             void *state_ptr,
                             const char *entry_name,
                             int blocksX, int blocksY, int blocksZ,
                             int threadsX, int threadsY, int threadsZ,
                             int shared_mem_bytes,
                             size_t arg_sizes[],
                             void *args[],
                             int8_t is_buffer[],
                             int num_attributes,
                             float* vertex_buffer,
                             int num_coords_dim0,
                             int num_coords_dim1);
// @}

/** This function retrieves pointers to OpenGL API functions.
 *
 *  You may have to implement this yourself. Halide only provides implementations
 *  for some platforms."
 */
void *halide_opengl_get_proc_address(void *user_context, const char *name);


/** This function creates an OpenGL context for use by the OpenGL backend.
 *
 *  You may have to implement this yourself as well. Halide only provides
*   implementations for some platforms."
 */
int halide_opengl_create_context(void *user_context);

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEOPENGLCOMPUTE_H
