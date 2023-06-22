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
    std::map<int32_t, Halide::Internal::FunctionPtr> reverse_function_mappings;

    // helper functions to deserialize each type of object
    Halide::MemoryType deserialize_memory_type(const Halide::Serialize::MemoryType memory_type);

    Halide::Internal::ForType deserialize_for_type(const Halide::Serialize::ForType for_type);

    Halide::DeviceAPI deserialize_device_api(const Halide::Serialize::DeviceAPI device_api);

    Halide::Internal::Call::CallType deserialize_call_type(const Halide::Serialize::CallType call_type);

    Halide::Internal::VectorReduce::Operator deserialize_vector_reduce_op(const Halide::Serialize::VectorReduceOp vector_reduce_op);

    Halide::PrefetchBoundStrategy deserialize_prefetch_bound_strategy(const Halide::Serialize::PrefetchBoundStrategy prefetch_bound_strategy);

    Halide::NameMangling deserialize_name_mangling(const Halide::Serialize::NameMangling name_mangling);

    Halide::TailStrategy deserialize_tail_strategy(const Halide::Serialize::TailStrategy tail_strategy);

    Halide::Internal::Split::SplitType deserialize_split_type(const Halide::Serialize::SplitType split_type);

    Halide::Internal::DimType deserialize_dim_type(const Halide::Serialize::DimType dim_type);

    Halide::LoopAlignStrategy deserialize_loop_align_strategy(const Halide::Serialize::LoopAlignStrategy loop_align_strategy);

    Halide::ExternFuncArgument::ArgType deserialize_extern_func_argument_type(const Halide::Serialize::ExternFuncArgumentType extern_func_argument_type);

    std::string deserialize_string(const flatbuffers::String *str);

    Halide::Type deserialize_type(const Halide::Serialize::Type *type);

    void deserialize_function(const Halide::Serialize::Func *function, Halide::Internal::Function &hl_function);

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

    Halide::Internal::ReductionVariable deserialize_reduction_variable(const Halide::Serialize::ReductionVariable *reduction_variable);

    Halide::Internal::ReductionDomain deserialize_reduction_domain(const Halide::Serialize::ReductionDomain *reduction_domain);

    Halide::Internal::ModulusRemainder deserialize_modulus_remainder(const Halide::Serialize::ModulusRemainder *modulus_remainder);

    Halide::Internal::PrefetchDirective deserialize_prefetch_directive(const Halide::Serialize::PrefetchDirective *prefetch_directive);

    Halide::Internal::Split deserialize_split(const Halide::Serialize::Split *split);

    Halide::Internal::Dim deserialize_dim(const Halide::Serialize::Dim *dim);

    Halide::FuseLoopLevel deserialize_fuse_loop_level(const Halide::Serialize::FuseLoopLevel *fuse_loop_level);

    Halide::Internal::FusedPair deserialize_fused_pair(const Halide::Serialize::FusedPair *fused_pair);

    Halide::Internal::StageSchedule deserialize_stage_schedule(const Halide::Serialize::StageSchedule *stage_schedule);

    Halide::Internal::BufferConstraint deserialize_buffer_constraint(const Halide::Serialize::BufferConstraint *buffer_constraint);

    Halide::Internal::Parameter deserialize_parameter(const Halide::Serialize::Parameter *parameter);

    Halide::ExternFuncArgument deserialize_extern_func_argument(const Halide::Serialize::ExternFuncArgument *extern_func_argument);

    void build_reverse_function_mappings(const std::vector<Halide::Internal::Function> &functions);
};

#endif
