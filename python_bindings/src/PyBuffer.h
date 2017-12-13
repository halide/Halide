#ifndef HALIDE_PYTHON_BINDINGS_PYBUFFER_H
#define HALIDE_PYTHON_BINDINGS_PYBUFFER_H

#include <boost/python.hpp>

#include "Halide.h"

void define_buffer();
boost::python::object buffer_to_python_object(const Halide::Buffer<> &);
Halide::Buffer<> python_object_to_buffer(boost::python::object);

#endif  // HALIDE_PYTHON_BINDINGS_PYBUFFER_H
