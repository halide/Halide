#ifndef HALIDE_SERIALIZER_H
#define HALIDE_SERIALIZER_H

#include <vector>
#include <string>

#include "halide_ir_generated.h"
#include <Halide.h>
using namespace Halide;

class Serializer {
public:
    Serializer() = default;

    void serialize(const Pipeline& pipeline, const std::string& filename);
};


#endif
