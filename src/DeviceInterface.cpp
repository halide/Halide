#include "JITModule.h"
#include "Target.h"

#include "runtime/HalideRuntime.h"
#include "runtime/HalideRuntimeCuda.h"
#include "runtime/HalideRuntimeOpenCL.h"
#include "runtime/HalideRuntimeOpenGL.h"
#include "runtime/HalideRuntimeOpenGLCompute.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

template <typename fn_type>
bool lookup_runtime_routine(const char *name, const Target &target,
                            fn_type &result) {
    std::vector<JITModule> runtime(
        JITSharedRuntime::get(NULL, target.with_feature(Target::JIT)));

    for (size_t i = 0; i < runtime.size(); i++) {
        std::map<std::string, JITModule::Symbol>::const_iterator f =
          runtime[i].exports().find(name);
        if (f != runtime[i].exports().end()) {
            result = reinterpret_bits<fn_type>(f->second.address);
            return true;
        }
    }
    return false;
}

}

extern "C" {

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
void halide_device_release(void *user_context, const halide_device_interface *interface) {
    user_assert(user_context == NULL) << "Cannot provide user_context to libHalide.a halide_device_release\n";
    Target target(get_host_target());
    void (*fn)(void *user_context, const halide_device_interface *interface);
    if (lookup_runtime_routine("halide_device_release", target, fn)) {
        (*fn)(user_context, interface);
    }
}

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
int halide_copy_to_host(void *user_context, struct buffer_t *buf) {
    user_assert(user_context == NULL) << "Cannot provide user_context to libHalide.a halide_copy_to_host\n";
    // Skip if there is no device buffer.
    if (buf->dev == 0)
        return 0;
    Target target(get_host_target());
    int (*fn)(void *user_context, struct buffer_t *buf);
    if (lookup_runtime_routine("halide_copy_to_host", target, fn)) {
        return (*fn)(user_context, buf);
    }
    return -1;
}

/** Copy image data from host memory to device memory. This should not
 * be called directly; Halide handles copying to the device
 * automatically.  If interface is NULL and the bug has a non-zero dev
 * field, the device associated with the dev handle will be
 * used. Otherwise if the dev field is 0 and interface is NULL, an
 * error is returned. */
int halide_copy_to_device(void *user_context, struct buffer_t *buf,
                          const halide_device_interface *interface) {
    user_assert(user_context == NULL) << "Cannot provide user_context to libHalide.a halide_copy_to_device\n";
    Target target(get_host_target());
    int (*fn)(void *user_context, struct buffer_t *buf, const halide_device_interface *interface);
    if (lookup_runtime_routine("halide_copy_to_device", target, fn)) {
      return (*fn)(user_context, buf, interface);
    }
    return -1;
}

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
int halide_device_sync(void *user_context, struct buffer_t *buf) {
    return false;
}

/** Allocate device memory to back a buffer_t. */
int halide_device_malloc(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    user_assert(user_context == NULL) << "Cannot provide user_context to libHalide.a halide_device_malloc\n";
    Target target(get_host_target());
    int (*fn)(void *user_context, struct buffer_t *buf, const halide_device_interface *interface);
    if (lookup_runtime_routine("halide_device_malloc", target, fn)) {
      return (*fn)(user_context, buf, interface);
    }
    return -1;
}

int halide_device_free(void *user_context, struct buffer_t *buf) {
    user_assert(user_context == NULL) << "Cannot provide user_context to libHalide.a halide_device_free\n";
    // Skip if there is no device buffer.
    if (buf->dev == 0)
        return 0;
    Target target(get_host_target());
    int (*fn)(void *user_context, struct buffer_t *buf);
    if (lookup_runtime_routine("halide_device_free", target, fn)) {
        return (*fn)(user_context, buf);
    }
    if (buf->dev != 0) {
        return -1;
    } else {
        return 0;
    }
}

const struct halide_device_interface *halide_cuda_device_interface() {
    Target target(get_host_target());
    target.set_feature(Target::CUDA);
    struct halide_device_interface *(*fn)();
    if (lookup_runtime_routine("halide_cuda_device_interface", target, fn)) {
        return (*fn)();
    }
    return NULL;
}

const struct halide_device_interface *halide_opencl_device_interface() {
    Target target(get_host_target());
    target.set_feature(Target::OpenCL);
    struct halide_device_interface *(*fn)();
    if (lookup_runtime_routine("halide_opencl_device_interface", target, fn)) {
        return (*fn)();
    }
    return NULL;
}

const struct halide_device_interface *halide_opengl_device_interface() {
    Target target(get_host_target());
    target.set_feature(Target::OpenGL);
    struct halide_device_interface *(*fn)();
    if (lookup_runtime_routine("halide_opengl_device_interface", target, fn)) {
        return (*fn)();
    }
    return NULL;
}

const struct halide_device_interface *halide_openglcompute_device_interface() {
    Target target(get_host_target());
    target.set_feature(Target::OpenGLCompute);
    struct halide_device_interface *(*fn)();
    if (lookup_runtime_routine("halide_openglcompute_device_interface", target, fn)) {
        return (*fn)();
    }
    return NULL;
}

const struct halide_device_interface *halide_renderscript_device_interface() {
    Target target(get_host_target());
    target.set_feature(Target::Renderscript);
    struct halide_device_interface *(*fn)();
    if (lookup_runtime_routine("halide_renderscript_device_interface", target, fn)) {
        return (*fn)();
    }
    return NULL;
}

}
