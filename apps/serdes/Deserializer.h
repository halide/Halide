#ifndef HALIDE_DESERIALIZER_H
#define HALIDE_DESERIALIZER_H

#include <vector>
#include <string>

#include "halide_ir_generated.h"
#include <Halide.h>
using namespace Halide;

class Deserializer {
public:
    Deserializer() = default;

    Pipeline deserialize(const std::string& filename);
};


#endif
