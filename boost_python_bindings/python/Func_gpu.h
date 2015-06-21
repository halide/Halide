#ifndef FUNC_GPU_H
#define FUNC_GPU_H

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Func.h"

/// Define all gpu related methods
void defineFuncGpuMethods(boost::python::class_<Halide::Func> &func_class);


#endif // FUNC_GPU_H
