#ifndef TYPE_H
#define TYPE_H

#include <string>

namespace Halide {
class Type; // forward declaration
}

void defineType();

std::string type_repr(const Halide::Type &t); // helper function

#endif // TYPE_H
