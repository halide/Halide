// Small stub to produce a module-specific entry point
// for a Python module. It assumes/requires that it is
// linked with PyStubImpl.o to be useful.
//
// Note that this quite deliberately doesn't include any PyBind11 headers;
//it is designed to be compiled without PyBind11 being available at compile time,
// to simplify build requirements in downstream environments.

#include <memory>
#include <Python.h>

#ifndef HALIDE_PYSTUB_GENERATOR_NAME
    #error "HALIDE_PYSTUB_GENERATOR_NAME must be defined"
#endif

#ifndef HALIDE_PYSTUB_MODULE_NAME
    #define HALIDE_PYSTUB_MODULE_NAME HALIDE_PYSTUB_GENERATOR_NAME
#endif

namespace Halide {
class GeneratorContext;
namespace Internal {
class GeneratorBase;
}  // namespace Internal
}  // namespace Halide

using FactoryFunc = std::unique_ptr<Halide::Internal::GeneratorBase> (*)(const Halide::GeneratorContext& context);

extern "C" PyObject *_halide_pystub_impl(const char *module_name, FactoryFunc factory);

#define HALIDE_STRINGIFY(x) #x
#define HALIDE_TOSTRING(x) HALIDE_STRINGIFY(x)

#define _HALIDE_CONCAT(first, second) first##second
#define HALIDE_CONCAT(first, second) _HALIDE_CONCAT(first, second)

#if !defined(HALIDE_EXPORT)
    #if defined(WIN32) || defined(_WIN32)
        #define HALIDE_EXPORT __declspec(dllexport)
    #else
        #define HALIDE_EXPORT __attribute__ ((visibility("default")))
    #endif
#endif

#if PY_MAJOR_VERSION >= 3
    #define _HALIDE_PLUGIN_IMPL(name) \
        extern "C" HALIDE_EXPORT PyObject *PyInit_##name()
#else
    #define _HALIDE_PLUGIN_IMPL(name) \
        static PyObject *halide_init_wrapper();                 \
        extern "C" HALIDE_EXPORT void init##name() {            \
            (void)halide_init_wrapper();                        \
        }                                                       \
        PyObject *halide_init_wrapper()
#endif
#define HALIDE_PLUGIN_IMPL(name) _HALIDE_PLUGIN_IMPL(name)

namespace halide_register_generator {
namespace HALIDE_CONCAT(HALIDE_PYSTUB_GENERATOR_NAME, _ns) {
extern std::unique_ptr<Halide::Internal::GeneratorBase> factory(const Halide::GeneratorContext& context);
}  // namespace _ns
}  // namespace halide_register_generator


HALIDE_PLUGIN_IMPL(HALIDE_PYSTUB_MODULE_NAME) {
    const auto factory = halide_register_generator:: HALIDE_CONCAT(HALIDE_PYSTUB_GENERATOR_NAME, _ns) ::factory;
    return _halide_pystub_impl(HALIDE_TOSTRING(HALIDE_PYSTUB_MODULE_NAME), factory);
}
