#ifndef HALIDE_DESERIALIZER_H
#define HALIDE_DESERIALIZER_H

#include <string>
#include <vector>

#include "Halide.h"
#include "halide_ir_generated.h"

class Deserializer {
public:
    Deserializer() = default;

    Halide::Pipeline deserialize(const std::string &filename);

private:
    std::map<std::string, int32_t> func_mappings_str2idx;
    std::map<int32_t, Halide::Internal::FunctionPtr> func_mappings_idx2ptr;

    // helper functions to deserialize each type of object
    Halide::MemoryType deserialize_memory_type(const Halide::Serialize::MemoryType memory_type);

    std::string deserialize_string(const flatbuffers::String *str);

    Halide::Type deserialize_type(const Halide::Serialize::Type *type);

    Halide::Internal::Function deserialize_function(const Halide::Serialize::Func *function);

    Halide::Internal::Stmt deserialize_stmt(uint8_t type_code, const void *stmt);

    Halide::Expr deserialize_expr(uint8_t type_code, const void *expr);

    std::vector<Halide::Expr> deserialize_expr_vector(const flatbuffers::Vector<uint8_t> *exprs_types, const flatbuffers::Vector<flatbuffers::Offset<void>> *exprs_serialized);

    Halide::Range deserialize_range(const Halide::Serialize::Range *range);

    Halide::Internal::Bound deserialize_bound(const Halide::Serialize::Bound *bound);

    Halide::Internal::StorageDim deserialize_storage_dim(const Halide::Serialize::StorageDim *storage_dim);

    Halide::LoopLevel deserialize_loop_level(const Halide::Serialize::LoopLevel *loop_level);

    Halide::Internal::FuncSchedule deserialize_func_schedule(const Halide::Serialize::FuncSchedule *func_schedule);

    Halide::Internal::Specialization deserialize_specialization(const Halide::Serialize::Specialization *specialization);

    Halide::Internal::Definition deserialize_definition(const Halide::Serialize::Definition *definition);

    // std::map<std::string, Halide::Internal::FunctionPtr> deserialize_wrapper_refs(const flatbuffers::Vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> *wrapper_refs);

    // std::map<std::string, int32_t> deserialize_func_mappings(const flatbuffers::Vector<flatbuffers::Offset<Halide::Serialize::FuncMapping>> *func_mappings);

    // std::map<int32_t, Halide::Internal::FunctionPtr> reconstruct_func_ptr_mappings();
};

#endif
