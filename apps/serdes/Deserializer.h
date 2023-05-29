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

private:
    // helper functions to deserialize each type of object
    std::string deserialize_string(const flatbuffers::String* str);

    Halide::Type deserialize_type(const Halide::Serdes::Type* type);

    Halide::Internal::Function deserialize_function(const Halide::Serdes::Func* function);

    Halide::Internal::Stmt deserialize_stmt(uint8_t type_code, const void * stmt);
};


#endif
