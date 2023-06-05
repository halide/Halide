#ifndef HALIDE_SERIALIZER_H
#define HALIDE_SERIALIZER_H

#include <string>
#include <utility>
#include <vector>

#include "halide_ir_generated.h"
#include "Halide.h"

class Serializer {
public:
    Serializer() = default;

    void serialize(const Halide::Pipeline &pipeline, const std::string &filename);

private:
    // helper functions to serialize each type of object
    flatbuffers::Offset<flatbuffers::String> serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str);

    flatbuffers::Offset<Halide::Serialize::Type> serialize_type(flatbuffers::FlatBufferBuilder &builder, const Halide::Type &type);

    // Stmt is special because it is a union type so we need to return both the type and serialized object
    std::pair<Halide::Serialize::Stmt, flatbuffers::Offset<void>> serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Stmt &stmt);

    // similar to Stmt, Expr is a union type so we need to return both the type and serialized object
    std::pair<Halide::Serialize::Expr, flatbuffers::Offset<void>> serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Halide::Expr &expr);

    flatbuffers::Offset<Halide::Serialize::Func> serialize_func(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Function &function);

    flatbuffers::Offset<Halide::Serialize::Range> serialize_range(flatbuffers::FlatBufferBuilder &builder, const Halide::Range &range);
};

#endif
