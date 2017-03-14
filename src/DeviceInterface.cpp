#include "DeviceInterface.h"
#include "JITModule.h"
#include "Target.h"

#include "runtime/HalideRuntime.h"
#include "runtime/HalideRuntimeCuda.h"
#include "runtime/HalideRuntimeOpenCL.h"
#include "runtime/HalideRuntimeOpenGL.h"
#include "runtime/HalideRuntimeOpenGLCompute.h"

#ifdef _MSC_VER
// We want to export the functions below from Halide.dll, but we can't
// use conventional EXPORT qualifiers because these are all declared
// in HalideRuntime.h without them. Instead we use a pragma that adds
// a linker flag. Because the pragma is in a macro, we use __pragma
// instead of #pragma
#ifdef _WIN64
#define EXPORT_SYM(n) __pragma(comment(linker, "/EXPORT:" #n))
#else
#define EXPORT_SYM(n) __pragma(comment(linker, "/EXPORT:_" #n))
#endif
#else
#define EXPORT_SYM(n)
#endif

using namespace Halide;
using namespace Halide::Internal;

namespace {

template <typename fn_type>
bool lookup_runtime_routine(const std::string &name,
                            const Target &target,
                            fn_type &result) {
    std::vector<JITModule> runtime(
        JITSharedRuntime::get(nullptr, target.with_feature(Target::JIT)));

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

namespace Halide {
const halide_device_interface_t *get_default_device_interface_for_target(const Target &t) {
    return get_device_interface_for_device_api(DeviceAPI::Default_GPU, t);
}

const halide_device_interface_t *get_device_interface_for_device_api(const DeviceAPI &d, const Target &t) {
    if (d == DeviceAPI::Default_GPU) {
        return get_device_interface_for_device_api(get_default_device_api_for_target(t), t);
    }

    const struct halide_device_interface_t *(*fn)();
    std::string name;
    if (d == DeviceAPI::Metal) {
        name = "metal";
    } else if (d == DeviceAPI::OpenCL) {
        name = "opencl";
    } else if (d == DeviceAPI::CUDA) {
        name = "cuda";
    } else if (d == DeviceAPI::OpenGLCompute) {
        name = "openglcompute";
    } else if (d == DeviceAPI::GLSL) {
        name = "opengl";
    } else {
        return nullptr;
    }

    if (lookup_runtime_routine("halide_" + name + "_device_interface", t, fn)) {
        return (*fn)();
    } else {
        return nullptr;
    }
}

DeviceAPI get_default_device_api_for_target(const Target &target) {
    if (target.has_feature(Target::Metal)) {
        return DeviceAPI::Metal;
    } else if (target.has_feature(Target::OpenCL)) {
        return DeviceAPI::OpenCL;
    } else if (target.has_feature(Target::CUDA)) {
        return DeviceAPI::CUDA;
    } else if (target.has_feature(Target::OpenGLCompute)) {
        return DeviceAPI::OpenGLCompute;
    } else if (target.has_feature(Target::OpenGL)) {
        return DeviceAPI::GLSL;
    } else {
        return DeviceAPI::Host;
    }
}
}

extern "C" {

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
void halide_device_release(void *user_context, const halide_device_interface_t *device_interface) {
    user_assert(user_context == nullptr) << "Cannot provide user_context to libHalide.a halide_device_release\n";
    Target target(get_host_target());
    void (*fn)(void *user_context, const halide_device_interface_t *device_interface);
    if (lookup_runtime_routine("halide_device_release", target, fn)) {
        (*fn)(user_context, device_interface);
    }
}
EXPORT_SYM(halide_device_release)

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
int halide_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    user_assert(user_context == nullptr) << "Cannot provide user_context to libHalide.a halide_copy_to_host\n";
    // Skip if there is no device buffer.
    if (buf->device == 0) {
        return 0;
    }
    Target target(get_host_target());
    int (*fn)(void *user_context, struct halide_buffer_t *buf);
    if (lookup_runtime_routine("halide_copy_to_host", target, fn)) {
        return (*fn)(user_context, buf);
    }
    return -1;
}
EXPORT_SYM(halide_copy_to_host)

/** Copy image data from host memory to device memory. This should not
 * be called directly; Halide handles copying to the device
 * automatically.  If interface is nullptr and the bug has a non-zero dev
 * field, the device associated with the dev handle will be
 * used. Otherwise if the dev field is 0 and interface is nullptr, an
 * error is returned. */
int halide_copy_to_device(void *user_context, struct halide_buffer_t *buf,
                          const halide_device_interface_t *device_interface) {
    user_assert(user_context == nullptr) << "Cannot provide user_context to libHalide.a halide_copy_to_device\n";
    Target target(get_host_target());
    int (*fn)(void *user_context, struct halide_buffer_t *buf, const halide_device_interface_t *device_interface);
    if (lookup_runtime_routine("halide_copy_to_device", target, fn)) {
        return (*fn)(user_context, buf, device_interface);
    }
    return -1;
}
EXPORT_SYM(halide_copy_to_device)

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
int halide_device_sync(void *user_context, struct halide_buffer_t *buf) {
    user_assert(user_context == nullptr) << "Cannot provide user_context to libHalide.a halide_device_sync\n";
    Target target(get_host_target());
    int (*fn)(void *user_context, struct halide_buffer_t *buf);
    if (lookup_runtime_routine("halide_device_sync", target, fn)) {
      return (*fn)(user_context, buf);
    }
    return -1;
}
EXPORT_SYM(halide_device_sync)

/** Allocate device memory to back a halide_buffer_t. */
int halide_device_malloc(void *user_context, struct halide_buffer_t *buf, const halide_device_interface_t *device_interface) {
    user_assert(user_context == nullptr) << "Cannot provide user_context to libHalide.a halide_device_malloc\n";
    Target target(get_host_target());
    int (*fn)(void *user_context, struct halide_buffer_t *buf, const halide_device_interface_t *device_interface);
    if (lookup_runtime_routine("halide_device_malloc", target, fn)) {
      return (*fn)(user_context, buf, device_interface);
    }
    return -1;
}
EXPORT_SYM(halide_device_malloc)

int halide_device_free(void *user_context, struct halide_buffer_t *buf) {
    user_assert(user_context == nullptr) << "Cannot provide user_context to libHalide.a halide_device_free\n";
    // Skip if there is no device buffer.
    if (buf->device == 0) {
        return 0;
    }
    Target target(get_host_target());
    int (*fn)(void *user_context, struct halide_buffer_t *buf);
    if (lookup_runtime_routine("halide_device_free", target, fn)) {
        return (*fn)(user_context, buf);
    }
    if (buf->device != 0) {
        return -1;
    } else {
        return 0;
    }
}
EXPORT_SYM(halide_device_free)

int halide_weak_device_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_device_free(user_context, buf);
}
EXPORT_SYM(halide_weak_device_free)

const struct halide_device_interface_t *halide_cuda_device_interface() {
    Target t = get_jit_target_from_environment().with_feature(Target::CUDA);
    return get_device_interface_for_device_api(DeviceAPI::CUDA, t);
}
EXPORT_SYM(halide_cuda_device_interface)

const struct halide_device_interface_t *halide_opencl_device_interface() {
    Target t = get_jit_target_from_environment().with_feature(Target::OpenCL);
    return get_device_interface_for_device_api(DeviceAPI::OpenCL, t);
}
EXPORT_SYM(halide_opencl_device_interface)

const struct halide_device_interface_t *halide_opengl_device_interface() {
    Target t = get_jit_target_from_environment().with_feature(Target::OpenGL);
    return get_device_interface_for_device_api(DeviceAPI::GLSL, t);
}
EXPORT_SYM(halide_opengl_device_interface)

int halide_opengl_wrap_texture(void *user_context, struct halide_buffer_t *buf, uintptr_t tex) {
    Target target(get_host_target());
    target.set_feature(Target::OpenGL);
    int (*fn)(void *, struct halide_buffer_t *, uintptr_t);
    if (lookup_runtime_routine("halide_opengl_wrap_texture", target, fn)) {
        return (*fn)(user_context, buf, tex);
    }
    return 0;
}
EXPORT_SYM(halide_opengl_wrap_texture)

uintptr_t halide_opengl_detach_texture(void *user_context, struct halide_buffer_t *buf) {
    Target target(get_host_target());
    target.set_feature(Target::OpenGL);
    uintptr_t (*fn)(void *, struct halide_buffer_t *);
    if (lookup_runtime_routine("halide_opengl_detach_texture", target, fn)) {
        return (*fn)(user_context, buf);
    }
    return 0;
}
EXPORT_SYM(halide_opengl_detach_texture)

const struct halide_device_interface_t *halide_openglcompute_device_interface() {
    Target t = get_jit_target_from_environment().with_feature(Target::OpenGLCompute);
    return get_device_interface_for_device_api(DeviceAPI::OpenGLCompute, t);
}
EXPORT_SYM(halide_openglcompute_device_interface)

const struct halide_device_interface_t *halide_metal_device_interface() {
    Target t = get_jit_target_from_environment().with_feature(Target::Metal);
    return get_device_interface_for_device_api(DeviceAPI::Metal, t);
}
EXPORT_SYM(halide_metal_device_interface)

}
