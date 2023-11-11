#include "Serialization.h"

#ifdef WITH_SERIALIZATION

#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "RealizationOrder.h"
#include "Schedule.h"
#include "halide_ir_generated.h"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Halide {
namespace Internal {

using flatbuffers::FlatBufferBuilder;
using flatbuffers::Offset;
using flatbuffers::String;

class Serializer {
public:
    Serializer() = default;

    // Serialize the given pipeline into the given filename
    void serialize(const Pipeline &pipeline, const std::string &filename);

    // Serialize the given pipeline into given the data buffer
    void serialize(const Pipeline &pipeline, std::vector<uint8_t> &data);

    const std::map<std::string, Parameter> &get_external_parameters() const {
        return external_parameters;
    }

private:
    // Mapping function names to a unique integer function id
    std::map<std::string, int32_t> func_mappings;

    // A lookup table for finding parameters via their names,
    // used for preventing the same parameter being serialized multiple times
    std::map<std::string, Parameter> parameters_in_pipeline;

    // A lookup table for finding buffers via their names,
    // used for preventing the same buffer being serialized multiple times
    std::map<std::string, Buffer<>> buffers_in_pipeline;

    // A lookup table for parameters that are potentially external to the pipeline,
    // so it can later be used during deserialization to have the correct bindings.
    std::map<std::string, Parameter> external_parameters;

    Serialize::MemoryType serialize_memory_type(const MemoryType &memory_type);

    Serialize::ForType serialize_for_type(const ForType &for_type);

    Serialize::DeviceAPI serialize_device_api(const DeviceAPI &device_api);

    Serialize::Partition serialize_partition(const Partition &partition);

    Serialize::CallType serialize_call_type(const Call::CallType &call_type);

    Serialize::VectorReduceOp serialize_vector_reduce_op(const VectorReduce::Operator &vector_reduce_op);

    Serialize::PrefetchBoundStrategy serialize_prefetch_bound_strategy(const PrefetchBoundStrategy &prefetch_bound_strategy);

    Serialize::NameMangling serialize_name_mangling(const NameMangling &name_mangling);

    Serialize::TailStrategy serialize_tail_strategy(const TailStrategy &tail_strategy);

    Serialize::SplitType serialize_split_type(const Split::SplitType &split_type);

    Serialize::DimType serialize_dim_type(const DimType &dim_type);

    Serialize::LoopAlignStrategy serialize_loop_align_strategy(const LoopAlignStrategy &loop_align_strategy);

    Serialize::ExternFuncArgumentType serialize_extern_func_argument_type(const ExternFuncArgument::ArgType &extern_func_argument_type);

    Offset<String> serialize_string(FlatBufferBuilder &builder, const std::string &str);

    Offset<Serialize::Type> serialize_type(FlatBufferBuilder &builder, const Type &type);

    // Stmt and Expr are special because they are union types so need to return both the type and serialized object
    std::pair<Serialize::Stmt, Offset<void>> serialize_stmt(FlatBufferBuilder &builder, const Stmt &stmt);

    std::pair<Serialize::Expr, Offset<void>> serialize_expr(FlatBufferBuilder &builder, const Expr &expr);

    Offset<Serialize::Func> serialize_function(FlatBufferBuilder &builder, const Function &function);

    Offset<Serialize::Range> serialize_range(FlatBufferBuilder &builder, const Range &range);

    Offset<Serialize::Bound> serialize_bound(FlatBufferBuilder &builder, const Bound &bound);

    Offset<Serialize::StorageDim> serialize_storage_dim(FlatBufferBuilder &builder, const StorageDim &storage_dim);

    Offset<Serialize::LoopLevel> serialize_loop_level(FlatBufferBuilder &builder, const LoopLevel &loop_level);

    Offset<Serialize::FuncSchedule> serialize_func_schedule(FlatBufferBuilder &builder, const FuncSchedule &func_schedule);

    Offset<Serialize::Specialization> serialize_specialization(FlatBufferBuilder &builder, const Specialization &specialization);

    Offset<Serialize::Definition> serialize_definition(FlatBufferBuilder &builder, const Definition &definition);

    Offset<Serialize::ReductionVariable> serialize_reduction_variable(FlatBufferBuilder &builder, const ReductionVariable &reduction_variable);

    Offset<Serialize::ReductionDomain> serialize_reduction_domain(FlatBufferBuilder &builder, const ReductionDomain &reduction_domain);

    Offset<Serialize::ModulusRemainder> serialize_modulus_remainder(FlatBufferBuilder &builder, const ModulusRemainder &modulus_remainder);

    Offset<Serialize::PrefetchDirective> serialize_prefetch_directive(FlatBufferBuilder &builder, const PrefetchDirective &prefetch_directive);

    Offset<Serialize::Split> serialize_split(FlatBufferBuilder &builder, const Split &split);

    Offset<Serialize::Dim> serialize_dim(FlatBufferBuilder &builder, const Dim &dim);

    Offset<Serialize::FuseLoopLevel> serialize_fuse_loop_level(FlatBufferBuilder &builder, const FuseLoopLevel &fuse_loop_level);

    Offset<Serialize::FusedPair> serialize_fused_pair(FlatBufferBuilder &builder, const FusedPair &fused_pair);

    Offset<Serialize::StageSchedule> serialize_stage_schedule(FlatBufferBuilder &builder, const StageSchedule &stage_schedule);

    Offset<Serialize::BufferConstraint> serialize_buffer_constraint(FlatBufferBuilder &builder, const BufferConstraint &buffer_constraint);

    Offset<Serialize::Parameter> serialize_parameter(FlatBufferBuilder &builder, const Parameter &parameter);

    Offset<Serialize::ExternFuncArgument> serialize_extern_func_argument(FlatBufferBuilder &builder, const ExternFuncArgument &extern_func_argument);

    Offset<Serialize::Buffer> serialize_buffer(FlatBufferBuilder &builder, Buffer<> buffer);

    std::vector<Offset<Serialize::WrapperRef>> serialize_wrapper_refs(FlatBufferBuilder &builder, const std::map<std::string, FunctionPtr> &wrappers);

    void build_function_mappings(const std::map<std::string, Function> &env);
};

Serialize::MemoryType Serializer::serialize_memory_type(const MemoryType &memory_type) {
    switch (memory_type) {
    case MemoryType::Auto:
        return Serialize::MemoryType::MemoryType_Auto;
    case MemoryType::Heap:
        return Serialize::MemoryType::MemoryType_Heap;
    case MemoryType::Stack:
        return Serialize::MemoryType::MemoryType_Stack;
    case MemoryType::Register:
        return Serialize::MemoryType::MemoryType_Register;
    case MemoryType::GPUShared:
        return Serialize::MemoryType::MemoryType_GPUShared;
    case MemoryType::GPUTexture:
        return Serialize::MemoryType::MemoryType_GPUTexture;
    case MemoryType::LockedCache:
        return Serialize::MemoryType::MemoryType_LockedCache;
    case MemoryType::VTCM:
        return Serialize::MemoryType::MemoryType_VTCM;
    case MemoryType::AMXTile:
        return Serialize::MemoryType::MemoryType_AMXTile;
    default:
        user_error << "Unsupported memory type\n";
        return Serialize::MemoryType_Auto;
    }
}

Serialize::ForType Serializer::serialize_for_type(const ForType &for_type) {
    switch (for_type) {
    case ForType::Serial:
        return Serialize::ForType::ForType_Serial;
    case ForType::Parallel:
        return Serialize::ForType::ForType_Parallel;
    case ForType::Vectorized:
        return Serialize::ForType::ForType_Vectorized;
    case ForType::Unrolled:
        return Serialize::ForType::ForType_Unrolled;
    case ForType::Extern:
        return Serialize::ForType::ForType_Extern;
    case ForType::GPUBlock:
        return Serialize::ForType::ForType_GPUBlock;
    case ForType::GPUThread:
        return Serialize::ForType::ForType_GPUThread;
    case ForType::GPULane:
        return Serialize::ForType::ForType_GPULane;
    default:
        user_error << "Unsupported for type\n";
        return Serialize::ForType_Serial;
    }
}

Serialize::Partition Serializer::serialize_partition(const Partition &partition) {
    switch (partition) {
    case Halide::Partition::Auto:
        return Serialize::Partition::Partition_Auto;
    case Halide::Partition::Never:
        return Serialize::Partition::Partition_Never;
    case Halide::Partition::Always:
        return Serialize::Partition::Partition_Always;
    default:
        user_error << "Unsupported loop partition policy\n";
        return Serialize::Partition::Partition_Auto;
    }
}

Serialize::DeviceAPI Serializer::serialize_device_api(const DeviceAPI &device_api) {
    switch (device_api) {
    case DeviceAPI::None:
        return Serialize::DeviceAPI::DeviceAPI_None;
    case DeviceAPI::Host:
        return Serialize::DeviceAPI::DeviceAPI_Host;
    case DeviceAPI::Default_GPU:
        return Serialize::DeviceAPI::DeviceAPI_Default_GPU;
    case DeviceAPI::CUDA:
        return Serialize::DeviceAPI::DeviceAPI_CUDA;
    case DeviceAPI::OpenCL:
        return Serialize::DeviceAPI::DeviceAPI_OpenCL;
    case DeviceAPI::OpenGLCompute:
        return Serialize::DeviceAPI::DeviceAPI_OpenGLCompute;
    case DeviceAPI::Metal:
        return Serialize::DeviceAPI::DeviceAPI_Metal;
    case DeviceAPI::Hexagon:
        return Serialize::DeviceAPI::DeviceAPI_Hexagon;
    case DeviceAPI::HexagonDma:
        return Serialize::DeviceAPI::DeviceAPI_HexagonDma;
    case DeviceAPI::D3D12Compute:
        return Serialize::DeviceAPI::DeviceAPI_D3D12Compute;
    case DeviceAPI::Vulkan:
        return Serialize::DeviceAPI::DeviceAPI_Vulkan;
    case DeviceAPI::WebGPU:
        return Serialize::DeviceAPI::DeviceAPI_WebGPU;
    default:
        user_error << "Unsupported device API\n";
        return Serialize::DeviceAPI_None;
    }
}

Serialize::CallType Serializer::serialize_call_type(const Call::CallType &call_type) {
    switch (call_type) {
    case Call::CallType::Image:
        return Serialize::CallType::CallType_Image;
    case Call::CallType::Extern:
        return Serialize::CallType::CallType_Extern;
    case Call::CallType::ExternCPlusPlus:
        return Serialize::CallType::CallType_ExternCPlusPlus;
    case Call::CallType::PureExtern:
        return Serialize::CallType::CallType_PureExtern;
    case Call::CallType::Halide:
        return Serialize::CallType::CallType_Halide;
    case Call::CallType::Intrinsic:
        return Serialize::CallType::CallType_Intrinsic;
    case Call::CallType::PureIntrinsic:
        return Serialize::CallType::CallType_PureIntrinsic;
    default:
        user_error << "Unsupported call type\n";
        return Serialize::CallType::CallType_Image;
    }
}

Serialize::VectorReduceOp Serializer::serialize_vector_reduce_op(const VectorReduce::Operator &vector_reduce_op) {
    switch (vector_reduce_op) {
    case VectorReduce::Operator::Add:
        return Serialize::VectorReduceOp::VectorReduceOp_Add;
    case VectorReduce::Operator::SaturatingAdd:
        return Serialize::VectorReduceOp::VectorReduceOp_SaturatingAdd;
    case VectorReduce::Operator::Mul:
        return Serialize::VectorReduceOp::VectorReduceOp_Mul;
    case VectorReduce::Operator::Min:
        return Serialize::VectorReduceOp::VectorReduceOp_Min;
    case VectorReduce::Operator::Max:
        return Serialize::VectorReduceOp::VectorReduceOp_Max;
    case VectorReduce::Operator::And:
        return Serialize::VectorReduceOp::VectorReduceOp_And;
    case VectorReduce::Operator::Or:
        return Serialize::VectorReduceOp::VectorReduceOp_Or;
    default:
        user_error << "Unsupported vector reduce op\n";
        return Serialize::VectorReduceOp::VectorReduceOp_Add;
    }
}

Serialize::PrefetchBoundStrategy Serializer::serialize_prefetch_bound_strategy(const PrefetchBoundStrategy &prefetch_bound_strategy) {
    switch (prefetch_bound_strategy) {
    case PrefetchBoundStrategy::Clamp:
        return Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp;
    case PrefetchBoundStrategy::GuardWithIf:
        return Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_GuardWithIf;
    case PrefetchBoundStrategy::NonFaulting:
        return Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_NonFaulting;
    default:
        user_error << "Unsupported prefetch bound strategy\n";
        return Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp;
    }
}

Serialize::NameMangling Serializer::serialize_name_mangling(const NameMangling &name_mangling) {
    switch (name_mangling) {
    case NameMangling::Default:
        return Serialize::NameMangling::NameMangling_Default;
    case NameMangling::C:
        return Serialize::NameMangling::NameMangling_C;
    case NameMangling::CPlusPlus:
        return Serialize::NameMangling::NameMangling_CPlusPlus;
    default:
        user_error << "Unsupported name mangling\n";
        return Serialize::NameMangling::NameMangling_Default;
    }
}

Serialize::TailStrategy Serializer::serialize_tail_strategy(const TailStrategy &tail_strategy) {
    switch (tail_strategy) {
    case TailStrategy::RoundUp:
        return Serialize::TailStrategy::TailStrategy_RoundUp;
    case TailStrategy::GuardWithIf:
        return Serialize::TailStrategy::TailStrategy_GuardWithIf;
    case TailStrategy::Predicate:
        return Serialize::TailStrategy::TailStrategy_Predicate;
    case TailStrategy::PredicateLoads:
        return Serialize::TailStrategy::TailStrategy_PredicateLoads;
    case TailStrategy::PredicateStores:
        return Serialize::TailStrategy::TailStrategy_PredicateStores;
    case TailStrategy::ShiftInwards:
        return Serialize::TailStrategy::TailStrategy_ShiftInwards;
    case TailStrategy::Auto:
        return Serialize::TailStrategy::TailStrategy_Auto;
    default:
        user_error << "Unsupported tail strategy\n";
        return Serialize::TailStrategy::TailStrategy_RoundUp;
    }
}

Serialize::SplitType Serializer::serialize_split_type(const Split::SplitType &split_type) {
    switch (split_type) {
    case Split::SplitType::SplitVar:
        return Serialize::SplitType::SplitType_SplitVar;
    case Split::SplitType::RenameVar:
        return Serialize::SplitType::SplitType_RenameVar;
    case Split::SplitType::FuseVars:
        return Serialize::SplitType::SplitType_FuseVars;
    case Split::SplitType::PurifyRVar:
        return Serialize::SplitType::SplitType_PurifyRVar;
    default:
        user_error << "Unsupported split type\n";
        return Serialize::SplitType::SplitType_SplitVar;
    }
}

Serialize::DimType Serializer::serialize_dim_type(const DimType &dim_type) {
    switch (dim_type) {
    case DimType::PureVar:
        return Serialize::DimType::DimType_PureVar;
    case DimType::PureRVar:
        return Serialize::DimType::DimType_PureRVar;
    case DimType::ImpureRVar:
        return Serialize::DimType::DimType_ImpureRVar;
    default:
        user_error << "Unsupported dim type\n";
        return Serialize::DimType::DimType_PureVar;
    }
}

Serialize::LoopAlignStrategy Serializer::serialize_loop_align_strategy(const LoopAlignStrategy &loop_align_strategy) {
    switch (loop_align_strategy) {
    case LoopAlignStrategy::AlignStart:
        return Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignStart;
    case LoopAlignStrategy::AlignEnd:
        return Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignEnd;
    case LoopAlignStrategy::NoAlign:
        return Serialize::LoopAlignStrategy::LoopAlignStrategy_NoAlign;
    case LoopAlignStrategy::Auto:
        return Serialize::LoopAlignStrategy::LoopAlignStrategy_Auto;
    default:
        user_error << "Unsupported loop align strategy\n";
        return Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignStart;
    }
}

Serialize::ExternFuncArgumentType Serializer::serialize_extern_func_argument_type(const ExternFuncArgument::ArgType &extern_func_argument_type) {
    switch (extern_func_argument_type) {
    case ExternFuncArgument::ArgType::UndefinedArg:
        return Serialize::ExternFuncArgumentType::ExternFuncArgumentType_UndefinedArg;
    case ExternFuncArgument::ArgType::FuncArg:
        return Serialize::ExternFuncArgumentType::ExternFuncArgumentType_FuncArg;
    case ExternFuncArgument::ArgType::BufferArg:
        return Serialize::ExternFuncArgumentType::ExternFuncArgumentType_BufferArg;
    case ExternFuncArgument::ArgType::ExprArg:
        return Serialize::ExternFuncArgumentType::ExternFuncArgumentType_ExprArg;
    case ExternFuncArgument::ArgType::ImageParamArg:
        return Serialize::ExternFuncArgumentType::ExternFuncArgumentType_ImageParamArg;
    default:
        user_error << "Unsupported extern func argument type\n";
        return Serialize::ExternFuncArgumentType::ExternFuncArgumentType_UndefinedArg;
    }
}

Offset<String> Serializer::serialize_string(FlatBufferBuilder &builder, const std::string &str) {
    return builder.CreateString(str);
}

Offset<Serialize::Type> Serializer::serialize_type(FlatBufferBuilder &builder, const Type &type) {
    const int bits = type.bits();
    const int lanes = type.lanes();
    halide_type_code_t code = type.code();
    const auto code_serialized = Serialize::TypeCode(code);
    return Serialize::CreateType(builder, code_serialized, bits, lanes);
}

std::pair<Serialize::Stmt, Offset<void>> Serializer::serialize_stmt(FlatBufferBuilder &builder, const Stmt &stmt) {
    if (!stmt.defined()) {
        return std::make_pair(Serialize::Stmt::Stmt_UndefinedStmt, Serialize::CreateUndefinedStmt(builder).Union());
    }
    switch (stmt->node_type) {
    case IRNodeType::LetStmt: {
        const auto *const let_stmt = stmt.as<LetStmt>();
        const auto name_serialized = serialize_string(builder, let_stmt->name);
        const auto value_serialized = serialize_expr(builder, let_stmt->value);
        const auto body_serialized = serialize_stmt(builder, let_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_LetStmt,
                              Serialize::CreateLetStmt(builder, name_serialized,
                                                       value_serialized.first, value_serialized.second,
                                                       body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::AssertStmt: {
        const auto *const assert_stmt = stmt.as<AssertStmt>();
        const auto condition_serialized = serialize_expr(builder, assert_stmt->condition);
        const auto message_serialized = serialize_expr(builder, assert_stmt->message);
        return std::make_pair(Serialize::Stmt::Stmt_AssertStmt,
                              Serialize::CreateAssertStmt(builder,
                                                          condition_serialized.first, condition_serialized.second,
                                                          message_serialized.first, message_serialized.second)
                                  .Union());
    }
    case IRNodeType::ProducerConsumer: {
        const auto *const producer_consumer = stmt.as<ProducerConsumer>();
        const auto name_serialized = serialize_string(builder, producer_consumer->name);
        const auto body_serialized = serialize_stmt(builder, producer_consumer->body);
        return std::make_pair(Serialize::Stmt::Stmt_ProducerConsumer,
                              Serialize::CreateProducerConsumer(builder, name_serialized,
                                                                producer_consumer->is_producer,
                                                                body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::For: {
        const auto *const for_stmt = stmt.as<For>();
        const auto name_serialized = serialize_string(builder, for_stmt->name);
        const auto min_serialized = serialize_expr(builder, for_stmt->min);
        const auto extent_serialized = serialize_expr(builder, for_stmt->extent);
        const Serialize::ForType for_type = serialize_for_type(for_stmt->for_type);
        const Serialize::Partition partition_policy = serialize_partition(for_stmt->partition_policy);
        const Serialize::DeviceAPI device_api = serialize_device_api(for_stmt->device_api);
        const auto body_serialized = serialize_stmt(builder, for_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_For,
                              Serialize::CreateFor(builder, name_serialized,
                                                   min_serialized.first, min_serialized.second,
                                                   extent_serialized.first, extent_serialized.second,
                                                   for_type, partition_policy, device_api,
                                                   body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::Store: {
        const auto *const store_stmt = stmt.as<Store>();
        const auto name_serialized = serialize_string(builder, store_stmt->name);
        const auto predicate_serialized = serialize_expr(builder, store_stmt->predicate);
        const auto value_serialized = serialize_expr(builder, store_stmt->value);
        const auto index_serialized = serialize_expr(builder, store_stmt->index);
        const std::string param_name = store_stmt->param.defined() ? store_stmt->param.name() : "";
        if (store_stmt->param.defined() && parameters_in_pipeline.find(param_name) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[param_name] = store_stmt->param;
        }
        const auto param_name_serialized = serialize_string(builder, param_name);
        const auto alignment_serialized = serialize_modulus_remainder(builder, store_stmt->alignment);
        return std::make_pair(Serialize::Stmt::Stmt_Store,
                              Serialize::CreateStore(builder, name_serialized,
                                                     predicate_serialized.first, predicate_serialized.second,
                                                     value_serialized.first, value_serialized.second,
                                                     index_serialized.first, index_serialized.second,
                                                     param_name_serialized, alignment_serialized)
                                  .Union());
    }
    case IRNodeType::Provide: {
        const auto *const provide_stmt = stmt.as<Provide>();
        const auto name_serialized = serialize_string(builder, provide_stmt->name);
        const auto values = provide_stmt->values;
        std::vector<uint8_t> values_types;
        values_types.reserve(values.size());
        std::vector<Offset<void>> values_serialized;
        values_serialized.reserve(values.size());
        for (const auto &value : values) {
            auto value_serialized = serialize_expr(builder, value);
            values_types.push_back(value_serialized.first);
            values_serialized.push_back(value_serialized.second);
        }
        const auto args = provide_stmt->args;
        std::vector<uint8_t> args_types;
        args_types.reserve(args.size());
        std::vector<Offset<void>> args_serialized;
        args_serialized.reserve(args.size());
        for (const auto &arg : args) {
            auto arg_serialized = serialize_expr(builder, arg);
            args_types.push_back(arg_serialized.first);
            args_serialized.push_back(arg_serialized.second);
        }
        const auto predicate_serialized = serialize_expr(builder, provide_stmt->predicate);
        return std::make_pair(Serialize::Stmt::Stmt_Provide,
                              Serialize::CreateProvide(builder, name_serialized,
                                                       builder.CreateVector(values_types),
                                                       builder.CreateVector(values_serialized),
                                                       builder.CreateVector(args_types),
                                                       builder.CreateVector(args_serialized),
                                                       predicate_serialized.first, predicate_serialized.second)
                                  .Union());
    }
    case IRNodeType::Allocate: {
        const auto *const allocate_stmt = stmt.as<Allocate>();
        const auto name_serialized = serialize_string(builder, allocate_stmt->name);
        const auto type_serialized = serialize_type(builder, allocate_stmt->type);
        const Serialize::MemoryType memory_type = serialize_memory_type(allocate_stmt->memory_type);
        const auto extents = allocate_stmt->extents;
        std::vector<uint8_t> extents_types;
        extents_types.reserve(extents.size());
        std::vector<Offset<void>> extents_serialized;
        extents_serialized.reserve(extents.size());
        for (const auto &extent : extents) {
            auto extent_serialized = serialize_expr(builder, extent);
            extents_types.push_back(extent_serialized.first);
            extents_serialized.push_back(extent_serialized.second);
        }
        const auto condition_serialized = serialize_expr(builder, allocate_stmt->condition);
        const auto new_expr_serialized = serialize_expr(builder, allocate_stmt->new_expr);
        const auto free_function_serialized = serialize_string(builder, allocate_stmt->free_function);
        const auto padding = allocate_stmt->padding;
        const auto body_serialized = serialize_stmt(builder, allocate_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_Allocate,
                              Serialize::CreateAllocate(builder, name_serialized,
                                                        type_serialized, memory_type,
                                                        builder.CreateVector(extents_types),
                                                        builder.CreateVector(extents_serialized),
                                                        condition_serialized.first, condition_serialized.second,
                                                        new_expr_serialized.first, new_expr_serialized.second,
                                                        free_function_serialized, padding,
                                                        body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::Free: {
        const auto *const free_stmt = stmt.as<Free>();
        const auto name_serialized = serialize_string(builder, free_stmt->name);
        return std::make_pair(Serialize::Stmt::Stmt_Free, Serialize::CreateFree(builder, name_serialized).Union());
    }
    case IRNodeType::Realize: {
        const auto *const realize_stmt = stmt.as<Realize>();
        const auto name_serialized = serialize_string(builder, realize_stmt->name);
        const auto types = realize_stmt->types;
        std::vector<Offset<Serialize::Type>> types_serialized;
        types_serialized.reserve(types.size());
        for (const auto &type : types) {
            types_serialized.push_back(serialize_type(builder, type));
        }
        const Serialize::MemoryType memory_type = serialize_memory_type(realize_stmt->memory_type);
        const auto bounds = realize_stmt->bounds;
        std::vector<Offset<Serialize::Range>> bounds_serialized;
        bounds_serialized.reserve(bounds.size());
        for (const auto &bound : bounds) {
            bounds_serialized.push_back(serialize_range(builder, bound));
        }
        const auto condition_serialized = serialize_expr(builder, realize_stmt->condition);
        const auto body_serialized = serialize_stmt(builder, realize_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_Realize,
                              Serialize::CreateRealize(builder, name_serialized,
                                                       builder.CreateVector(types_serialized),
                                                       memory_type,
                                                       builder.CreateVector(bounds_serialized),
                                                       condition_serialized.first, condition_serialized.second,
                                                       body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::Block: {
        const auto *const block_stmt = stmt.as<Block>();
        const auto first_serialized = serialize_stmt(builder, block_stmt->first);
        const auto rest_serialized = serialize_stmt(builder, block_stmt->rest);
        return std::make_pair(Serialize::Stmt::Stmt_Block,
                              Serialize::CreateBlock(builder,
                                                     first_serialized.first, first_serialized.second,
                                                     rest_serialized.first, rest_serialized.second)
                                  .Union());
    }
    case IRNodeType::IfThenElse: {
        const auto *const if_then_else_stmt = stmt.as<IfThenElse>();
        const auto condition_serialized = serialize_expr(builder, if_then_else_stmt->condition);
        const auto then_case_serialized = serialize_stmt(builder, if_then_else_stmt->then_case);
        const auto else_case_serialized = serialize_stmt(builder, if_then_else_stmt->else_case);
        return std::make_pair(Serialize::Stmt::Stmt_IfThenElse,
                              Serialize::CreateIfThenElse(builder,
                                                          condition_serialized.first, condition_serialized.second,
                                                          then_case_serialized.first, then_case_serialized.second,
                                                          else_case_serialized.first, else_case_serialized.second)
                                  .Union());
    }
    case IRNodeType::Evaluate: {
        const auto *const evaluate_stmt = stmt.as<Evaluate>();
        const auto value_serialized = serialize_expr(builder, evaluate_stmt->value);
        return std::make_pair(Serialize::Stmt::Stmt_Evaluate,
                              Serialize::CreateEvaluate(builder, value_serialized.first, value_serialized.second).Union());
    }
    case IRNodeType::Prefetch: {
        const auto *const prefetch_stmt = stmt.as<Prefetch>();
        const auto name_serialized = serialize_string(builder, prefetch_stmt->name);
        const auto types = prefetch_stmt->types;
        std::vector<Offset<Serialize::Type>> types_serialized;
        types_serialized.reserve(types.size());
        for (const auto &type : types) {
            types_serialized.push_back(serialize_type(builder, type));
        }
        const auto types_vector = builder.CreateVector(types_serialized);
        const auto bounds = prefetch_stmt->bounds;
        std::vector<Offset<Serialize::Range>> bounds_serialized;
        bounds_serialized.reserve(bounds.size());
        for (const auto &bound : bounds) {
            bounds_serialized.push_back(serialize_range(builder, bound));
        }
        const auto prefetch_serialized = serialize_prefetch_directive(builder, prefetch_stmt->prefetch);
        const auto condition_serialized = serialize_expr(builder, prefetch_stmt->condition);
        const auto body_serialized = serialize_stmt(builder, prefetch_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_Prefetch,
                              Serialize::CreatePrefetch(builder, name_serialized, types_vector,
                                                        builder.CreateVector(bounds_serialized),
                                                        prefetch_serialized,
                                                        condition_serialized.first, condition_serialized.second,
                                                        body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::Acquire: {
        const auto *const acquire_stmt = stmt.as<Acquire>();
        const auto semaphore_serialized = serialize_expr(builder, acquire_stmt->semaphore);
        const auto count_serialized = serialize_expr(builder, acquire_stmt->count);
        const auto body_serialized = serialize_stmt(builder, acquire_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_Acquire,
                              Serialize::CreateAcquire(builder,
                                                       semaphore_serialized.first, semaphore_serialized.second,
                                                       count_serialized.first, count_serialized.second,
                                                       body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::Fork: {
        const auto *const fork_stmt = stmt.as<Fork>();
        const auto first_serialized = serialize_stmt(builder, fork_stmt->first);
        const auto rest_serialized = serialize_stmt(builder, fork_stmt->rest);
        return std::make_pair(Serialize::Stmt::Stmt_Fork,
                              Serialize::CreateFork(builder,
                                                    first_serialized.first, first_serialized.second,
                                                    rest_serialized.first, rest_serialized.second)
                                  .Union());
    }
    case IRNodeType::Atomic: {
        const auto *const atomic_stmt = stmt.as<Atomic>();
        const auto producer_name_serialized = serialize_string(builder, atomic_stmt->producer_name);
        const auto mutex_name_serialized = serialize_string(builder, atomic_stmt->mutex_name);
        const auto body_serialized = serialize_stmt(builder, atomic_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_Atomic,
                              Serialize::CreateAtomic(builder, producer_name_serialized, mutex_name_serialized,
                                                      body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::HoistedStorage: {
        const auto *const hoisted_storage_stmt = stmt.as<HoistedStorage>();
        const auto name_serialized = serialize_string(builder, hoisted_storage_stmt->name);
        const auto body_serialized = serialize_stmt(builder, hoisted_storage_stmt->body);
        return std::make_pair(Serialize::Stmt::Stmt_HoistedStorage,
                              Serialize::CreateHoistedStorage(builder, name_serialized,
                                                              body_serialized.first, body_serialized.second)
                                  .Union());
    }
    default:
        user_error << "Unsupported stmt type\n";
        return std::make_pair(Serialize::Stmt::Stmt_UndefinedStmt, Serialize::CreateUndefinedStmt(builder).Union());
    }
}

std::pair<Serialize::Expr, Offset<void>> Serializer::serialize_expr(FlatBufferBuilder &builder, const Expr &expr) {
    if (!expr.defined()) {
        return std::make_pair(Serialize::Expr::Expr_UndefinedExpr, Serialize::CreateUndefinedExpr(builder).Union());
    }
    switch (expr->node_type) {
    case IRNodeType::IntImm: {
        const auto *const int_imm = expr.as<IntImm>();
        const auto type_serialized = serialize_type(builder, int_imm->type);
        return std::make_pair(Serialize::Expr::Expr_IntImm, Serialize::CreateIntImm(builder, int_imm->value, type_serialized).Union());
    }
    case IRNodeType::UIntImm: {
        const auto *const uint_imm = expr.as<UIntImm>();
        const auto type_serialized = serialize_type(builder, uint_imm->type);
        return std::make_pair(Serialize::Expr::Expr_UIntImm, Serialize::CreateUIntImm(builder, uint_imm->value, type_serialized).Union());
    }
    case IRNodeType::FloatImm: {
        const auto *const float_imm = expr.as<FloatImm>();
        const auto type_serialized = serialize_type(builder, float_imm->type);
        return std::make_pair(Serialize::Expr::Expr_FloatImm, Serialize::CreateFloatImm(builder, float_imm->value, type_serialized).Union());
    }
    case IRNodeType::StringImm: {
        const auto *const string_imm = expr.as<StringImm>();
        const auto value_serialized = serialize_string(builder, string_imm->value);
        return std::make_pair(Serialize::Expr::Expr_StringImm, Serialize::CreateStringImm(builder, value_serialized).Union());
    }
    case IRNodeType::Cast: {
        const auto *const cast_expr = expr.as<Cast>();
        const auto value_serialized = serialize_expr(builder, cast_expr->value);
        const auto type_serialized = serialize_type(builder, cast_expr->type);
        return std::make_pair(Serialize::Expr::Expr_Cast, Serialize::CreateCast(builder, value_serialized.first, value_serialized.second, type_serialized).Union());
    }
    case IRNodeType::Reinterpret: {
        const auto *const reinterpret_expr = expr.as<Reinterpret>();
        const auto value_serialized = serialize_expr(builder, reinterpret_expr->value);
        const auto type_serialized = serialize_type(builder, reinterpret_expr->type);
        return std::make_pair(Serialize::Expr::Expr_Reinterpret, Serialize::CreateReinterpret(builder, value_serialized.first, value_serialized.second, type_serialized).Union());
    }
    case IRNodeType::Add: {
        const auto *const add_expr = expr.as<Add>();
        const auto a_serialized = serialize_expr(builder, add_expr->a);
        const auto b_serialized = serialize_expr(builder, add_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Add, Serialize::CreateAdd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Sub: {
        const auto *const sub_expr = expr.as<Sub>();
        const auto a_serialized = serialize_expr(builder, sub_expr->a);
        const auto b_serialized = serialize_expr(builder, sub_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Sub, Serialize::CreateSub(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Mul: {
        const auto *const mul_expr = expr.as<Mul>();
        const auto a_serialized = serialize_expr(builder, mul_expr->a);
        const auto b_serialized = serialize_expr(builder, mul_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Mul, Serialize::CreateMul(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Div: {
        const auto *const div_expr = expr.as<Div>();
        const auto a_serialized = serialize_expr(builder, div_expr->a);
        const auto b_serialized = serialize_expr(builder, div_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Div, Serialize::CreateDiv(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Mod: {
        const auto *const mod_expr = expr.as<Mod>();
        const auto a_serialized = serialize_expr(builder, mod_expr->a);
        const auto b_serialized = serialize_expr(builder, mod_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Mod, Serialize::CreateMod(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Min: {
        const auto *const min_expr = expr.as<Min>();
        const auto a_serialized = serialize_expr(builder, min_expr->a);
        const auto b_serialized = serialize_expr(builder, min_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Min, Serialize::CreateMin(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Max: {
        const auto *const max_expr = expr.as<Max>();
        const auto a_serialized = serialize_expr(builder, max_expr->a);
        const auto b_serialized = serialize_expr(builder, max_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Max, Serialize::CreateMax(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::EQ: {
        const auto *const eq_expr = expr.as<EQ>();
        const auto a_serialized = serialize_expr(builder, eq_expr->a);
        const auto b_serialized = serialize_expr(builder, eq_expr->b);
        return std::make_pair(Serialize::Expr::Expr_EQ, Serialize::CreateEQ(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::NE: {
        const auto *const ne_expr = expr.as<NE>();
        const auto a_serialized = serialize_expr(builder, ne_expr->a);
        const auto b_serialized = serialize_expr(builder, ne_expr->b);
        return std::make_pair(Serialize::Expr::Expr_NE, Serialize::CreateNE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::LT: {
        const auto *const lt_expr = expr.as<LT>();
        const auto a_serialized = serialize_expr(builder, lt_expr->a);
        const auto b_serialized = serialize_expr(builder, lt_expr->b);
        return std::make_pair(Serialize::Expr::Expr_LT, Serialize::CreateLT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::LE: {
        const auto *const le_expr = expr.as<LE>();
        const auto a_serialized = serialize_expr(builder, le_expr->a);
        const auto b_serialized = serialize_expr(builder, le_expr->b);
        return std::make_pair(Serialize::Expr::Expr_LE, Serialize::CreateLE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::GT: {
        const auto *const gt_expr = expr.as<GT>();
        const auto a_serialized = serialize_expr(builder, gt_expr->a);
        const auto b_serialized = serialize_expr(builder, gt_expr->b);
        return std::make_pair(Serialize::Expr::Expr_GT, Serialize::CreateGT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::GE: {
        const auto *const ge_expr = expr.as<GE>();
        const auto a_serialized = serialize_expr(builder, ge_expr->a);
        const auto b_serialized = serialize_expr(builder, ge_expr->b);
        return std::make_pair(Serialize::Expr::Expr_GE, Serialize::CreateGE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::And: {
        const auto *const and_expr = expr.as<And>();
        const auto a_serialized = serialize_expr(builder, and_expr->a);
        const auto b_serialized = serialize_expr(builder, and_expr->b);
        return std::make_pair(Serialize::Expr::Expr_And, Serialize::CreateAnd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Or: {
        const auto *const or_expr = expr.as<Or>();
        const auto a_serialized = serialize_expr(builder, or_expr->a);
        const auto b_serialized = serialize_expr(builder, or_expr->b);
        return std::make_pair(Serialize::Expr::Expr_Or, Serialize::CreateOr(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Not: {
        const auto *const not_expr = expr.as<Not>();
        const auto a_serialized = serialize_expr(builder, not_expr->a);
        return std::make_pair(Serialize::Expr::Expr_Not, Serialize::CreateNot(builder, a_serialized.first, a_serialized.second).Union());
    }
    case IRNodeType::Select: {
        const auto *const select_expr = expr.as<Select>();
        const auto condition_serialized = serialize_expr(builder, select_expr->condition);
        const auto true_value_serialized = serialize_expr(builder, select_expr->true_value);
        const auto false_value_serialized = serialize_expr(builder, select_expr->false_value);
        return std::make_pair(Serialize::Expr::Expr_Select,
                              Serialize::CreateSelect(builder,
                                                      condition_serialized.first, condition_serialized.second,
                                                      true_value_serialized.first, true_value_serialized.second,
                                                      false_value_serialized.first, false_value_serialized.second)
                                  .Union());
    }
    case IRNodeType::Load: {
        const auto *const load_expr = expr.as<Load>();
        const auto name_serialized = serialize_string(builder, load_expr->name);
        const auto predicate_serialized = serialize_expr(builder, load_expr->predicate);
        const auto index_serialized = serialize_expr(builder, load_expr->index);
        const std::string image_name = load_expr->image.defined() ? load_expr->image.name() : "";
        if (load_expr->image.defined() && buffers_in_pipeline.find(image_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[image_name] = load_expr->image;
        }
        const auto image_name_serialized = serialize_string(builder, image_name);
        const std::string param_name = load_expr->param.defined() ? load_expr->param.name() : "";
        if (load_expr->param.defined() && parameters_in_pipeline.find(param_name) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[param_name] = load_expr->param;
        }
        const auto param_name_serialized = serialize_string(builder, param_name);
        const auto alignment_serialized = serialize_modulus_remainder(builder, load_expr->alignment);
        const auto type_serialized = serialize_type(builder, load_expr->type);
        return std::make_pair(Serialize::Expr::Expr_Load,
                              Serialize::CreateLoad(builder, name_serialized,
                                                    predicate_serialized.first, predicate_serialized.second,
                                                    index_serialized.first, index_serialized.second,
                                                    image_name_serialized, param_name_serialized,
                                                    alignment_serialized, type_serialized)
                                  .Union());
    }
    case IRNodeType::Ramp: {
        const auto *const ramp_expr = expr.as<Ramp>();
        const auto base_serialized = serialize_expr(builder, ramp_expr->base);
        const auto stride_serialized = serialize_expr(builder, ramp_expr->stride);
        const auto lanes = ramp_expr->lanes;
        return std::make_pair(Serialize::Expr::Expr_Ramp,
                              Serialize::CreateRamp(builder,
                                                    base_serialized.first, base_serialized.second,
                                                    stride_serialized.first, stride_serialized.second, lanes)
                                  .Union());
    }
    case IRNodeType::Broadcast: {
        const auto *const broadcast_expr = expr.as<Broadcast>();
        const auto value_serialized = serialize_expr(builder, broadcast_expr->value);
        const auto lanes = broadcast_expr->lanes;
        return std::make_pair(Serialize::Expr::Expr_Broadcast, Serialize::CreateBroadcast(builder, value_serialized.first, value_serialized.second, lanes).Union());
    }
    case IRNodeType::Let: {
        const auto *const let_expr = expr.as<Let>();
        const auto name_serialized = serialize_string(builder, let_expr->name);
        const auto value_serialized = serialize_expr(builder, let_expr->value);
        const auto body_serialized = serialize_expr(builder, let_expr->body);
        return std::make_pair(Serialize::Expr::Expr_Let,
                              Serialize::CreateLet(builder, name_serialized,
                                                   value_serialized.first, value_serialized.second,
                                                   body_serialized.first, body_serialized.second)
                                  .Union());
    }
    case IRNodeType::Call: {
        const auto *const call_expr = expr.as<Call>();
        const auto name_serialized = serialize_string(builder, call_expr->name);
        const auto args = call_expr->args;
        std::vector<uint8_t> args_types;
        args_types.reserve(args.size());
        std::vector<Offset<void>> args_serialized;
        args_serialized.reserve(args.size());
        for (const auto &arg : args) {
            auto arg_serialized = serialize_expr(builder, arg);
            args_types.push_back(arg_serialized.first);
            args_serialized.push_back(arg_serialized.second);
        }
        const auto call_type = serialize_call_type(call_expr->call_type);
        int func_index = -1;
        if (call_expr->func.defined() && this->func_mappings.find(Function(call_expr->func).name()) != this->func_mappings.end()) {
            func_index = this->func_mappings[Function(call_expr->func).name()];
        }
        const auto value_index = call_expr->value_index;
        const std::string image_name = call_expr->image.defined() ? call_expr->image.name() : "";
        if (call_expr->image.defined() && buffers_in_pipeline.find(image_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[image_name] = call_expr->image;
        }
        const auto image_name_serialized = serialize_string(builder, image_name);
        const std::string param_name = call_expr->param.defined() ? call_expr->param.name() : "";
        if (call_expr->param.defined() && external_parameters.find(param_name) == external_parameters.end()) {
            external_parameters[param_name] = call_expr->param;
        }
        const auto param_name_serialized = serialize_string(builder, param_name);
        const auto type_serialized = serialize_type(builder, call_expr->type);
        return std::make_pair(Serialize::Expr::Expr_Call,
                              Serialize::CreateCall(builder, name_serialized,
                                                    builder.CreateVector(args_types),
                                                    builder.CreateVector(args_serialized),
                                                    call_type, func_index, value_index,
                                                    image_name_serialized, param_name_serialized, type_serialized)
                                  .Union());
    }
    case IRNodeType::Variable: {
        const auto *const variable_expr = expr.as<Variable>();
        const auto name_serialized = serialize_string(builder, variable_expr->name);
        const auto type_serialized = serialize_type(builder, variable_expr->type);
        const std::string param_name = variable_expr->param.defined() ? variable_expr->param.name() : "";
        if (variable_expr->param.defined() && external_parameters.find(param_name) == external_parameters.end()) {
            external_parameters[param_name] = variable_expr->param;
        }
        const auto param_name_serialized = serialize_string(builder, param_name);
        const std::string image_name = variable_expr->image.defined() ? variable_expr->image.name() : "";
        if (variable_expr->image.defined() && buffers_in_pipeline.find(image_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[image_name] = variable_expr->image;
        }
        const auto image_name_serialized = serialize_string(builder, image_name);
        const auto reduction_domain_serialized = serialize_reduction_domain(builder, variable_expr->reduction_domain);
        return std::make_pair(Serialize::Expr::Expr_Variable,
                              Serialize::CreateVariable(builder, name_serialized, type_serialized,
                                                        param_name_serialized, image_name_serialized, reduction_domain_serialized)
                                  .Union());
    }
    case IRNodeType::Shuffle: {
        const auto *const shuffle_expr = expr.as<Shuffle>();
        const auto vectors = shuffle_expr->vectors;
        std::vector<uint8_t> vectors_types;
        vectors_types.reserve(vectors.size());
        std::vector<Offset<void>> vectors_serialized;
        vectors_serialized.reserve(vectors.size());
        for (const auto &vector : vectors) {
            auto vector_serialized = serialize_expr(builder, vector);
            vectors_types.push_back(vector_serialized.first);
            vectors_serialized.push_back(vector_serialized.second);
        }
        const auto indices = shuffle_expr->indices;
        return std::make_pair(Serialize::Expr::Expr_Shuffle,
                              Serialize::CreateShuffle(builder,
                                                       builder.CreateVector(vectors_types),
                                                       builder.CreateVector(vectors_serialized),
                                                       builder.CreateVector(indices))
                                  .Union());
    }
    case IRNodeType::VectorReduce: {
        const auto *const vector_reduce_expr = expr.as<VectorReduce>();
        const auto value_serialized = serialize_expr(builder, vector_reduce_expr->value);
        const auto reduction_op_serialized = serialize_vector_reduce_op(vector_reduce_expr->op);
        const int lanes = vector_reduce_expr->type.lanes();
        return std::make_pair(Serialize::Expr::Expr_VectorReduce,
                              Serialize::CreateVectorReduce(builder,
                                                            value_serialized.first, value_serialized.second,
                                                            reduction_op_serialized, lanes)
                                  .Union());
    }
    default:
        user_error << "Unsupported Expr type\n";
        return std::make_pair(Serialize::Expr::Expr_UndefinedExpr, Serialize::CreateUndefinedExpr(builder).Union());
    }
}

Offset<Serialize::Func> Serializer::serialize_function(FlatBufferBuilder &builder, const Function &function) {
    const auto name_serialized = serialize_string(builder, function.name());

    const auto origin_name_serialized = serialize_string(builder, function.origin_name());

    const std::vector<Type> &output_types = function.output_types();
    std::vector<Offset<Serialize::Type>> output_types_serialized;
    output_types_serialized.reserve(output_types.size());
    for (const auto &type : output_types) {
        output_types_serialized.push_back(serialize_type(builder, type));
    }

    const std::vector<Type> &required_types = function.required_types();
    std::vector<Offset<Serialize::Type>> required_types_serialized;
    required_types_serialized.reserve(required_types.size());
    for (const auto &type : required_types) {
        required_types_serialized.push_back(serialize_type(builder, type));
    }
    const int required_dim = function.required_dimensions();
    const std::vector<std::string> &args = function.args();
    std::vector<Offset<String>> args_serialized;
    args_serialized.reserve(args.size());
    for (const auto &arg : args) {
        args_serialized.push_back(serialize_string(builder, arg));
    }
    const auto func_schedule_serialized = serialize_func_schedule(builder, function.schedule());
    const auto init_def_serialized = serialize_definition(builder, function.definition());
    std::vector<Offset<Serialize::Definition>> updates_serialized;
    for (const auto &update : function.updates()) {
        updates_serialized.push_back(serialize_definition(builder, update));
    }
    const auto debug_file_serialized = serialize_string(builder, function.debug_file());

    std::vector<Offset<String>> output_buffers_names_serialized;
    output_buffers_names_serialized.reserve(function.output_buffers().size());
    for (const auto &output_buffer : function.output_buffers()) {
        std::string output_buffer_name = output_buffer.defined() ? output_buffer.name() : "";
        if (output_buffer.defined() && parameters_in_pipeline.find(output_buffer_name) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[output_buffer_name] = output_buffer;
        }
        output_buffers_names_serialized.push_back(serialize_string(builder, output_buffer_name));
    }
    std::vector<Offset<Serialize::ExternFuncArgument>> extern_arguments_serialized;
    extern_arguments_serialized.reserve(function.extern_arguments().size());
    for (const auto &extern_argument : function.extern_arguments()) {
        extern_arguments_serialized.push_back(serialize_extern_func_argument(builder, extern_argument));
    }

    const auto extern_function_name_serialized = serialize_string(builder, function.extern_function_name());
    const auto extern_mangling_serialized = serialize_name_mangling(function.extern_definition_name_mangling());
    const auto extern_function_device_api_serialized = serialize_device_api(function.extern_function_device_api());
    const auto extern_proxy_expr_serialized = serialize_expr(builder, function.extern_definition_proxy_expr());
    const bool trace_loads = function.is_tracing_loads();
    const bool trace_stores = function.is_tracing_stores();
    const bool trace_realizations = function.is_tracing_realizations();
    std::vector<Offset<String>> trace_tags_serialized;
    trace_tags_serialized.reserve(function.get_trace_tags().size());
    for (const auto &tag : function.get_trace_tags()) {
        trace_tags_serialized.push_back(serialize_string(builder, tag));
    }
    const bool frozen = function.frozen();
    auto func = Serialize::CreateFunc(builder,
                                      name_serialized,
                                      origin_name_serialized,
                                      builder.CreateVector(output_types_serialized),
                                      builder.CreateVector(required_types_serialized),
                                      required_dim,
                                      builder.CreateVector(args_serialized),
                                      func_schedule_serialized,
                                      init_def_serialized,
                                      builder.CreateVector(updates_serialized),
                                      debug_file_serialized,
                                      builder.CreateVector(output_buffers_names_serialized),
                                      builder.CreateVector(extern_arguments_serialized),
                                      extern_function_name_serialized,
                                      extern_mangling_serialized,
                                      extern_function_device_api_serialized,
                                      extern_proxy_expr_serialized.first, extern_proxy_expr_serialized.second,
                                      trace_loads,
                                      trace_stores,
                                      trace_realizations,
                                      builder.CreateVector(trace_tags_serialized), frozen);
    return func;
}

Offset<Serialize::Range> Serializer::serialize_range(FlatBufferBuilder &builder, const Range &range) {
    const auto min_serialized = serialize_expr(builder, range.min);
    const auto extent_serialized = serialize_expr(builder, range.extent);
    return Serialize::CreateRange(builder, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second);
}

Offset<Serialize::Bound> Serializer::serialize_bound(FlatBufferBuilder &builder, const Bound &bound) {
    const auto var_serialized = serialize_string(builder, bound.var);
    const auto min_serialized = serialize_expr(builder, bound.min);
    const auto extent_serialized = serialize_expr(builder, bound.extent);
    const auto modulus_serialized = serialize_expr(builder, bound.modulus);
    const auto remainder_serialized = serialize_expr(builder, bound.remainder);
    return Serialize::CreateBound(builder, var_serialized,
                                  min_serialized.first, min_serialized.second,
                                  extent_serialized.first, extent_serialized.second,
                                  modulus_serialized.first, modulus_serialized.second,
                                  remainder_serialized.first, remainder_serialized.second);
}

Offset<Serialize::StorageDim> Serializer::serialize_storage_dim(FlatBufferBuilder &builder, const StorageDim &storage_dim) {
    const auto var_serialized = serialize_string(builder, storage_dim.var);
    const auto alignment_serialized = serialize_expr(builder, storage_dim.alignment);
    const auto bound_serialized = serialize_expr(builder, storage_dim.bound);
    const auto fold_factor_serialized = serialize_expr(builder, storage_dim.fold_factor);
    const auto fold_forward = storage_dim.fold_forward;
    return Serialize::CreateStorageDim(builder, var_serialized,
                                       alignment_serialized.first, alignment_serialized.second,
                                       bound_serialized.first, bound_serialized.second,
                                       fold_factor_serialized.first, fold_factor_serialized.second,
                                       fold_forward);
}

Offset<Serialize::LoopLevel> Serializer::serialize_loop_level(FlatBufferBuilder &builder, const LoopLevel &loop_level) {
    const auto func_name_serialized = serialize_string(builder, loop_level.func_name());
    const auto stage_index = loop_level.get_stage_index();
    const auto var_name_serialized = serialize_string(builder, loop_level.var_name());
    const bool is_rvar = loop_level.is_rvar();
    const auto locked = loop_level.locked();
    return Serialize::CreateLoopLevel(builder, func_name_serialized, stage_index, var_name_serialized, is_rvar, locked);
}

Offset<Serialize::FuncSchedule> Serializer::serialize_func_schedule(FlatBufferBuilder &builder, const FuncSchedule &func_schedule) {
    const auto store_level_serialized = serialize_loop_level(builder, func_schedule.store_level());
    const auto compute_level_serialized = serialize_loop_level(builder, func_schedule.compute_level());
    const auto hoist_storage_level_serialized = serialize_loop_level(builder, func_schedule.hoist_storage_level());
    std::vector<Offset<Serialize::StorageDim>> storage_dims_serialized;
    for (const auto &storage_dim : func_schedule.storage_dims()) {
        storage_dims_serialized.push_back(serialize_storage_dim(builder, storage_dim));
    }
    std::vector<Offset<Serialize::Bound>> bounds_serialized;
    for (const auto &bound : func_schedule.bounds()) {
        bounds_serialized.push_back(serialize_bound(builder, bound));
    }
    std::vector<Offset<Serialize::Bound>> estimates_serialized;
    for (const auto &estimate : func_schedule.estimates()) {
        estimates_serialized.push_back(serialize_bound(builder, estimate));
    }
    std::vector<Offset<Serialize::WrapperRef>> wrappers_serialized = serialize_wrapper_refs(builder, func_schedule.wrappers());
    const Serialize::MemoryType memory_type = serialize_memory_type(func_schedule.memory_type());
    const auto memoized = func_schedule.memoized();
    const auto async = func_schedule.async();
    const auto memoize_eviction_key_serialized = serialize_expr(builder, func_schedule.memoize_eviction_key());
    return Serialize::CreateFuncSchedule(builder, store_level_serialized, compute_level_serialized,
                                         hoist_storage_level_serialized,
                                         builder.CreateVector(storage_dims_serialized),
                                         builder.CreateVector(bounds_serialized),
                                         builder.CreateVector(estimates_serialized),
                                         builder.CreateVector(wrappers_serialized),
                                         memory_type, memoized, async,
                                         memoize_eviction_key_serialized.first, memoize_eviction_key_serialized.second);
}

Offset<Serialize::Specialization> Serializer::serialize_specialization(FlatBufferBuilder &builder, const Specialization &specialization) {
    const auto condition_serialized = serialize_expr(builder, specialization.condition);
    const auto definition_serialized = serialize_definition(builder, specialization.definition);
    const auto failure_message_serialized = serialize_string(builder, specialization.failure_message);
    return Serialize::CreateSpecialization(builder, condition_serialized.first, condition_serialized.second, definition_serialized, failure_message_serialized);
}

Offset<Serialize::Definition> Serializer::serialize_definition(FlatBufferBuilder &builder, const Definition &definition) {
    const auto is_init = definition.is_init();
    const auto predicate_serialized = serialize_expr(builder, definition.predicate());
    std::vector<uint8_t> values_types;
    values_types.reserve(definition.values().size());
    std::vector<Offset<void>> values_serialized;
    values_serialized.reserve(definition.values().size());
    for (const auto &value : definition.values()) {
        auto value_serialized = serialize_expr(builder, value);
        values_types.push_back(value_serialized.first);
        values_serialized.push_back(value_serialized.second);
    }
    std::vector<uint8_t> args_types;
    args_types.reserve(definition.args().size());
    std::vector<Offset<void>> args_serialized;
    args_serialized.reserve(definition.args().size());
    for (const auto &arg : definition.args()) {
        auto arg_serialized = serialize_expr(builder, arg);
        args_types.push_back(arg_serialized.first);
        args_serialized.push_back(arg_serialized.second);
    }
    const auto stage_schedule_serialized = serialize_stage_schedule(builder, definition.schedule());
    std::vector<Offset<Serialize::Specialization>> specializations_serialized;
    for (const auto &specialization : definition.specializations()) {
        specializations_serialized.push_back(serialize_specialization(builder, specialization));
    }
    const auto source_location_serialized = serialize_string(builder, definition.source_location());
    return Serialize::CreateDefinition(builder, is_init,
                                       predicate_serialized.first, predicate_serialized.second,
                                       builder.CreateVector(values_types), builder.CreateVector(values_serialized),
                                       builder.CreateVector(args_types), builder.CreateVector(args_serialized),
                                       stage_schedule_serialized, builder.CreateVector(specializations_serialized),
                                       source_location_serialized);
}

Offset<Serialize::ReductionVariable> Serializer::serialize_reduction_variable(FlatBufferBuilder &builder, const ReductionVariable &reduction_variable) {
    const auto var_serialized = serialize_string(builder, reduction_variable.var);
    const auto min_serialized = serialize_expr(builder, reduction_variable.min);
    const auto extent_serialized = serialize_expr(builder, reduction_variable.extent);
    return Serialize::CreateReductionVariable(builder, var_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second);
}

Offset<Serialize::ReductionDomain> Serializer::serialize_reduction_domain(FlatBufferBuilder &builder, const ReductionDomain &reduction_domain) {
    const bool defined = reduction_domain.defined();
    if (!defined) {
        return Serialize::CreateReductionDomain(builder, defined);
    }
    std::vector<Offset<Serialize::ReductionVariable>> domain_serialized;
    for (const auto &reduction_variable : reduction_domain.domain()) {
        domain_serialized.push_back(serialize_reduction_variable(builder, reduction_variable));
    }
    const auto predicate_serialized = serialize_expr(builder, reduction_domain.predicate());
    return Serialize::CreateReductionDomain(builder, defined,
                                            builder.CreateVector(domain_serialized),
                                            predicate_serialized.first, predicate_serialized.second,
                                            reduction_domain.frozen());
}

Offset<Serialize::ModulusRemainder> Serializer::serialize_modulus_remainder(FlatBufferBuilder &builder, const ModulusRemainder &modulus_remainder) {
    return Serialize::CreateModulusRemainder(builder, modulus_remainder.modulus, modulus_remainder.remainder);
}

Offset<Serialize::PrefetchDirective> Serializer::serialize_prefetch_directive(FlatBufferBuilder &builder, const PrefetchDirective &prefetch_directive) {
    const auto name_serialized = serialize_string(builder, prefetch_directive.name);
    const auto at_serialized = serialize_string(builder, prefetch_directive.at);
    const auto from_serialized = serialize_string(builder, prefetch_directive.from);
    const auto offset_serialized = serialize_expr(builder, prefetch_directive.offset);
    const auto strategy_serialized = serialize_prefetch_bound_strategy(prefetch_directive.strategy);
    const std::string param_name = prefetch_directive.param.defined() ? prefetch_directive.param.name() : "";
    if (prefetch_directive.param.defined() && parameters_in_pipeline.find(param_name) == parameters_in_pipeline.end()) {
        parameters_in_pipeline[param_name] = prefetch_directive.param;
    }
    const auto param_name_serialized = serialize_string(builder, param_name);
    return Serialize::CreatePrefetchDirective(builder, name_serialized,
                                              at_serialized, from_serialized,
                                              offset_serialized.first, offset_serialized.second,
                                              strategy_serialized, param_name_serialized);
}

Offset<Serialize::Split> Serializer::serialize_split(FlatBufferBuilder &builder, const Split &split) {
    const auto old_var_serialized = serialize_string(builder, split.old_var);
    const auto outer_serialized = serialize_string(builder, split.outer);
    const auto inner_serialized = serialize_string(builder, split.inner);
    const auto factor_serialized = serialize_expr(builder, split.factor);
    const auto exact = split.exact;
    const auto tail_serialized = serialize_tail_strategy(split.tail);
    const auto split_type_serialized = serialize_split_type(split.split_type);
    return Serialize::CreateSplit(builder, old_var_serialized,
                                  outer_serialized, inner_serialized,
                                  factor_serialized.first, factor_serialized.second,
                                  exact, tail_serialized, split_type_serialized);
}

Offset<Serialize::Dim> Serializer::serialize_dim(FlatBufferBuilder &builder, const Dim &dim) {
    const auto var_serialized = serialize_string(builder, dim.var);
    const auto for_type_serialized = serialize_for_type(dim.for_type);
    const auto device_api_serialized = serialize_device_api(dim.device_api);
    const auto dim_type_serialized = serialize_dim_type(dim.dim_type);
    const auto partition_policy_serialized = serialize_partition(dim.partition_policy);
    return Serialize::CreateDim(builder, var_serialized, for_type_serialized, device_api_serialized, dim_type_serialized, partition_policy_serialized);
}

Offset<Serialize::FuseLoopLevel> Serializer::serialize_fuse_loop_level(FlatBufferBuilder &builder, const FuseLoopLevel &fuse_loop_level) {
    const auto fuse_level_serialized = serialize_loop_level(builder, fuse_loop_level.level);
    std::vector<Offset<String>> align_dimension_names_serialized;
    std::vector<uint8_t> align_strategies_serialized;
    for (const auto &align : fuse_loop_level.align) {
        align_dimension_names_serialized.push_back(serialize_string(builder, align.first));
        align_strategies_serialized.push_back(serialize_loop_align_strategy(align.second));
    }
    return Serialize::CreateFuseLoopLevel(builder, fuse_level_serialized,
                                          builder.CreateVector(align_dimension_names_serialized),
                                          builder.CreateVector(align_strategies_serialized));
}

Offset<Serialize::FusedPair> Serializer::serialize_fused_pair(FlatBufferBuilder &builder, const FusedPair &fused_pair) {
    const auto func_1_serialized = serialize_string(builder, fused_pair.func_1);
    const auto func_2_serialized = serialize_string(builder, fused_pair.func_2);
    const auto var_name_serialized = serialize_string(builder, fused_pair.var_name);
    return Serialize::CreateFusedPair(builder, func_1_serialized, func_2_serialized,
                                      fused_pair.stage_1, fused_pair.stage_2, var_name_serialized);
}

Offset<Serialize::StageSchedule> Serializer::serialize_stage_schedule(FlatBufferBuilder &builder, const StageSchedule &stage_schedule) {
    std::vector<Offset<Serialize::ReductionVariable>> rvars_serialized;
    rvars_serialized.reserve(stage_schedule.rvars().size());
    for (const auto &rvar : stage_schedule.rvars()) {
        rvars_serialized.push_back(serialize_reduction_variable(builder, rvar));
    }
    std::vector<Offset<Serialize::Split>> splits_serialized;
    splits_serialized.reserve(stage_schedule.splits().size());
    for (const auto &split : stage_schedule.splits()) {
        splits_serialized.push_back(serialize_split(builder, split));
    }
    std::vector<Offset<Serialize::Dim>> dims_serialized;
    dims_serialized.reserve(stage_schedule.dims().size());
    for (const auto &dim : stage_schedule.dims()) {
        dims_serialized.push_back(serialize_dim(builder, dim));
    }
    std::vector<Offset<Serialize::PrefetchDirective>> prefetches_serialized;
    prefetches_serialized.reserve(stage_schedule.prefetches().size());
    for (const auto &prefetch : stage_schedule.prefetches()) {
        prefetches_serialized.push_back(serialize_prefetch_directive(builder, prefetch));
    }
    const auto fuse_level_serialized = serialize_fuse_loop_level(builder, stage_schedule.fuse_level());
    std::vector<Offset<Serialize::FusedPair>> fused_pairs_serialized;
    fused_pairs_serialized.reserve(stage_schedule.fused_pairs().size());
    for (const auto &fused_pair : stage_schedule.fused_pairs()) {
        fused_pairs_serialized.push_back(serialize_fused_pair(builder, fused_pair));
    }
    const bool touched = stage_schedule.touched();
    const bool allow_race_conditions = stage_schedule.allow_race_conditions();
    const bool atomic = stage_schedule.atomic();
    const bool override_atomic_associativity_test = stage_schedule.override_atomic_associativity_test();
    return Serialize::CreateStageSchedule(builder,
                                          builder.CreateVector(rvars_serialized),
                                          builder.CreateVector(splits_serialized),
                                          builder.CreateVector(dims_serialized),
                                          builder.CreateVector(prefetches_serialized),
                                          fuse_level_serialized,
                                          builder.CreateVector(fused_pairs_serialized),
                                          touched, allow_race_conditions, atomic,
                                          override_atomic_associativity_test);
}

Offset<Serialize::BufferConstraint> Serializer::serialize_buffer_constraint(FlatBufferBuilder &builder, const BufferConstraint &buffer_constraint) {
    const auto min_serialized = serialize_expr(builder, buffer_constraint.min);
    const auto extent_serialized = serialize_expr(builder, buffer_constraint.extent);
    const auto stride_serialized = serialize_expr(builder, buffer_constraint.stride);
    const auto min_estimate_serialized = serialize_expr(builder, buffer_constraint.min_estimate);
    const auto extent_estimate_serialized = serialize_expr(builder, buffer_constraint.extent_estimate);
    return Serialize::CreateBufferConstraint(builder,
                                             min_serialized.first, min_serialized.second,
                                             extent_serialized.first, extent_serialized.second,
                                             stride_serialized.first, stride_serialized.second,
                                             min_estimate_serialized.first, min_estimate_serialized.second,
                                             extent_estimate_serialized.first, extent_estimate_serialized.second);
}

Offset<Serialize::Parameter> Serializer::serialize_parameter(FlatBufferBuilder &builder, const Parameter &parameter) {
    const bool defined = parameter.defined();
    if (!defined) {
        return Serialize::CreateParameter(builder, defined);
    }
    const auto type_serialized = serialize_type(builder, parameter.type());
    const int dimensions = parameter.dimensions();
    const auto name_serialized = serialize_string(builder, parameter.name());
    const bool is_buffer = parameter.is_buffer();
    // Because of check_is_buffer()/check_is_scalar(), we cannot serialize all fields at the same time.
    // Depending on whether the parameter is a buffer, we serialize different fields,
    // or fill 0 or default values for the unavailable fields.
    if (is_buffer) {
        const int host_alignment = parameter.host_alignment();
        std::vector<Offset<Serialize::BufferConstraint>> buffer_constraints_serialized;
        buffer_constraints_serialized.reserve(parameter.buffer_constraints().size());
        for (const auto &buffer_constraint : parameter.buffer_constraints()) {
            buffer_constraints_serialized.push_back(serialize_buffer_constraint(builder, buffer_constraint));
        }
        const auto memory_type_serialized = serialize_memory_type(parameter.memory_type());
        return Serialize::CreateParameter(builder, defined, is_buffer, type_serialized, dimensions, name_serialized, host_alignment,
                                          builder.CreateVector(buffer_constraints_serialized), memory_type_serialized);
    } else {
        static_assert(FLATBUFFERS_USE_STD_OPTIONAL);
        const auto make_optional_u64 = [](const std::optional<halide_scalar_value_t> &v) -> std::optional<uint64_t> {
            return v.has_value() ?
                       std::optional<uint64_t>(v.value().u.u64) :
                       std::nullopt;
        };
        const auto scalar_data = make_optional_u64(parameter.scalar_data());
        const auto scalar_default_serialized = serialize_expr(builder, parameter.default_value());
        const auto scalar_min_serialized = serialize_expr(builder, parameter.min_value());
        const auto scalar_max_serialized = serialize_expr(builder, parameter.max_value());
        const auto scalar_estimate_serialized = serialize_expr(builder, parameter.estimate());
        return Serialize::CreateParameter(builder, defined, is_buffer, type_serialized,
                                          dimensions, name_serialized, 0, 0, Serialize::MemoryType_Auto, scalar_data,
                                          scalar_default_serialized.first, scalar_default_serialized.second,
                                          scalar_min_serialized.first, scalar_min_serialized.second,
                                          scalar_max_serialized.first, scalar_max_serialized.second,
                                          scalar_estimate_serialized.first, scalar_estimate_serialized.second);
    }
}

Offset<Serialize::ExternFuncArgument> Serializer::serialize_extern_func_argument(FlatBufferBuilder &builder, const ExternFuncArgument &extern_func_argument) {
    const auto arg_type_serialized = serialize_extern_func_argument_type(extern_func_argument.arg_type);
    if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::UndefinedArg) {
        return Serialize::CreateExternFuncArgument(builder, arg_type_serialized);
    } else if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::FuncArg) {
        int func_index = -1;
        if (this->func_mappings.find(Function(extern_func_argument.func).name()) != this->func_mappings.end()) {
            func_index = this->func_mappings[Function(extern_func_argument.func).name()];
        }
        return Serialize::CreateExternFuncArgument(builder, arg_type_serialized, func_index);
    } else if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::BufferArg) {
        const std::string buffer_name = extern_func_argument.buffer.defined() ? extern_func_argument.buffer.name() : "";
        if (extern_func_argument.buffer.defined() && buffers_in_pipeline.find(buffer_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[buffer_name] = extern_func_argument.buffer;
        }
        const auto buffer_name_serialized = serialize_string(builder, buffer_name);
        return Serialize::CreateExternFuncArgument(builder, arg_type_serialized, -1, buffer_name_serialized);
    } else if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::ExprArg) {
        const auto expr_serialized = serialize_expr(builder, extern_func_argument.expr);
        return Serialize::CreateExternFuncArgument(builder, arg_type_serialized, -1, 0, expr_serialized.first, expr_serialized.second);
    } else {
        const std::string image_param_name = extern_func_argument.image_param.defined() ? extern_func_argument.image_param.name() : "";
        if (extern_func_argument.defined() && external_parameters.find(image_param_name) == external_parameters.end()) {
            external_parameters[image_param_name] = extern_func_argument.image_param;
        }
        const auto image_param_name_serialized = serialize_string(builder, image_param_name);
        return Serialize::CreateExternFuncArgument(builder, arg_type_serialized, -1, 0, Serialize::Expr_NONE, 0, image_param_name_serialized);
    }
}

Offset<Serialize::Buffer> Serializer::serialize_buffer(FlatBufferBuilder &builder, Buffer<> buffer) {
    if (!buffer.defined()) {
        return Serialize::CreateBuffer(builder, false);
    }
    if (buffer.device_dirty()) {
        user_error << "Cannot serialize on-device buffer: " << buffer.name() << "\n";
    }
    buffer.copy_to_host();
    const auto name_serialized = serialize_string(builder, buffer.name());
    const auto type_serialized = serialize_type(builder, buffer.type());
    const int32_t dimensions = buffer.dimensions();
    std::vector<Offset<Serialize::BufferDimension>> buffer_dimensions_serialized;
    for (int i = 0; i < buffer.dimensions(); ++i) {
        int32_t min = buffer.dim(i).min();
        int32_t extent = buffer.dim(i).extent();
        int32_t stride = buffer.dim(i).stride();
        buffer_dimensions_serialized.push_back(Serialize::CreateBufferDimension(builder, min, extent, stride));
    }
    auto copy = buffer.copy();  // compact in memory
    std::vector<uint8_t> data;
    data.resize(copy.size_in_bytes());
    memcpy(data.data(), copy.data(), copy.size_in_bytes());
    return Serialize::CreateBuffer(builder, true, name_serialized, type_serialized, dimensions, builder.CreateVector(buffer_dimensions_serialized), builder.CreateVector(data));
}

std::vector<Offset<Serialize::WrapperRef>> Serializer::serialize_wrapper_refs(FlatBufferBuilder &builder, const std::map<std::string, FunctionPtr> &wrappers) {
    std::vector<Offset<Serialize::WrapperRef>> wrapper_refs_serialized;
    wrapper_refs_serialized.reserve(wrappers.size());
    for (const auto &wrapper : wrappers) {
        auto wrapper_name_serialized = serialize_string(builder, wrapper.first);
        int func_index = -1;
        if (this->func_mappings.find(Function(wrapper.second).name()) != this->func_mappings.end()) {
            func_index = this->func_mappings[Function(wrapper.second).name()];
        }
        wrapper_refs_serialized.push_back(Serialize::CreateWrapperRef(builder, wrapper_name_serialized, func_index));
    }
    return wrapper_refs_serialized;
}

void Serializer::build_function_mappings(const std::map<std::string, Function> &env) {
    if (!this->func_mappings.empty()) {
        this->func_mappings.clear();
    }
    int32_t cnt = 0;
    for (const auto &entry : env) {
        user_assert(env.find(entry.first) != env.end()) << "function " << entry.first << " not found in the environment\n";
        this->func_mappings[entry.first] = cnt++;
    }
}

void Serializer::serialize(const Pipeline &pipeline, std::vector<uint8_t> &result) {
    FlatBufferBuilder builder(1024);

    // extract the DAG, unwrap function from Funcs
    std::vector<Function> outputs_functions;
    for (const Func &func : pipeline.outputs()) {
        outputs_functions.push_back(func.function());
    }
    std::map<std::string, Function> env = build_environment(outputs_functions);
    build_function_mappings(env);

    std::vector<Offset<String>> func_names_in_order_serialized;
    std::vector<Offset<Serialize::Func>> funcs_serialized;
    for (const auto &entry : env) {
        func_names_in_order_serialized.push_back(serialize_string(builder, entry.first));
        funcs_serialized.push_back(this->serialize_function(builder, entry.second));
    }

    auto outputs = pipeline.outputs();
    std::vector<Offset<String>> output_names_serialized;
    output_names_serialized.reserve(outputs.size());
    for (const auto &output : outputs) {
        output_names_serialized.push_back(serialize_string(builder, output.name()));
    }
    auto requirements = pipeline.requirements();
    std::vector<Offset<void>> requirements_serialized;
    requirements_serialized.reserve(requirements.size());
    std::vector<uint8_t> requirements_types;
    requirements_types.reserve(requirements.size());
    for (const auto &stmt : requirements) {
        auto stmt_serialized = serialize_stmt(builder, stmt);
        requirements_serialized.push_back(stmt_serialized.second);
        requirements_types.push_back(stmt_serialized.first);
    }

    // For Parameters and buffers, to avoid serializing the same object multiple times, we use a map to store the unique
    // objects seen in the whole pipeline and only serialize their names (strings) at the use sites,
    // then we do the actual serialization of the unique objects once
    std::vector<Offset<Serialize::Parameter>> parameters_serialized;
    parameters_serialized.reserve(parameters_in_pipeline.size());
    for (const auto &param : parameters_in_pipeline) {
        // we only serialize internal parameters with the pipeline
        if (external_parameters.find(param.first) == external_parameters.end()) {
            parameters_serialized.push_back(serialize_parameter(builder, param.second));
        }
    }

    std::vector<Offset<Serialize::Buffer>> buffers_serialized;
    buffers_serialized.reserve(buffers_in_pipeline.size());
    for (auto &buffer : buffers_in_pipeline) {
        buffers_serialized.push_back(serialize_buffer(builder, buffer.second));
    }

    auto pipeline_obj = Serialize::CreatePipeline(builder,
                                                  builder.CreateVector(funcs_serialized),
                                                  builder.CreateVector(output_names_serialized),
                                                  builder.CreateVector(requirements_types),
                                                  builder.CreateVector(requirements_serialized),
                                                  builder.CreateVector(func_names_in_order_serialized),
                                                  builder.CreateVector(parameters_serialized),
                                                  builder.CreateVector(buffers_serialized));
    builder.Finish(pipeline_obj);

    uint8_t *buf = builder.GetBufferPointer();
    int size = builder.GetSize();

    if (buf != nullptr && size > 0) {
        result.clear();
        result.reserve(size);
        result.insert(result.begin(), buf, buf + size);
    } else {
        user_error << "failed to serialize pipeline!\n";
    }
}

void Serializer::serialize(const Pipeline &pipeline, const std::string &filename) {
    std::vector<uint8_t> data;
    serialize(pipeline, data);
    std::ofstream out(filename, std::ios::out | std::ios::binary);
    if (!out) {
        user_error << "failed to open file " << filename << "\n";
        exit(1);
    }
    out.write((char *)(data.data()), data.size());
    out.close();
}

}  // namespace Internal

void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data) {
    Internal::Serializer serializer;
    serializer.serialize(pipeline, data);
}

void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data, std::map<std::string, Parameter> &params) {
    Internal::Serializer serializer;
    serializer.serialize(pipeline, data);
    params = serializer.get_external_parameters();
}

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename) {
    Internal::Serializer serializer;
    serializer.serialize(pipeline, filename);
}

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::map<std::string, Parameter> &params) {
    Internal::Serializer serializer;
    serializer.serialize(pipeline, filename);
    params = serializer.get_external_parameters();
}

}  // namespace Halide

#else  // WITH_SERIALIZATION

namespace Halide {

void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data) {
    user_error << "Serialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
}

void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data, std::map<std::string, Parameter> &params) {
    user_error << "Serialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
}

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename) {
    user_error << "Serialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
}

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::map<std::string, Parameter> &params) {
    user_error << "Serialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
}

}  // namespace Halide

#endif  // WITH_SERIALIZATION
