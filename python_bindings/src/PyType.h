#ifndef HALIDE_PYTHON_BINDINGS_PYTYPE_H
#define HALIDE_PYTHON_BINDINGS_PYTYPE_H

#include "Halide.h"

void define_type();

std::string halide_type_to_string(const Halide::Type &type);

#endif  // HALIDE_PYTHON_BINDINGS_PYTYPE_H
