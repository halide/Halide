#include "Error.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;

void translate_error(h::Error const &e) {
    // Use the Python 'C' API to set up an exception object
    PyErr_SetString(PyExc_RuntimeError,
                    (std::string("Halide Error: ") + e.what()).c_str());
    return;
}

void translate_runtime_error(h::RuntimeError const &e) {
    // Use the Python 'C' API to set up an exception object
    PyErr_SetString(PyExc_RuntimeError,
                    (std::string("Halide RuntimeError: ") + e.what()).c_str());
    return;
}

void translate_compile_error(h::CompileError const &e) {
    // Use the Python 'C' API to set up an exception object
    PyErr_SetString(PyExc_RuntimeError,
                    (std::string("Halide CompileError: ") + e.what()).c_str());
    return;
}

void translate_internal_error(h::InternalError const &e) {
    // Use the Python 'C' API to set up an exception object
    PyErr_SetString(PyExc_RuntimeError,
                    (std::string("Halide InternalError: ") + e.what()).c_str());
    return;
}

void defineError() {
    // Might create linking problems, if Param.cpp is not included in the python library

    p::register_exception_translator<h::Error>(&translate_error);
    p::register_exception_translator<h::RuntimeError>(&translate_runtime_error);
    p::register_exception_translator<h::CompileError>(&translate_compile_error);
    p::register_exception_translator<h::InternalError>(&translate_internal_error);

    return;
}
