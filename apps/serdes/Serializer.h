#ifndef HALIDE_SERIALIZER_H
#define HALIDE_SERIALIZER_H

#include <string>
#include <utility>
#include <vector>

#include "halide_ir_generated.h"
#include <Halide.h>
using namespace Halide;

class Serializer {
public:
    Serializer() = default;

    void serialize(const Pipeline &pipeline, const std::string &filename);

private:
    // helper functions to serialize each type of object
    flatbuffers::Offset<flatbuffers::String> serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str);

    flatbuffers::Offset<Halide::Serdes::Type> serialize_type(flatbuffers::FlatBufferBuilder &builder, const Halide::Type &type);

    // Stmt is special because it is a union type so we need to return both the type and serialized object
    std::pair<Halide::Serdes::Stmt, flatbuffers::Offset<void>> serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Stmt &stmt);

    // similar to Stmt, Expr is a union type so we need to return both the type and serialized object
    std::pair<Halide::Serdes::Expr, flatbuffers::Offset<void>> serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Halide::Expr &expr);

    flatbuffers::Offset<Halide::Serdes::Func> serialize_func(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Function &function);

    flatbuffers::Offset<Halide::Serdes::Range> serialize_range(flatbuffers::FlatBufferBuilder &builder, const Halide::Range &range);
};

#endif
