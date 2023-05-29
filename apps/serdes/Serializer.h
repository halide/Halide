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

private:
    // helper functions to serialize each type of object
    flatbuffers::Offset<flatbuffers::String> serialize_string(flatbuffers::FlatBufferBuilder& builder, const std::string& str);

    flatbuffers::Offset<Halide::Serdes::Type> serialize_type(flatbuffers::FlatBufferBuilder& builder, const Halide::Type& type);

    flatbuffers::Offset<Halide::Serdes::Func> serialize_func(flatbuffers::FlatBufferBuilder& builder, const Halide::Internal::Function& function);
};


#endif
