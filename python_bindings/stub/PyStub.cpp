// Small stub to produce a module-specific entry point
// for a Python module. It assumes/requires that it is
// linked with PyStubImpl.o to be useful.
//
// Note that this quite deliberately doesn't include any PyBind11 headers;
// it is designed to be compiled without PyBind11 being available at compile time,
// to simplify build requirements in downstream environments.

#include <Python.h>
#include <functional>
#include <memory>

#include "Halide.h"

#ifndef HALIDE_PYSTUB_GENERATOR_NAME
#error "HALIDE_PYSTUB_GENERATOR_NAME must be defined"
#endif

#ifndef HALIDE_PYSTUB_MODULE_NAME
#define HALIDE_PYSTUB_MODULE_NAME HALIDE_PYSTUB_GENERATOR_NAME
#endif

extern "C" PyObject *_halide_pystub_impl(const char *module_name, const Halide::Internal::GeneratorFactory &factory);

#define HALIDE_STRINGIFY(x) #x
#define HALIDE_TOSTRING(x) HALIDE_STRINGIFY(x)

#define _HALIDE_CONCAT(first, second) first##second
#define HALIDE_CONCAT(first, second) _HALIDE_CONCAT(first, second)

static_assert(PY_MAJOR_VERSION >= 3, "Python bindings for Halide require Python 3+");

#define _HALIDE_PLUGIN_IMPL(name) extern "C" HALIDE_EXPORT_SYMBOL PyObject *PyInit_##name() /* NOLINT(bugprone-macro-parentheses) */
#define HALIDE_PLUGIN_IMPL(name) _HALIDE_PLUGIN_IMPL(name)

// clang-format off

namespace halide_register_generator {
namespace HALIDE_CONCAT(HALIDE_PYSTUB_GENERATOR_NAME, _ns) {
    extern std::unique_ptr<Halide::Internal::AbstractGenerator> factory(const Halide::GeneratorContext &context);
}  // namespace HALIDE_CONCAT(HALIDE_PYSTUB_GENERATOR_NAME,_ns)
}  // namespace halide_register_generator

HALIDE_PLUGIN_IMPL(HALIDE_PYSTUB_MODULE_NAME) {
    const auto factory = halide_register_generator::HALIDE_CONCAT(HALIDE_PYSTUB_GENERATOR_NAME, _ns)::factory;
    return _halide_pystub_impl(HALIDE_TOSTRING(HALIDE_PYSTUB_MODULE_NAME), factory);
}

// clang-format on
