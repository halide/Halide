#ifndef IMAGE_H
#define IMAGE_H

#include "Halide.h"
#include <boost/python.hpp>

void defineBuffer();
boost::python::object buffer_to_python_object(const Halide::Buffer<> &);
Halide::Buffer<> python_object_to_buffer(boost::python::object);

#endif  // IMAGE_H
