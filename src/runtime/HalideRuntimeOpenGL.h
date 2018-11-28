#ifndef HALIDE_HALIDERUNTIMEOPENGL_H
#define HALIDE_HALIDERUNTIMEOPENGL_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide OpenGL runtime.
 */

#define HALIDE_RUNTIME_OPENGL

extern const struct halide_device_interface_t *halide_opengl_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide Glsl runtime. Do not call them. */
// @{
extern int halide_opengl_initialize_kernels(void *user_context, void **state_ptr,
                                            const char *src, int size);

extern int halide_opengl_run(void *user_context,
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

/** Set the underlying OpenGL texture for a buffer. The texture must
 * have an extent large enough to cover that specified by the
 * halide_buffer_t extent fields. The dev field of the halide_buffer_t
 * must be NULL when this routine is called. This call can fail due to
 * being passed an invalid texture. The device and host dirty bits are
 * left unmodified. */
extern int halide_opengl_wrap_texture(void *user_context, struct halide_buffer_t *buf, uint64_t texture_id);

/** Set the underlying OpenGL texture for a buffer to refer to the
 * current render target (e.g., the frame buffer or an FBO). The
 * render target must have an extent large enough to cover that
 * specified by the halide_buffer_t extent fields. The dev field of
 * the halide_buffer_t must be NULL when this routine is called. This
 * call can fail due to running out of memory. The device and host
 * dirty bits are left unmodified. */
extern int halide_opengl_wrap_render_target(void *user_context, struct halide_buffer_t *buf);

/** Disconnect this halide_buffer_t from the texture it was previously
 * wrapped around. Should only be called for a halide_buffer_t that
 * halide_opengl_wrap_texture was previously called on. Frees any
 * storage associated with the binding of the halide_buffer_t and the
 * device pointer, but does not free the texture.  The dev field of
 * the halide_buffer_t will be NULL on return.
 */
extern int halide_opengl_detach_texture(void *user_context, struct halide_buffer_t *buf);

/** Return the underlying texture for a halide_buffer_t. This buffer
 *  must be valid on an OpenGL device, or not have any associated
 *  device memory. If there is no device memory (dev field is NULL),
 *  or if the buffer was wrapped via
 *  halide_opengl_wrap_render_target(), this returns 0.
 */
extern uintptr_t halide_opengl_get_texture(void *user_context, struct halide_buffer_t *buf);

/** Forget all state associated with the previous OpenGL context.  This is
 * similar to halide_opengl_release, except that we assume that all OpenGL
 * resources have already been reclaimed by the OS. */
extern void halide_opengl_context_lost(void *user_context);

/** This functions MUST be provided by the host environment to retrieve pointers
 *  to OpenGL API functions. */
void *halide_opengl_get_proc_address(void *user_context, const char *name);


/** This functions MUST be provided by the host environment to create an OpenGL
 *  context for use by the OpenGL backend. */
int halide_opengl_create_context(void *user_context);

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEOPENGL_H
