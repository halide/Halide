#ifndef TYPE_H
#define TYPE_H

#include <string>

namespace Halide {
struct Type;  // forward declaration
}

void defineType();

std::string type_repr(const Halide::Type &t);  // helper function
std::string type_code_to_string(const Halide::Type &t);

#endif  // TYPE_H
