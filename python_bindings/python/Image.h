#ifndef IMAGE_H
#define IMAGE_H

#include "../../src/runtime/HalideBuffer.h"
#include <boost/python.hpp>

void defineImage();
boost::python::object image_to_python_object(const Halide::Image<> &);
Halide::Image<> python_object_to_image(boost::python::object);

#endif  // IMAGE_H
