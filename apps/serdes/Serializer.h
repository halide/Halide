#ifndef HALIDE_SERIALIZER_H
#define HALIDE_SERIALIZER_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Halide.h"
#include "halide_ir_generated.h"

class Serializer {
public:
    Serializer() = default;

    void serialize(const Halide::Pipeline &pipeline, const std::string &filename);

private:
    std::map<uint64_t, int32_t> func_mappings;

    // helper functions to serialize each type of object
    Halide::Serialize::MemoryType serialize_memory_type(const Halide::MemoryType &memory_type);

    Halide::Serialize::ForType serialize_for_type(const Halide::Internal::ForType &for_type);

    Halide::Serialize::DeviceAPI serialize_device_api(const Halide::DeviceAPI &device_api);

    Halide::Serialize::CallType serialize_call_type(const Halide::Internal::Call::CallType &call_type);

    Halide::Serialize::VectorReduceOp serialize_vector_reduce_op(const Halide::Internal::VectorReduce::Operator &vector_reduce_op);

    Halide::Serialize::PrefetchBoundStrategy serialize_prefetch_bound_strategy(const Halide::PrefetchBoundStrategy &prefetch_bound_strategy);

    Halide::Serialize::NameMangling serialize_name_mangling(const Halide::NameMangling &name_mangling);

    Halide::Serialize::TailStrategy serialize_tail_strategy(const Halide::TailStrategy &tail_strategy);

    Halide::Serialize::SplitType serialize_split_type(const Halide::Internal::Split::SplitType &split_type);

    Halide::Serialize::DimType serialize_dim_type(const Halide::Internal::DimType &dim_type);

    Halide::Serialize::LoopAlignStrategy serialize_loop_align_strategy(const Halide::LoopAlignStrategy &loop_align_strategy);

    flatbuffers::Offset<flatbuffers::String> serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str);

    flatbuffers::Offset<Halide::Serialize::Type> serialize_type(flatbuffers::FlatBufferBuilder &builder, const Halide::Type &type);

    // Stmt is special because it is a union type so we need to return both the type and serialized object
    std::pair<Halide::Serialize::Stmt, flatbuffers::Offset<void>> serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Stmt &stmt);

    // similar to Stmt, Expr is a union type so we need to return both the type and serialized object
    std::pair<Halide::Serialize::Expr, flatbuffers::Offset<void>> serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Halide::Expr &expr);

    flatbuffers::Offset<Halide::Serialize::Func> serialize_function(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Function &function);

    flatbuffers::Offset<Halide::Serialize::Range> serialize_range(flatbuffers::FlatBufferBuilder &builder, const Halide::Range &range);

    flatbuffers::Offset<Halide::Serialize::Bound> serialize_bound(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Bound &bound);

    flatbuffers::Offset<Halide::Serialize::StorageDim> serialize_storage_dim(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::StorageDim &storage_dim);

    flatbuffers::Offset<Halide::Serialize::LoopLevel> serialize_loop_level(flatbuffers::FlatBufferBuilder &builder, const Halide::LoopLevel &loop_level);

    flatbuffers::Offset<Halide::Serialize::FuncSchedule> serialize_func_schedule(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::FuncSchedule &func_schedule);

    flatbuffers::Offset<Halide::Serialize::Specialization> serialize_specialization(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Specialization &specialization);

    flatbuffers::Offset<Halide::Serialize::Definition> serialize_definition(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Definition &definition);

    flatbuffers::Offset<Halide::Serialize::ReductionVariable> serialize_reduction_variable(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::ReductionVariable &reduction_variable);

    flatbuffers::Offset<Halide::Serialize::ReductionDomain> serialize_reduction_domain(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::ReductionDomain &reduction_domain);

    flatbuffers::Offset<Halide::Serialize::ModulusRemainder> serialize_modulus_remainder(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::ModulusRemainder &modulus_remainder);

    flatbuffers::Offset<Halide::Serialize::PrefetchDirective> serialize_prefetch_directive(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::PrefetchDirective &prefetch_directive);

    flatbuffers::Offset<Halide::Serialize::Split> serialize_split(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Split &split);

    flatbuffers::Offset<Halide::Serialize::Dim> serialize_dim(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Dim &dim);

    flatbuffers::Offset<Halide::Serialize::FuseLoopLevel> serialize_fuse_loop_level(flatbuffers::FlatBufferBuilder &builder, const Halide::FuseLoopLevel &fuse_loop_level);

    flatbuffers::Offset<Halide::Serialize::FusedPair> serialize_fused_pair(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::FusedPair &fused_pair);

    flatbuffers::Offset<Halide::Serialize::StageSchedule> serialize_stage_schedule(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::StageSchedule &stage_schedule);

    flatbuffers::Offset<Halide::Serialize::BufferConstraint> serialize_buffer_constraint(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::BufferConstraint &buffer_constraint);

    flatbuffers::Offset<Halide::Serialize::Parameter> serialize_parameter(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Parameter &parameter);

    void build_function_mappings(const std::map<std::string, Halide::Internal::Function> &env);
};

#endif
