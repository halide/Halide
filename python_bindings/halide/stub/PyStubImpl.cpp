#include <Python.h>

#include "Halide.h"

extern "C" int _halide_pystub_impl(PyObject *module, const Halide::Internal::GeneratorFactory &factory) {
    PyObject *compiler = PyImport_ImportModule("halide.halide_");
    if (!compiler) {
        return -1;
    }
    PyObject *initialize = PyObject_GetAttrString(compiler, "_initialize_pystub");
    Py_DECREF(compiler);
    if (!initialize) {
        return -1;
    }

    // The compiler copies the factory during this synchronous call.
    PyObject *capsule = PyCapsule_New(
        const_cast<Halide::Internal::GeneratorFactory *>(&factory),
        "halide.generator_factory", nullptr);
    if (!capsule) {
        Py_DECREF(initialize);
        return -1;
    }
    PyObject *result = PyObject_CallFunctionObjArgs(initialize, module, capsule, nullptr);
    Py_DECREF(capsule);
    Py_DECREF(initialize);
    if (!result) {
        return -1;
    }
    Py_DECREF(result);
    return 0;
}
