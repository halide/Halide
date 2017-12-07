#ifndef HALIDE_PYTHON_BINDINGS_TYPE_H
#define HALIDE_PYTHON_BINDINGS_TYPE_H

#include <string>

namespace Halide {
struct Type;  // forward declaration
}

void define_type();

std::string type_repr(const Halide::Type &t);  // helper function
std::string type_code_to_string(const Halide::Type &t);

#endif  // HALIDE_PYTHON_BINDINGS_TYPE_H
