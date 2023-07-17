#include "Serialization.h"
#include "Schedule.h"
#include "halide_ir_generated.h"

#include <string>
#include <utility>
#include <vector>

#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "RealizationOrder.h"
#include <fstream>
#include <iostream>
#include <map>

namespace Halide {
namespace Internal {
class Serializer {
public:
    Serializer() = default;

    void serialize(const Pipeline &pipeline, const std::string &filename);

    const std::unordered_map<std::string, Parameter> &get_external_parameters() const {
        return external_parameters;
    }

private:
    std::unordered_map<std::string, int32_t> func_mappings;

    std::unordered_map<std::string, Internal::Parameter> parameters_in_pipeline;

    std::unordered_map<std::string, Buffer<>> buffers_in_pipeline;

    std::unordered_map<std::string, Parameter> external_parameters;

    // helper functions to serialize each type of object
    Halide::Serialize::MemoryType serialize_memory_type(const MemoryType &memory_type);

    Halide::Serialize::ForType serialize_for_type(const ForType &for_type);

    Halide::Serialize::DeviceAPI serialize_device_api(const DeviceAPI &device_api);

    Halide::Serialize::CallType serialize_call_type(const Call::CallType &call_type);

    Halide::Serialize::VectorReduceOp serialize_vector_reduce_op(const VectorReduce::Operator &vector_reduce_op);

    Halide::Serialize::PrefetchBoundStrategy serialize_prefetch_bound_strategy(const PrefetchBoundStrategy &prefetch_bound_strategy);

    Halide::Serialize::NameMangling serialize_name_mangling(const NameMangling &name_mangling);

    Halide::Serialize::TailStrategy serialize_tail_strategy(const TailStrategy &tail_strategy);

    Halide::Serialize::SplitType serialize_split_type(const Split::SplitType &split_type);

    Halide::Serialize::DimType serialize_dim_type(const DimType &dim_type);

    Halide::Serialize::LoopAlignStrategy serialize_loop_align_strategy(const LoopAlignStrategy &loop_align_strategy);

    Halide::Serialize::ExternFuncArgumentType serialize_extern_func_argument_type(const ExternFuncArgument::ArgType &extern_func_argument_type);

    flatbuffers::Offset<flatbuffers::String> serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str);

    flatbuffers::Offset<Halide::Serialize::Type> serialize_type(flatbuffers::FlatBufferBuilder &builder, const Type &type);

    // Stmt and Expr are special because they are union types so need to return both the type and serialized object
    std::pair<Halide::Serialize::Stmt, flatbuffers::Offset<void>> serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Stmt &stmt);

    std::pair<Halide::Serialize::Expr, flatbuffers::Offset<void>> serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Expr &expr);

    flatbuffers::Offset<Halide::Serialize::Func> serialize_function(flatbuffers::FlatBufferBuilder &builder, const Function &function);

    flatbuffers::Offset<Halide::Serialize::Range> serialize_range(flatbuffers::FlatBufferBuilder &builder, const Range &range);

    flatbuffers::Offset<Halide::Serialize::Bound> serialize_bound(flatbuffers::FlatBufferBuilder &builder, const Bound &bound);

    flatbuffers::Offset<Halide::Serialize::StorageDim> serialize_storage_dim(flatbuffers::FlatBufferBuilder &builder, const StorageDim &storage_dim);

    flatbuffers::Offset<Halide::Serialize::LoopLevel> serialize_loop_level(flatbuffers::FlatBufferBuilder &builder, const LoopLevel &loop_level);

    flatbuffers::Offset<Halide::Serialize::FuncSchedule> serialize_func_schedule(flatbuffers::FlatBufferBuilder &builder, const FuncSchedule &func_schedule);

    flatbuffers::Offset<Halide::Serialize::Specialization> serialize_specialization(flatbuffers::FlatBufferBuilder &builder, const Specialization &specialization);

    flatbuffers::Offset<Halide::Serialize::Definition> serialize_definition(flatbuffers::FlatBufferBuilder &builder, const Definition &definition);

    flatbuffers::Offset<Halide::Serialize::ReductionVariable> serialize_reduction_variable(flatbuffers::FlatBufferBuilder &builder, const ReductionVariable &reduction_variable);

    flatbuffers::Offset<Halide::Serialize::ReductionDomain> serialize_reduction_domain(flatbuffers::FlatBufferBuilder &builder, const ReductionDomain &reduction_domain);

    flatbuffers::Offset<Halide::Serialize::ModulusRemainder> serialize_modulus_remainder(flatbuffers::FlatBufferBuilder &builder, const ModulusRemainder &modulus_remainder);

    flatbuffers::Offset<Halide::Serialize::PrefetchDirective> serialize_prefetch_directive(flatbuffers::FlatBufferBuilder &builder, const PrefetchDirective &prefetch_directive);

    flatbuffers::Offset<Halide::Serialize::Split> serialize_split(flatbuffers::FlatBufferBuilder &builder, const Split &split);

    flatbuffers::Offset<Halide::Serialize::Dim> serialize_dim(flatbuffers::FlatBufferBuilder &builder, const Dim &dim);

    flatbuffers::Offset<Halide::Serialize::FuseLoopLevel> serialize_fuse_loop_level(flatbuffers::FlatBufferBuilder &builder, const FuseLoopLevel &fuse_loop_level);

    flatbuffers::Offset<Halide::Serialize::FusedPair> serialize_fused_pair(flatbuffers::FlatBufferBuilder &builder, const FusedPair &fused_pair);

    flatbuffers::Offset<Halide::Serialize::StageSchedule> serialize_stage_schedule(flatbuffers::FlatBufferBuilder &builder, const StageSchedule &stage_schedule);

    flatbuffers::Offset<Halide::Serialize::BufferConstraint> serialize_buffer_constraint(flatbuffers::FlatBufferBuilder &builder, const BufferConstraint &buffer_constraint);

    flatbuffers::Offset<Halide::Serialize::Parameter> serialize_parameter(flatbuffers::FlatBufferBuilder &builder, const Parameter &parameter);

    flatbuffers::Offset<Halide::Serialize::ExternFuncArgument> serialize_extern_func_argument(flatbuffers::FlatBufferBuilder &builder, const ExternFuncArgument &extern_func_argument);

    flatbuffers::Offset<Halide::Serialize::Buffer> serialize_buffer(flatbuffers::FlatBufferBuilder &builder, const Buffer<> &buffer);

    std::vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> serialize_wrapper_refs(flatbuffers::FlatBufferBuilder &builder, const std::map<std::string, FunctionPtr> &wrappers);

    void build_function_mappings(const std::map<std::string, Function> &env);
};

Halide::Serialize::MemoryType Serializer::serialize_memory_type(const MemoryType &memory_type) {
    switch (memory_type) {
    case MemoryType::Auto:
        return Halide::Serialize::MemoryType::MemoryType_Auto;
    case MemoryType::Heap:
        return Halide::Serialize::MemoryType::MemoryType_Heap;
    case MemoryType::Stack:
        return Halide::Serialize::MemoryType::MemoryType_Stack;
    case MemoryType::Register:
        return Halide::Serialize::MemoryType::MemoryType_Register;
    case MemoryType::GPUShared:
        return Halide::Serialize::MemoryType::MemoryType_GPUShared;
    case MemoryType::GPUTexture:
        return Halide::Serialize::MemoryType::MemoryType_GPUTexture;
    case MemoryType::LockedCache:
        return Halide::Serialize::MemoryType::MemoryType_LockedCache;
    case MemoryType::VTCM:
        return Halide::Serialize::MemoryType::MemoryType_VTCM;
    case MemoryType::AMXTile:
        return Halide::Serialize::MemoryType::MemoryType_AMXTile;
    default:
        user_assert(false) << "Unsupported memory type\n";
        return Halide::Serialize::MemoryType_Auto;
    }
}

Halide::Serialize::ForType Serializer::serialize_for_type(const ForType &for_type) {
    switch (for_type) {
    case ForType::Serial:
        return Halide::Serialize::ForType::ForType_Serial;
    case ForType::Parallel:
        return Halide::Serialize::ForType::ForType_Parallel;
    case ForType::Vectorized:
        return Halide::Serialize::ForType::ForType_Vectorized;
    case ForType::Unrolled:
        return Halide::Serialize::ForType::ForType_Unrolled;
    case ForType::Extern:
        return Halide::Serialize::ForType::ForType_Extern;
    case ForType::GPUBlock:
        return Halide::Serialize::ForType::ForType_GPUBlock;
    case ForType::GPUThread:
        return Halide::Serialize::ForType::ForType_GPUThread;
    case ForType::GPULane:
        return Halide::Serialize::ForType::ForType_GPULane;
    default:
        user_assert(false) << "Unsupported for type\n";
        return Halide::Serialize::ForType_Serial;
    }
}

Halide::Serialize::DeviceAPI Serializer::serialize_device_api(const DeviceAPI &device_api) {
    switch (device_api) {
    case DeviceAPI::None:
        return Halide::Serialize::DeviceAPI::DeviceAPI_None;
    case DeviceAPI::Host:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Host;
    case DeviceAPI::Default_GPU:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Default_GPU;
    case DeviceAPI::CUDA:
        return Halide::Serialize::DeviceAPI::DeviceAPI_CUDA;
    case DeviceAPI::OpenCL:
        return Halide::Serialize::DeviceAPI::DeviceAPI_OpenCL;
    case DeviceAPI::OpenGLCompute:
        return Halide::Serialize::DeviceAPI::DeviceAPI_OpenGLCompute;
    case DeviceAPI::Metal:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Metal;
    case DeviceAPI::Hexagon:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Hexagon;
    case DeviceAPI::HexagonDma:
        return Halide::Serialize::DeviceAPI::DeviceAPI_HexagonDma;
    case DeviceAPI::D3D12Compute:
        return Halide::Serialize::DeviceAPI::DeviceAPI_D3D12Compute;
    case DeviceAPI::Vulkan:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Vulkan;
    case DeviceAPI::WebGPU:
        return Halide::Serialize::DeviceAPI::DeviceAPI_WebGPU;
    default:
        user_assert(false) << "Unsupported device API\n";
        return Halide::Serialize::DeviceAPI_None;
    }
}

Halide::Serialize::CallType Serializer::serialize_call_type(const Call::CallType &call_type) {
    switch (call_type) {
    case Call::CallType::Image:
        return Halide::Serialize::CallType::CallType_Image;
    case Call::CallType::Extern:
        return Halide::Serialize::CallType::CallType_Extern;
    case Call::CallType::ExternCPlusPlus:
        return Halide::Serialize::CallType::CallType_ExternCPlusPlus;
    case Call::CallType::PureExtern:
        return Halide::Serialize::CallType::CallType_PureExtern;
    case Call::CallType::Halide:
        return Halide::Serialize::CallType::CallType_Halide;
    case Call::CallType::Intrinsic:
        return Halide::Serialize::CallType::CallType_Intrinsic;
    case Call::CallType::PureIntrinsic:
        return Halide::Serialize::CallType::CallType_PureIntrinsic;
    default:
        user_assert(false) << "Unsupported call type\n";
        return Halide::Serialize::CallType::CallType_Image;
    }
}

Halide::Serialize::VectorReduceOp Serializer::serialize_vector_reduce_op(const VectorReduce::Operator &vector_reduce_op) {
    switch (vector_reduce_op) {
    case VectorReduce::Operator::Add:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Add;
    case VectorReduce::Operator::SaturatingAdd:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_SaturatingAdd;
    case VectorReduce::Operator::Mul:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Mul;
    case VectorReduce::Operator::Min:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Min;
    case VectorReduce::Operator::Max:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Max;
    case VectorReduce::Operator::And:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_And;
    case VectorReduce::Operator::Or:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Or;
    default:
        user_assert(false) << "Unsupported vector reduce op\n";
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Add;
    }
}

Halide::Serialize::PrefetchBoundStrategy Serializer::serialize_prefetch_bound_strategy(const PrefetchBoundStrategy &prefetch_bound_strategy) {
    switch (prefetch_bound_strategy) {
    case PrefetchBoundStrategy::Clamp:
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp;
    case PrefetchBoundStrategy::GuardWithIf:
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_GuardWithIf;
    case PrefetchBoundStrategy::NonFaulting:
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_NonFaulting;
    default:
        user_assert(false) << "Unsupported prefetch bound strategy\n";
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp;
    }
}

Halide::Serialize::NameMangling Serializer::serialize_name_mangling(const NameMangling &name_mangling) {
    switch (name_mangling) {
    case NameMangling::Default:
        return Halide::Serialize::NameMangling::NameMangling_Default;
    case NameMangling::C:
        return Halide::Serialize::NameMangling::NameMangling_C;
    case NameMangling::CPlusPlus:
        return Halide::Serialize::NameMangling::NameMangling_CPlusPlus;
    default:
        user_assert(false) << "Unsupported name mangling\n";
        return Halide::Serialize::NameMangling::NameMangling_Default;
    }
}

Halide::Serialize::TailStrategy Serializer::serialize_tail_strategy(const TailStrategy &tail_strategy) {
    switch (tail_strategy) {
    case TailStrategy::RoundUp:
        return Halide::Serialize::TailStrategy::TailStrategy_RoundUp;
    case TailStrategy::GuardWithIf:
        return Halide::Serialize::TailStrategy::TailStrategy_GuardWithIf;
    case TailStrategy::Predicate:
        return Halide::Serialize::TailStrategy::TailStrategy_Predicate;
    case TailStrategy::PredicateLoads:
        return Halide::Serialize::TailStrategy::TailStrategy_PredicateLoads;
    case TailStrategy::PredicateStores:
        return Halide::Serialize::TailStrategy::TailStrategy_PredicateStores;
    case TailStrategy::ShiftInwards:
        return Halide::Serialize::TailStrategy::TailStrategy_ShiftInwards;
    case TailStrategy::Auto:
        return Halide::Serialize::TailStrategy::TailStrategy_Auto;
    default:
        user_assert(false) << "Unsupported tail strategy\n";
        return Halide::Serialize::TailStrategy::TailStrategy_RoundUp;
    }
}

Halide::Serialize::SplitType Serializer::serialize_split_type(const Split::SplitType &split_type) {
    switch (split_type) {
    case Split::SplitType::SplitVar:
        return Halide::Serialize::SplitType::SplitType_SplitVar;
    case Split::SplitType::RenameVar:
        return Halide::Serialize::SplitType::SplitType_RenameVar;
    case Split::SplitType::FuseVars:
        return Halide::Serialize::SplitType::SplitType_FuseVars;
    case Split::SplitType::PurifyRVar:
        return Halide::Serialize::SplitType::SplitType_PurifyRVar;
    default:
        user_assert(false) << "Unsupported split type\n";
        return Halide::Serialize::SplitType::SplitType_SplitVar;
    }
}

Halide::Serialize::DimType Serializer::serialize_dim_type(const DimType &dim_type) {
    switch (dim_type) {
    case DimType::PureVar:
        return Halide::Serialize::DimType::DimType_PureVar;
    case DimType::PureRVar:
        return Halide::Serialize::DimType::DimType_PureRVar;
    case DimType::ImpureRVar:
        return Halide::Serialize::DimType::DimType_ImpureRVar;
    default:
        user_assert(false) << "Unsupported dim type\n";
        return Halide::Serialize::DimType::DimType_PureVar;
    }
}

Halide::Serialize::LoopAlignStrategy Serializer::serialize_loop_align_strategy(const LoopAlignStrategy &loop_align_strategy) {
    switch (loop_align_strategy) {
    case LoopAlignStrategy::AlignStart:
        return Halide::Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignStart;
    case LoopAlignStrategy::AlignEnd:
        return Halide::Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignEnd;
    case LoopAlignStrategy::NoAlign:
        return Halide::Serialize::LoopAlignStrategy::LoopAlignStrategy_NoAlign;
    case LoopAlignStrategy::Auto:
        return Halide::Serialize::LoopAlignStrategy::LoopAlignStrategy_Auto;
    default:
        user_assert(false) << "Unsupported loop align strategy\n";
        return Halide::Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignStart;
    }
}

Halide::Serialize::ExternFuncArgumentType Serializer::serialize_extern_func_argument_type(const ExternFuncArgument::ArgType &extern_func_argument_type) {
    switch (extern_func_argument_type) {
    case ExternFuncArgument::ArgType::UndefinedArg:
        return Halide::Serialize::ExternFuncArgumentType::ExternFuncArgumentType_UndefinedArg;
    case ExternFuncArgument::ArgType::FuncArg:
        return Halide::Serialize::ExternFuncArgumentType::ExternFuncArgumentType_FuncArg;
    case ExternFuncArgument::ArgType::BufferArg:
        return Halide::Serialize::ExternFuncArgumentType::ExternFuncArgumentType_BufferArg;
    case ExternFuncArgument::ArgType::ExprArg:
        return Halide::Serialize::ExternFuncArgumentType::ExternFuncArgumentType_ExprArg;
    case ExternFuncArgument::ArgType::ImageParamArg:
        return Halide::Serialize::ExternFuncArgumentType::ExternFuncArgumentType_ImageParamArg;
    default:
        user_assert(false) << "Unsupported extern func argument type\n";
        return Halide::Serialize::ExternFuncArgumentType::ExternFuncArgumentType_UndefinedArg;
    }
}

flatbuffers::Offset<flatbuffers::String> Serializer::serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str) {
    return builder.CreateString(str);
}

flatbuffers::Offset<Halide::Serialize::Type> Serializer::serialize_type(flatbuffers::FlatBufferBuilder &builder, const Type &type) {
    int bits = type.bits();
    int lanes = type.lanes();
    halide_type_code_t code = type.code();
    auto code_serialized = Halide::Serialize::TypeCode(code);
    auto type_obj = Halide::Serialize::CreateType(builder, code_serialized, bits, lanes);
    return type_obj;
}

std::pair<Halide::Serialize::Stmt, flatbuffers::Offset<void>> Serializer::serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Stmt &stmt) {
    if (!stmt.defined()) {
        return std::make_pair(Halide::Serialize::Stmt::Stmt_UndefinedStmt, Halide::Serialize::CreateUndefinedStmt(builder).Union());
    }
    switch (stmt->node_type) {
    case IRNodeType::LetStmt: {
        auto let_stmt = stmt.as<LetStmt>();
        auto name_serialized = serialize_string(builder, let_stmt->name);
        auto value_serialized = serialize_expr(builder, let_stmt->value);
        auto body_serialized = serialize_stmt(builder, let_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_LetStmt, Halide::Serialize::CreateLetStmt(builder, name_serialized, value_serialized.first, value_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::AssertStmt: {
        auto assert_stmt = stmt.as<AssertStmt>();
        auto condition_serialized = serialize_expr(builder, assert_stmt->condition);
        auto message_serialized = serialize_expr(builder, assert_stmt->message);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_AssertStmt, Halide::Serialize::CreateAssertStmt(builder, condition_serialized.first, condition_serialized.second, message_serialized.first, message_serialized.second).Union());
    }
    case IRNodeType::ProducerConsumer: {
        auto producer_consumer = stmt.as<ProducerConsumer>();
        auto name_serialized = serialize_string(builder, producer_consumer->name);
        auto body_serialized = serialize_stmt(builder, producer_consumer->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_ProducerConsumer, Halide::Serialize::CreateProducerConsumer(builder, name_serialized, producer_consumer->is_producer, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::For: {
        auto for_stmt = stmt.as<For>();
        auto name_serialized = serialize_string(builder, for_stmt->name);
        auto min_serialized = serialize_expr(builder, for_stmt->min);
        auto extent_serialized = serialize_expr(builder, for_stmt->extent);
        Halide::Serialize::ForType for_type = serialize_for_type(for_stmt->for_type);
        Halide::Serialize::DeviceAPI device_api = serialize_device_api(for_stmt->device_api);
        auto body_serialized = serialize_stmt(builder, for_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_For, Halide::Serialize::CreateFor(builder, name_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second, for_type, device_api, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::Store: {
        auto store_stmt = stmt.as<Store>();
        auto name_serialized = serialize_string(builder, store_stmt->name);
        auto predicate_serialized = serialize_expr(builder, store_stmt->predicate);
        auto value_serialized = serialize_expr(builder, store_stmt->value);
        auto index_serialized = serialize_expr(builder, store_stmt->index);
        std::string param_name = store_stmt->param.defined() ? store_stmt->param.name() : "";
        if (store_stmt->param.defined() && parameters_in_pipeline.find(param_name) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[param_name] = store_stmt->param;
        }
        auto param_name_serialized = serialize_string(builder, param_name);
        auto alignment_serialized = serialize_modulus_remainder(builder, store_stmt->alignment);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Store, Halide::Serialize::CreateStore(builder, name_serialized, predicate_serialized.first, predicate_serialized.second, value_serialized.first, value_serialized.second, index_serialized.first, index_serialized.second, param_name_serialized, alignment_serialized).Union());
    }
    case IRNodeType::Provide: {
        auto provide_stmt = stmt.as<Provide>();
        auto name_serialized = serialize_string(builder, provide_stmt->name);
        auto values = provide_stmt->values;
        std::vector<uint8_t> values_types;
        values_types.reserve(values.size());
        std::vector<flatbuffers::Offset<void>> values_serialized;
        values_serialized.reserve(values.size());
        for (const auto &value : values) {
            auto value_serialized = serialize_expr(builder, value);
            values_types.push_back(value_serialized.first);
            values_serialized.push_back(value_serialized.second);
        }
        auto args = provide_stmt->args;
        std::vector<uint8_t> args_types;
        args_types.reserve(args.size());
        std::vector<flatbuffers::Offset<void>> args_serialized;
        args_serialized.reserve(args.size());
        for (const auto &arg : args) {
            auto arg_serialized = serialize_expr(builder, arg);
            args_types.push_back(arg_serialized.first);
            args_serialized.push_back(arg_serialized.second);
        }
        auto predicate_serialized = serialize_expr(builder, provide_stmt->predicate);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Provide, Halide::Serialize::CreateProvide(builder, name_serialized, builder.CreateVector(values_types), builder.CreateVector(values_serialized), builder.CreateVector(args_types), builder.CreateVector(args_serialized), predicate_serialized.first, predicate_serialized.second).Union());
    }
    case IRNodeType::Allocate: {
        auto allocate_stmt = stmt.as<Allocate>();
        auto name_serialized = serialize_string(builder, allocate_stmt->name);
        auto type_serialized = serialize_type(builder, allocate_stmt->type);
        Halide::Serialize::MemoryType memory_type = serialize_memory_type(allocate_stmt->memory_type);
        auto extents = allocate_stmt->extents;
        std::vector<uint8_t> extents_types;
        extents_types.reserve(extents.size());
        std::vector<flatbuffers::Offset<void>> extents_serialized;
        extents_serialized.reserve(extents.size());
        for (const auto &extent : extents) {
            auto extent_serialized = serialize_expr(builder, extent);
            extents_types.push_back(extent_serialized.first);
            extents_serialized.push_back(extent_serialized.second);
        }
        auto condition_serialized = serialize_expr(builder, allocate_stmt->condition);
        auto new_expr_serialized = serialize_expr(builder, allocate_stmt->new_expr);
        auto free_function_serialized = serialize_string(builder, allocate_stmt->free_function);
        auto padding = allocate_stmt->padding;
        auto body_serialized = serialize_stmt(builder, allocate_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Allocate, Halide::Serialize::CreateAllocate(builder, name_serialized, type_serialized, memory_type, builder.CreateVector(extents_types), builder.CreateVector(extents_serialized), condition_serialized.first, condition_serialized.second, new_expr_serialized.first, new_expr_serialized.second, free_function_serialized, padding, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::Free: {
        auto free_stmt = stmt.as<Free>();
        auto name_serialized = serialize_string(builder, free_stmt->name);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Free, Halide::Serialize::CreateFree(builder, name_serialized).Union());
    }
    case IRNodeType::Realize: {
        auto realize_stmt = stmt.as<Realize>();
        auto name_serialized = serialize_string(builder, realize_stmt->name);
        auto types = realize_stmt->types;
        std::vector<flatbuffers::Offset<Halide::Serialize::Type>> types_serialized;
        types_serialized.reserve(types.size());
        for (const auto &type : types) {
            types_serialized.push_back(serialize_type(builder, type));
        }
        Halide::Serialize::MemoryType memory_type = serialize_memory_type(realize_stmt->memory_type);
        auto bounds = realize_stmt->bounds;
        std::vector<flatbuffers::Offset<Halide::Serialize::Range>> bounds_serialized;
        bounds_serialized.reserve(bounds.size());
        for (const auto &bound : bounds) {
            bounds_serialized.push_back(serialize_range(builder, bound));
        }
        auto types_vector = builder.CreateVector(types_serialized);
        auto condition_serialized = serialize_expr(builder, realize_stmt->condition);
        auto body_serialized = serialize_stmt(builder, realize_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Realize, Halide::Serialize::CreateRealize(builder, name_serialized, types_vector, memory_type, builder.CreateVector(bounds_serialized), condition_serialized.first, condition_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::Block: {
        auto block_stmt = stmt.as<Block>();
        auto first_serialized = serialize_stmt(builder, block_stmt->first);
        auto rest_serialized = serialize_stmt(builder, block_stmt->rest);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Block, Halide::Serialize::CreateBlock(builder, first_serialized.first, first_serialized.second, rest_serialized.first, rest_serialized.second).Union());
    }
    case IRNodeType::IfThenElse: {
        auto if_then_else_stmt = stmt.as<IfThenElse>();
        auto condition_serialized = serialize_expr(builder, if_then_else_stmt->condition);
        auto then_case_serialized = serialize_stmt(builder, if_then_else_stmt->then_case);
        auto else_case_serialized = serialize_stmt(builder, if_then_else_stmt->else_case);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_IfThenElse, Halide::Serialize::CreateIfThenElse(builder, condition_serialized.first, condition_serialized.second, then_case_serialized.first, then_case_serialized.second, else_case_serialized.first, else_case_serialized.second).Union());
    }
    case IRNodeType::Evaluate: {
        auto evaluate_stmt = stmt.as<Evaluate>();
        auto value_serialized = serialize_expr(builder, evaluate_stmt->value);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Evaluate, Halide::Serialize::CreateEvaluate(builder, value_serialized.first, value_serialized.second).Union());
    }
    case IRNodeType::Prefetch: {
        auto prefetch_stmt = stmt.as<Prefetch>();
        auto name_serialized = serialize_string(builder, prefetch_stmt->name);
        auto types = prefetch_stmt->types;
        std::vector<flatbuffers::Offset<Halide::Serialize::Type>> types_serialized;
        types_serialized.reserve(types.size());
        for (const auto &type : types) {
            types_serialized.push_back(serialize_type(builder, type));
        }
        auto types_vector = builder.CreateVector(types_serialized);
        auto bounds = prefetch_stmt->bounds;
        std::vector<flatbuffers::Offset<Halide::Serialize::Range>> bounds_serialized;
        bounds_serialized.reserve(bounds.size());
        for (const auto &bound : bounds) {
            bounds_serialized.push_back(serialize_range(builder, bound));
        }
        auto prefetch_serialized = serialize_prefetch_directive(builder, prefetch_stmt->prefetch);
        auto condition_serialized = serialize_expr(builder, prefetch_stmt->condition);
        auto body_serialized = serialize_stmt(builder, prefetch_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Prefetch, Halide::Serialize::CreatePrefetch(builder, name_serialized, types_vector, builder.CreateVector(bounds_serialized), prefetch_serialized, condition_serialized.first, condition_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::Acquire: {
        auto acquire_stmt = stmt.as<Acquire>();
        auto semaphore_serialized = serialize_expr(builder, acquire_stmt->semaphore);
        auto count_serialized = serialize_expr(builder, acquire_stmt->count);
        auto body_serialized = serialize_stmt(builder, acquire_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Acquire, Halide::Serialize::CreateAcquire(builder, semaphore_serialized.first, semaphore_serialized.second, count_serialized.first, count_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::Fork: {
        auto fork_stmt = stmt.as<Fork>();
        auto first_serialized = serialize_stmt(builder, fork_stmt->first);
        auto rest_serialized = serialize_stmt(builder, fork_stmt->rest);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Fork, Halide::Serialize::CreateFork(builder, first_serialized.first, first_serialized.second, rest_serialized.first, rest_serialized.second).Union());
    }
    case IRNodeType::Atomic: {
        auto atomic_stmt = stmt.as<Atomic>();
        auto producer_name_serialized = serialize_string(builder, atomic_stmt->producer_name);
        auto mutex_name_serialized = serialize_string(builder, atomic_stmt->mutex_name);
        auto body_serialized = serialize_stmt(builder, atomic_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Atomic, Halide::Serialize::CreateAtomic(builder, producer_name_serialized, mutex_name_serialized, body_serialized.first, body_serialized.second).Union());
    }
    default:
        user_assert(false) << "Unsupported stmt type\n";
        return std::make_pair(Halide::Serialize::Stmt::Stmt_UndefinedStmt, Halide::Serialize::CreateUndefinedStmt(builder).Union());
    }
}

std::pair<Halide::Serialize::Expr, flatbuffers::Offset<void>> Serializer::serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Expr &expr) {
    if (!expr.defined()) {
        return std::make_pair(Halide::Serialize::Expr::Expr_UndefinedExpr, Halide::Serialize::CreateUndefinedExpr(builder).Union());
    }
    switch (expr->node_type) {
    case IRNodeType::IntImm: {
        auto int_imm = expr.as<IntImm>();
        auto type_serialized = serialize_type(builder, int_imm->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_IntImm, Halide::Serialize::CreateIntImm(builder, int_imm->value, type_serialized).Union());
    }
    case IRNodeType::UIntImm: {
        auto uint_imm = expr.as<UIntImm>();
        auto type_serialized = serialize_type(builder, uint_imm->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_UIntImm, Halide::Serialize::CreateUIntImm(builder, uint_imm->value, type_serialized).Union());
    }
    case IRNodeType::FloatImm: {
        auto float_imm = expr.as<FloatImm>();
        auto type_serialized = serialize_type(builder, float_imm->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_FloatImm, Halide::Serialize::CreateFloatImm(builder, float_imm->value, type_serialized).Union());
    }
    case IRNodeType::StringImm: {
        auto string_imm = expr.as<StringImm>();
        auto value_serialized = serialize_string(builder, string_imm->value);
        return std::make_pair(Halide::Serialize::Expr::Expr_StringImm, Halide::Serialize::CreateStringImm(builder, value_serialized).Union());
    }
    case IRNodeType::Cast: {
        auto cast_expr = expr.as<Cast>();
        auto value_serialized = serialize_expr(builder, cast_expr->value);
        auto type_serialized = serialize_type(builder, cast_expr->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_Cast, Halide::Serialize::CreateCast(builder, value_serialized.first, value_serialized.second, type_serialized).Union());
    }
    case IRNodeType::Reinterpret: {
        auto reinterpret_expr = expr.as<Reinterpret>();
        auto value_serialized = serialize_expr(builder, reinterpret_expr->value);
        auto type_serialized = serialize_type(builder, reinterpret_expr->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_Reinterpret, Halide::Serialize::CreateReinterpret(builder, value_serialized.first, value_serialized.second, type_serialized).Union());
    }
    case IRNodeType::Add: {
        auto add_expr = expr.as<Add>();
        auto a_serialized = serialize_expr(builder, add_expr->a);
        auto b_serialized = serialize_expr(builder, add_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Add, Halide::Serialize::CreateAdd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Sub: {
        auto sub_expr = expr.as<Sub>();
        auto a_serialized = serialize_expr(builder, sub_expr->a);
        auto b_serialized = serialize_expr(builder, sub_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Sub, Halide::Serialize::CreateSub(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Mul: {
        auto mul_expr = expr.as<Mul>();
        auto a_serialized = serialize_expr(builder, mul_expr->a);
        auto b_serialized = serialize_expr(builder, mul_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Mul, Halide::Serialize::CreateMul(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Div: {
        auto div_expr = expr.as<Div>();
        auto a_serialized = serialize_expr(builder, div_expr->a);
        auto b_serialized = serialize_expr(builder, div_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Div, Halide::Serialize::CreateDiv(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Mod: {
        auto mod_expr = expr.as<Mod>();
        auto a_serialized = serialize_expr(builder, mod_expr->a);
        auto b_serialized = serialize_expr(builder, mod_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Mod, Halide::Serialize::CreateMod(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Min: {
        auto min_expr = expr.as<Min>();
        auto a_serialized = serialize_expr(builder, min_expr->a);
        auto b_serialized = serialize_expr(builder, min_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Min, Halide::Serialize::CreateMin(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Max: {
        auto max_expr = expr.as<Max>();
        auto a_serialized = serialize_expr(builder, max_expr->a);
        auto b_serialized = serialize_expr(builder, max_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Max, Halide::Serialize::CreateMax(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::EQ: {
        auto eq_expr = expr.as<EQ>();
        auto a = eq_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = eq_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serialize::Expr::Expr_EQ, Halide::Serialize::CreateEQ(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::NE: {
        auto ne_expr = expr.as<NE>();
        auto a = ne_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = ne_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serialize::Expr::Expr_NE, Halide::Serialize::CreateNE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::LT: {
        auto lt_expr = expr.as<LT>();
        auto a_serialized = serialize_expr(builder, lt_expr->a);
        auto b_serialized = serialize_expr(builder, lt_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_LT, Halide::Serialize::CreateLT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::LE: {
        auto le_expr = expr.as<LE>();
        auto a_serialized = serialize_expr(builder, le_expr->a);
        auto b_serialized = serialize_expr(builder, le_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_LE, Halide::Serialize::CreateLE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::GT: {
        auto gt_expr = expr.as<GT>();
        auto a_serialized = serialize_expr(builder, gt_expr->a);
        auto b_serialized = serialize_expr(builder, gt_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_GT, Halide::Serialize::CreateGT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::GE: {
        auto ge_expr = expr.as<GE>();
        auto a_serialized = serialize_expr(builder, ge_expr->a);
        auto b_serialized = serialize_expr(builder, ge_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_GE, Halide::Serialize::CreateGE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::And: {
        auto and_expr = expr.as<And>();
        auto a_serialized = serialize_expr(builder, and_expr->a);
        auto b_serialized = serialize_expr(builder, and_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_And, Halide::Serialize::CreateAnd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Or: {
        auto or_expr = expr.as<Or>();
        auto a_serialized = serialize_expr(builder, or_expr->a);
        auto b_serialized = serialize_expr(builder, or_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Or, Halide::Serialize::CreateOr(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case IRNodeType::Not: {
        auto not_expr = expr.as<Not>();
        auto a_serialized = serialize_expr(builder, not_expr->a);
        return std::make_pair(Halide::Serialize::Expr::Expr_Not, Halide::Serialize::CreateNot(builder, a_serialized.first, a_serialized.second).Union());
    }
    case IRNodeType::Select: {
        auto select_expr = expr.as<Select>();
        auto condition_serialized = serialize_expr(builder, select_expr->condition);
        auto true_value_serialized = serialize_expr(builder, select_expr->true_value);
        auto false_value_serialized = serialize_expr(builder, select_expr->false_value);
        return std::make_pair(Halide::Serialize::Expr::Expr_Select, Halide::Serialize::CreateSelect(builder, condition_serialized.first, condition_serialized.second, true_value_serialized.first, true_value_serialized.second, false_value_serialized.first, false_value_serialized.second).Union());
    }
    case IRNodeType::Load: {
        auto load_expr = expr.as<Load>();
        auto name_serialized = serialize_string(builder, load_expr->name);
        auto predicate_serialized = serialize_expr(builder, load_expr->predicate);
        auto index_serialized = serialize_expr(builder, load_expr->index);
        std::string image_name = load_expr->image.defined() ? load_expr->image.name() : "";
        if (load_expr->image.defined() && buffers_in_pipeline.find(image_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[image_name] = load_expr->image;
        }
        auto image_name_serialized = serialize_string(builder, image_name);
        std::string param_name = load_expr->param.defined() ? load_expr->param.name() : "";
        if (load_expr->param.defined() && parameters_in_pipeline.find(param_name) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[param_name] = load_expr->param;
        }
        auto param_name_serialized = serialize_string(builder, param_name);
        auto alignment_serialized = serialize_modulus_remainder(builder, load_expr->alignment);
        auto type_serialized = serialize_type(builder, load_expr->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_Load, Halide::Serialize::CreateLoad(builder, name_serialized, predicate_serialized.first, predicate_serialized.second, index_serialized.first, index_serialized.second, image_name_serialized, param_name_serialized, alignment_serialized, type_serialized).Union());
    }
    case IRNodeType::Ramp: {
        auto ramp_expr = expr.as<Ramp>();
        auto base_serialized = serialize_expr(builder, ramp_expr->base);
        auto stride_serialized = serialize_expr(builder, ramp_expr->stride);
        auto lanes = ramp_expr->lanes;
        return std::make_pair(Halide::Serialize::Expr::Expr_Ramp, Halide::Serialize::CreateRamp(builder, base_serialized.first, base_serialized.second, stride_serialized.first, stride_serialized.second, lanes).Union());
    }
    case IRNodeType::Broadcast: {
        auto broadcast_expr = expr.as<Broadcast>();
        auto value_serialized = serialize_expr(builder, broadcast_expr->value);
        auto lanes = broadcast_expr->lanes;
        return std::make_pair(Halide::Serialize::Expr::Expr_Broadcast, Halide::Serialize::CreateBroadcast(builder, value_serialized.first, value_serialized.second, lanes).Union());
    }
    case IRNodeType::Let: {
        auto let_expr = expr.as<Let>();
        auto name_serialized = serialize_string(builder, let_expr->name);
        auto value_serialized = serialize_expr(builder, let_expr->value);
        auto body_serialized = serialize_expr(builder, let_expr->body);
        return std::make_pair(Halide::Serialize::Expr::Expr_Let, Halide::Serialize::CreateLet(builder, name_serialized, value_serialized.first, value_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case IRNodeType::Call: {
        auto call_expr = expr.as<Call>();
        auto name_serialized = serialize_string(builder, call_expr->name);
        auto args = call_expr->args;
        std::vector<uint8_t> args_types;
        args_types.reserve(args.size());
        std::vector<flatbuffers::Offset<void>> args_serialized;
        args_serialized.reserve(args.size());
        for (const auto &arg : args) {
            auto arg_serialized = serialize_expr(builder, arg);
            args_types.push_back(arg_serialized.first);
            args_serialized.push_back(arg_serialized.second);
        }
        auto call_type = serialize_call_type(call_expr->call_type);
        int func_index = -1;
        if (call_expr->func.defined() && this->func_mappings.find(Function(call_expr->func).name()) != this->func_mappings.end()) {
            func_index = this->func_mappings[Function(call_expr->func).name()];
        }
        auto value_index = call_expr->value_index;
        std::string image_name = call_expr->image.defined() ? call_expr->image.name() : "";
        if (call_expr->image.defined() && buffers_in_pipeline.find(image_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[image_name] = call_expr->image;
        }
        auto image_name_serialized = serialize_string(builder, image_name);
        std::string param_name = call_expr->param.defined() ? call_expr->param.name() : "";
        if (call_expr->param.defined() && external_parameters.find(param_name) == external_parameters.end()) {
            external_parameters[param_name] = call_expr->param;
        }
        auto param_name_serialized = serialize_string(builder, param_name);
        auto type_serialized = serialize_type(builder, call_expr->type);
        return std::make_pair(Halide::Serialize::Expr::Expr_Call, Halide::Serialize::CreateCall(builder, name_serialized, builder.CreateVector(args_types), builder.CreateVector(args_serialized), call_type, func_index, value_index, image_name_serialized, param_name_serialized, type_serialized).Union());
    }
    case IRNodeType::Variable: {
        auto variable_expr = expr.as<Variable>();
        auto name_serialized = serialize_string(builder, variable_expr->name);
        auto type_serialized = serialize_type(builder, variable_expr->type);
        std::string param_name = variable_expr->param.defined() ? variable_expr->param.name() : "";
        if (variable_expr->param.defined() && external_parameters.find(param_name) == external_parameters.end()) {
            external_parameters[param_name] = variable_expr->param;
        }
        auto param_name_serialized = serialize_string(builder, param_name);
        std::string image_name = variable_expr->image.defined() ? variable_expr->image.name() : "";
        if (variable_expr->image.defined() && buffers_in_pipeline.find(image_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[image_name] = variable_expr->image;
        }
        auto image_name_serialized = serialize_string(builder, image_name);
        auto reduction_domain_serialized = serialize_reduction_domain(builder, variable_expr->reduction_domain);
        return std::make_pair(Halide::Serialize::Expr::Expr_Variable, Halide::Serialize::CreateVariable(builder, name_serialized, type_serialized, param_name_serialized, image_name_serialized, reduction_domain_serialized).Union());
    }
    case IRNodeType::Shuffle: {
        auto shuffle_expr = expr.as<Shuffle>();
        auto vectors = shuffle_expr->vectors;
        std::vector<uint8_t> vectors_types;
        vectors_types.reserve(vectors.size());
        std::vector<flatbuffers::Offset<void>> vectors_serialized;
        vectors_serialized.reserve(vectors.size());
        for (const auto &vector : vectors) {
            auto vector_serialized = serialize_expr(builder, vector);
            vectors_types.push_back(vector_serialized.first);
            vectors_serialized.push_back(vector_serialized.second);
        }
        auto indices = shuffle_expr->indices;
        return std::make_pair(Halide::Serialize::Expr::Expr_Shuffle, Halide::Serialize::CreateShuffle(builder, builder.CreateVector(vectors_types), builder.CreateVector(vectors_serialized), builder.CreateVector(indices)).Union());
    }
    case IRNodeType::VectorReduce: {
        auto vector_reduce_expr = expr.as<VectorReduce>();
        auto value_serialized = serialize_expr(builder, vector_reduce_expr->value);
        auto reduction_op_serialized = serialize_vector_reduce_op(vector_reduce_expr->op);
        int lanes = vector_reduce_expr->type.lanes();
        return std::make_pair(Halide::Serialize::Expr::Expr_VectorReduce, Halide::Serialize::CreateVectorReduce(builder, value_serialized.first, value_serialized.second, reduction_op_serialized, lanes).Union());
    }
    default:
        user_assert(false) << "Unsupported Expr type\n";
        return std::make_pair(Halide::Serialize::Expr::Expr_UndefinedExpr, Halide::Serialize::CreateUndefinedExpr(builder).Union());
    }
}

flatbuffers::Offset<Halide::Serialize::Func> Serializer::serialize_function(flatbuffers::FlatBufferBuilder &builder, const Function &function) {
    auto name_serialized = serialize_string(builder, function.name());

    auto origin_name_serialized = serialize_string(builder, function.origin_name());

    std::vector<Type> output_types = function.output_types();
    std::vector<flatbuffers::Offset<Halide::Serialize::Type>> output_types_serialized;
    output_types_serialized.reserve(output_types.size());
    for (const auto &type : output_types) {
        output_types_serialized.push_back(serialize_type(builder, type));
    }
    auto output_types_vector = builder.CreateVector(output_types_serialized);

    std::vector<Type> required_types = function.required_types();
    std::vector<flatbuffers::Offset<Halide::Serialize::Type>> required_types_serialized;
    required_types_serialized.reserve(required_types.size());
    for (const auto &type : required_types) {
        required_types_serialized.push_back(serialize_type(builder, type));
    }
    auto required_types_vector = builder.CreateVector(required_types_serialized);

    int required_dim = function.required_dimensions();

    std::vector<std::string> args = function.args();
    std::vector<flatbuffers::Offset<flatbuffers::String>> args_serialized;
    args_serialized.reserve(args.size());
    for (const auto &arg : args) {
        args_serialized.push_back(serialize_string(builder, arg));
    }
    auto args_vector = builder.CreateVector(args_serialized);

    auto func_schedule_serialized = serialize_func_schedule(builder, function.schedule());
    auto init_def_serialized = serialize_definition(builder, function.definition());
    std::vector<flatbuffers::Offset<Halide::Serialize::Definition>> updates_serialized;
    for (const auto &update : function.updates()) {
        updates_serialized.push_back(serialize_definition(builder, update));
    }
    auto debug_file_serialized = serialize_string(builder, function.debug_file());

    std::vector<flatbuffers::Offset<flatbuffers::String>> output_buffers_names_serialized;
    output_buffers_names_serialized.reserve(function.output_buffers().size());
    for (const auto &output_buffer : function.output_buffers()) {
        std::string output_buffer_name = output_buffer.defined() ? output_buffer.name() : "";
        if (output_buffer.defined() && parameters_in_pipeline.find(output_buffer_name) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[output_buffer_name] = output_buffer;
        }
        output_buffers_names_serialized.push_back(serialize_string(builder, output_buffer_name));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::ExternFuncArgument>> extern_arguments_serialized;
    extern_arguments_serialized.reserve(function.extern_arguments().size());
    for (const auto &extern_argument : function.extern_arguments()) {
        extern_arguments_serialized.push_back(serialize_extern_func_argument(builder, extern_argument));
    }

    auto extern_function_name_serialized = serialize_string(builder, function.extern_function_name());
    auto extern_mangling_serialized = serialize_name_mangling(function.extern_definition_name_mangling());
    auto extern_function_device_api_serialized = serialize_device_api(function.extern_function_device_api());
    auto extern_proxy_expr_serialized = serialize_expr(builder, function.extern_definition_proxy_expr());
    bool trace_loads = function.is_tracing_loads();
    bool trace_stores = function.is_tracing_stores();
    bool trace_realizations = function.is_tracing_realizations();
    std::vector<flatbuffers::Offset<flatbuffers::String>> trace_tags_serialized;
    trace_tags_serialized.reserve(function.get_trace_tags().size());
    for (const auto &tag : function.get_trace_tags()) {
        trace_tags_serialized.push_back(serialize_string(builder, tag));
    }
    bool frozen = function.frozen();
    auto func = Halide::Serialize::CreateFunc(builder, name_serialized, origin_name_serialized, output_types_vector, required_types_vector, required_dim,
                                              args_vector, func_schedule_serialized, init_def_serialized, builder.CreateVector(updates_serialized), debug_file_serialized, builder.CreateVector(output_buffers_names_serialized),
                                              builder.CreateVector(extern_arguments_serialized), extern_function_name_serialized, extern_mangling_serialized, extern_function_device_api_serialized, extern_proxy_expr_serialized.first,
                                              extern_proxy_expr_serialized.second, trace_loads, trace_stores, trace_realizations, builder.CreateVector(trace_tags_serialized), frozen);
    return func;
}

flatbuffers::Offset<Halide::Serialize::Range> Serializer::serialize_range(flatbuffers::FlatBufferBuilder &builder, const Range &range) {
    auto min = range.min;
    auto min_serialized = serialize_expr(builder, min);
    auto extent = range.extent;
    auto extent_serialized = serialize_expr(builder, extent);
    auto range_obj = Halide::Serialize::CreateRange(builder, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second);
    return range_obj;
}

flatbuffers::Offset<Halide::Serialize::Bound> Serializer::serialize_bound(flatbuffers::FlatBufferBuilder &builder, const Bound &bound) {
    auto var = bound.var;
    auto var_serialized = serialize_string(builder, var);
    auto min = bound.min;
    auto min_serialized = serialize_expr(builder, min);
    auto extent = bound.extent;
    auto extent_serialized = serialize_expr(builder, extent);
    auto modulus = bound.modulus;
    auto modulus_serialized = serialize_expr(builder, modulus);
    auto remainder = bound.remainder;
    auto remainder_serialized = serialize_expr(builder, remainder);
    auto bound_obj = Halide::Serialize::CreateBound(builder, var_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second, modulus_serialized.first, modulus_serialized.second, remainder_serialized.first, remainder_serialized.second);
    return bound_obj;
}

flatbuffers::Offset<Halide::Serialize::StorageDim> Serializer::serialize_storage_dim(flatbuffers::FlatBufferBuilder &builder, const StorageDim &storage_dim) {
    auto var = storage_dim.var;
    auto var_serialized = serialize_string(builder, var);
    auto alignment = storage_dim.alignment;
    auto alignment_serialized = serialize_expr(builder, alignment);
    auto bound = storage_dim.bound;
    auto bound_serialized = serialize_expr(builder, bound);
    auto fold_factor = storage_dim.fold_factor;
    auto fold_factor_serialized = serialize_expr(builder, fold_factor);
    auto fold_forward = storage_dim.fold_forward;
    auto storage_dim_obj = Halide::Serialize::CreateStorageDim(builder, var_serialized, alignment_serialized.first, alignment_serialized.second, bound_serialized.first, bound_serialized.second, fold_factor_serialized.first, fold_factor_serialized.second, fold_forward);
    return storage_dim_obj;
}

flatbuffers::Offset<Halide::Serialize::LoopLevel> Serializer::serialize_loop_level(flatbuffers::FlatBufferBuilder &builder, const LoopLevel &loop_level) {
    auto func_name = loop_level.func_name();
    auto func_name_serialized = serialize_string(builder, func_name);
    auto stage_index = loop_level.get_stage_index();
    auto var_name = loop_level.var_name();
    auto var_name_serialized = serialize_string(builder, var_name);
    bool is_rvar = loop_level.is_rvar();
    auto locked = loop_level.locked();
    auto loop_level_obj = Halide::Serialize::CreateLoopLevel(builder, func_name_serialized, stage_index, var_name_serialized, is_rvar, locked);
    return loop_level_obj;
}

flatbuffers::Offset<Halide::Serialize::FuncSchedule> Serializer::serialize_func_schedule(flatbuffers::FlatBufferBuilder &builder, const FuncSchedule &func_schedule) {
    auto store_level = func_schedule.store_level();
    auto store_level_serialized = serialize_loop_level(builder, store_level);
    auto compute_level = func_schedule.compute_level();
    auto compute_level_serialized = serialize_loop_level(builder, compute_level);
    std::vector<flatbuffers::Offset<Halide::Serialize::StorageDim>> storage_dims_serialized;
    for (const auto &storage_dim : func_schedule.storage_dims()) {
        storage_dims_serialized.push_back(serialize_storage_dim(builder, storage_dim));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::Bound>> bounds_serialized;
    for (const auto &bound : func_schedule.bounds()) {
        bounds_serialized.push_back(serialize_bound(builder, bound));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::Bound>> estimates_serialized;
    for (const auto &estimate : func_schedule.estimates()) {
        estimates_serialized.push_back(serialize_bound(builder, estimate));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> wrappers_serialized = serialize_wrapper_refs(builder, func_schedule.wrappers());
    Halide::Serialize::MemoryType memory_type = serialize_memory_type(func_schedule.memory_type());
    auto memoized = func_schedule.memoized();
    auto async = func_schedule.async();
    auto memoize_eviction_key = func_schedule.memoize_eviction_key();
    auto memoize_eviction_key_serialized = serialize_expr(builder, memoize_eviction_key);
    return Halide::Serialize::CreateFuncSchedule(builder, store_level_serialized, compute_level_serialized, builder.CreateVector(storage_dims_serialized), builder.CreateVector(bounds_serialized),
                                                 builder.CreateVector(estimates_serialized), builder.CreateVector(wrappers_serialized), memory_type, memoized, async, memoize_eviction_key_serialized.first, memoize_eviction_key_serialized.second);
}

flatbuffers::Offset<Halide::Serialize::Specialization> Serializer::serialize_specialization(flatbuffers::FlatBufferBuilder &builder, const Specialization &specialization) {
    auto condition_serialized = serialize_expr(builder, specialization.condition);
    auto definition_serialized = serialize_definition(builder, specialization.definition);
    auto failure_message_serialized = serialize_string(builder, specialization.failure_message);
    return Halide::Serialize::CreateSpecialization(builder, condition_serialized.first, condition_serialized.second, definition_serialized, failure_message_serialized);
}

flatbuffers::Offset<Halide::Serialize::Definition> Serializer::serialize_definition(flatbuffers::FlatBufferBuilder &builder, const Definition &definition) {
    auto is_init = definition.is_init();
    auto predicate_serialized = serialize_expr(builder, definition.predicate());
    std::vector<uint8_t> values_types;
    values_types.reserve(definition.values().size());
    std::vector<flatbuffers::Offset<void>> values_serialized;
    values_serialized.reserve(definition.values().size());
    for (const auto &value : definition.values()) {
        auto value_serialized = serialize_expr(builder, value);
        values_types.push_back(value_serialized.first);
        values_serialized.push_back(value_serialized.second);
    }
    std::vector<uint8_t> args_types;
    args_types.reserve(definition.args().size());
    std::vector<flatbuffers::Offset<void>> args_serialized;
    args_serialized.reserve(definition.args().size());
    for (const auto &arg : definition.args()) {
        auto arg_serialized = serialize_expr(builder, arg);
        args_types.push_back(arg_serialized.first);
        args_serialized.push_back(arg_serialized.second);
    }
    auto stage_schedule_serialized = serialize_stage_schedule(builder, definition.schedule());
    std::vector<flatbuffers::Offset<Halide::Serialize::Specialization>> specializations_serialized;
    for (const auto &specialization : definition.specializations()) {
        specializations_serialized.push_back(serialize_specialization(builder, specialization));
    }
    auto source_location_serialized = serialize_string(builder, definition.source_location());
    return Halide::Serialize::CreateDefinition(builder, is_init, predicate_serialized.first, predicate_serialized.second, builder.CreateVector(values_types),
                                               builder.CreateVector(values_serialized), builder.CreateVector(args_types), builder.CreateVector(args_serialized),
                                               stage_schedule_serialized, builder.CreateVector(specializations_serialized), source_location_serialized);
}

flatbuffers::Offset<Halide::Serialize::ReductionVariable> Serializer::serialize_reduction_variable(flatbuffers::FlatBufferBuilder &builder, const ReductionVariable &reduction_variable) {
    auto var_serialized = serialize_string(builder, reduction_variable.var);
    auto min_serialized = serialize_expr(builder, reduction_variable.min);
    auto extent_serialized = serialize_expr(builder, reduction_variable.extent);
    return Halide::Serialize::CreateReductionVariable(builder, var_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second);
}

flatbuffers::Offset<Halide::Serialize::ReductionDomain> Serializer::serialize_reduction_domain(flatbuffers::FlatBufferBuilder &builder, const ReductionDomain &reduction_domain) {
    bool defined = reduction_domain.defined();
    if (!defined) {
        return Halide::Serialize::CreateReductionDomain(builder, defined);
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::ReductionVariable>> domain_serialized;
    for (const auto &reduction_variable : reduction_domain.domain()) {
        domain_serialized.push_back(serialize_reduction_variable(builder, reduction_variable));
    }
    auto predicate_serialized = serialize_expr(builder, reduction_domain.predicate());
    return Halide::Serialize::CreateReductionDomain(builder, defined, builder.CreateVector(domain_serialized), predicate_serialized.first, predicate_serialized.second, reduction_domain.frozen());
}

flatbuffers::Offset<Halide::Serialize::ModulusRemainder> Serializer::serialize_modulus_remainder(flatbuffers::FlatBufferBuilder &builder, const ModulusRemainder &modulus_remainder) {
    return Halide::Serialize::CreateModulusRemainder(builder, modulus_remainder.modulus, modulus_remainder.remainder);
}

flatbuffers::Offset<Halide::Serialize::PrefetchDirective> Serializer::serialize_prefetch_directive(flatbuffers::FlatBufferBuilder &builder, const PrefetchDirective &prefetch_directive) {
    auto name_serialized = serialize_string(builder, prefetch_directive.name);
    auto at_serialized = serialize_string(builder, prefetch_directive.at);
    auto from_serialized = serialize_string(builder, prefetch_directive.from);
    auto offset_serialized = serialize_expr(builder, prefetch_directive.offset);
    auto strategy_serialized = serialize_prefetch_bound_strategy(prefetch_directive.strategy);
    std::string param_name = prefetch_directive.param.defined() ? prefetch_directive.param.name() : "";
    if (prefetch_directive.param.defined() && parameters_in_pipeline.find(param_name) == parameters_in_pipeline.end()) {
        parameters_in_pipeline[param_name] = prefetch_directive.param;
    }
    auto param_name_serialized = serialize_string(builder, param_name);
    return Halide::Serialize::CreatePrefetchDirective(builder, name_serialized, at_serialized, from_serialized, offset_serialized.first, offset_serialized.second, strategy_serialized, param_name_serialized);
}

flatbuffers::Offset<Halide::Serialize::Split> Serializer::serialize_split(flatbuffers::FlatBufferBuilder &builder, const Split &split) {
    auto old_var_serialized = serialize_string(builder, split.old_var);
    auto outer_serialized = serialize_string(builder, split.outer);
    auto inner_serialized = serialize_string(builder, split.inner);
    auto factor_serialized = serialize_expr(builder, split.factor);
    auto tail_serialized = serialize_tail_strategy(split.tail);
    auto inner_to_outer_serialized = serialize_split_type(split.split_type);
    return Halide::Serialize::CreateSplit(builder, old_var_serialized, outer_serialized, inner_serialized, factor_serialized.first, factor_serialized.second, tail_serialized, inner_to_outer_serialized);
}

flatbuffers::Offset<Halide::Serialize::Dim> Serializer::serialize_dim(flatbuffers::FlatBufferBuilder &builder, const Dim &dim) {
    auto var_serialized = serialize_string(builder, dim.var);
    auto for_type_serialized = serialize_for_type(dim.for_type);
    auto device_api_serialized = serialize_device_api(dim.device_api);
    auto dim_type_serialized = serialize_dim_type(dim.dim_type);
    return Halide::Serialize::CreateDim(builder, var_serialized, for_type_serialized, device_api_serialized, dim_type_serialized);
}

flatbuffers::Offset<Halide::Serialize::FuseLoopLevel> Serializer::serialize_fuse_loop_level(flatbuffers::FlatBufferBuilder &builder, const FuseLoopLevel &fuse_loop_level) {
    auto fuse_level_serialized = serialize_loop_level(builder, fuse_loop_level.level);
    std::vector<flatbuffers::Offset<flatbuffers::String>> align_dimension_names_serialized;
    std::vector<uint8_t> align_strategies_serialized;
    for (const auto &align : fuse_loop_level.align) {
        align_dimension_names_serialized.push_back(serialize_string(builder, align.first));
        align_strategies_serialized.push_back(serialize_loop_align_strategy(align.second));
    }
    return Halide::Serialize::CreateFuseLoopLevel(builder, fuse_level_serialized, builder.CreateVector(align_dimension_names_serialized), builder.CreateVector(align_strategies_serialized));
}

flatbuffers::Offset<Halide::Serialize::FusedPair> Serializer::serialize_fused_pair(flatbuffers::FlatBufferBuilder &builder, const FusedPair &fused_pair) {
    auto func_1_serialized = serialize_string(builder, fused_pair.func_1);
    auto func_2_serialized = serialize_string(builder, fused_pair.func_2);
    auto var_name_serialized = serialize_string(builder, fused_pair.var_name);
    return Halide::Serialize::CreateFusedPair(builder, func_1_serialized, func_2_serialized, fused_pair.stage_1, fused_pair.stage_2, var_name_serialized);
}

flatbuffers::Offset<Halide::Serialize::StageSchedule> Serializer::serialize_stage_schedule(flatbuffers::FlatBufferBuilder &builder, const StageSchedule &stage_schedule) {
    std::vector<flatbuffers::Offset<Halide::Serialize::ReductionVariable>> rvars_serialized;
    rvars_serialized.reserve(stage_schedule.rvars().size());
    for (const auto &rvar : stage_schedule.rvars()) {
        rvars_serialized.push_back(serialize_reduction_variable(builder, rvar));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::Split>> splits_serialized;
    splits_serialized.reserve(stage_schedule.splits().size());
    for (const auto &split : stage_schedule.splits()) {
        splits_serialized.push_back(serialize_split(builder, split));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::Dim>> dims_serialized;
    dims_serialized.reserve(stage_schedule.dims().size());
    for (const auto &dim : stage_schedule.dims()) {
        dims_serialized.push_back(serialize_dim(builder, dim));
    }
    std::vector<flatbuffers::Offset<Halide::Serialize::PrefetchDirective>> prefetches_serialized;
    prefetches_serialized.reserve(stage_schedule.prefetches().size());
    for (const auto &prefetch : stage_schedule.prefetches()) {
        prefetches_serialized.push_back(serialize_prefetch_directive(builder, prefetch));
    }
    auto fuse_level_serialized = serialize_fuse_loop_level(builder, stage_schedule.fuse_level());
    std::vector<flatbuffers::Offset<Halide::Serialize::FusedPair>> fused_pairs_serialized;
    fused_pairs_serialized.reserve(stage_schedule.fused_pairs().size());
    for (const auto &fused_pair : stage_schedule.fused_pairs()) {
        fused_pairs_serialized.push_back(serialize_fused_pair(builder, fused_pair));
    }
    bool touched = stage_schedule.touched();
    bool allow_race_conditions = stage_schedule.allow_race_conditions();
    bool atomic = stage_schedule.atomic();
    bool override_atomic_associativity_test = stage_schedule.override_atomic_associativity_test();
    return Halide::Serialize::CreateStageSchedule(builder, builder.CreateVector(rvars_serialized), builder.CreateVector(splits_serialized), builder.CreateVector(dims_serialized),
                                                  builder.CreateVector(prefetches_serialized), fuse_level_serialized, builder.CreateVector(fused_pairs_serialized),
                                                  touched, allow_race_conditions, atomic, override_atomic_associativity_test);
}

flatbuffers::Offset<Halide::Serialize::BufferConstraint> Serializer::serialize_buffer_constraint(flatbuffers::FlatBufferBuilder &builder, const BufferConstraint &buffer_constraint) {
    auto min_serialized = serialize_expr(builder, buffer_constraint.min);
    auto extent_serialized = serialize_expr(builder, buffer_constraint.extent);
    auto stride_serialized = serialize_expr(builder, buffer_constraint.stride);
    auto min_estimate_serialized = serialize_expr(builder, buffer_constraint.min_estimate);
    auto extent_estimate_serialized = serialize_expr(builder, buffer_constraint.extent_estimate);
    return Halide::Serialize::CreateBufferConstraint(builder, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second, stride_serialized.first, stride_serialized.second, min_estimate_serialized.first, min_estimate_serialized.second, extent_estimate_serialized.first, extent_estimate_serialized.second);
}

flatbuffers::Offset<Halide::Serialize::Parameter> Serializer::serialize_parameter(flatbuffers::FlatBufferBuilder &builder, const Parameter &parameter) {
    // TODO: check defined for every other class that has a defined
    bool defined = parameter.defined();
    if (!defined) {
        return Halide::Serialize::CreateParameter(builder, defined);
    }
    auto type_serialized = serialize_type(builder, parameter.type());
    int dimensions = parameter.dimensions();
    auto name_serialized = serialize_string(builder, parameter.name());
    bool is_buffer = parameter.is_buffer();
    // because of check_is_buffer()/check_is_scalar(), we cannot serialize all fields at the same time
    // based on if the parameter is a buffer, we serialize different fields, fill 0 or default values for the unavailable fields
    if (is_buffer) {
        return Halide::Serialize::CreateParameter(builder, defined, is_buffer, type_serialized, dimensions, name_serialized);
    } else {
        uint64_t data = parameter.scalar_raw_value();
        auto scalar_default_serialized = serialize_expr(builder, parameter.default_value());
        auto scalar_min_serialized = serialize_expr(builder, parameter.min_value());
        auto scalar_max_serialized = serialize_expr(builder, parameter.max_value());
        auto scalar_estimate_serialized = serialize_expr(builder, parameter.estimate());
        return Halide::Serialize::CreateParameter(builder, defined, is_buffer, type_serialized, dimensions, name_serialized, data,
                                                  scalar_default_serialized.first, scalar_default_serialized.second, scalar_min_serialized.first,
                                                  scalar_min_serialized.second, scalar_max_serialized.first, scalar_max_serialized.second,
                                                  scalar_estimate_serialized.first, scalar_estimate_serialized.second);
    }
}

flatbuffers::Offset<Halide::Serialize::ExternFuncArgument> Serializer::serialize_extern_func_argument(flatbuffers::FlatBufferBuilder &builder, const ExternFuncArgument &extern_func_argument) {
    auto arg_type_serialized = serialize_extern_func_argument_type(extern_func_argument.arg_type);
    if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::UndefinedArg) {
        return Halide::Serialize::CreateExternFuncArgument(builder, arg_type_serialized);
    } else if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::FuncArg) {
        int func_index = -1;
        if (this->func_mappings.find(Function(extern_func_argument.func).name()) != this->func_mappings.end()) {
            func_index = this->func_mappings[Function(extern_func_argument.func).name()];
        }
        return Halide::Serialize::CreateExternFuncArgument(builder, arg_type_serialized, func_index);
    } else if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::BufferArg) {
        std::string buffer_name = extern_func_argument.buffer.defined() ? extern_func_argument.buffer.name() : "";
        if (extern_func_argument.buffer.defined() && buffers_in_pipeline.find(buffer_name) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[buffer_name] = extern_func_argument.buffer;
        }
        auto buffer_name_serialized = serialize_string(builder, buffer_name);
        return Halide::Serialize::CreateExternFuncArgument(builder, arg_type_serialized, -1, buffer_name_serialized);
    } else if (extern_func_argument.arg_type == ExternFuncArgument::ArgType::ExprArg) {
        auto expr_serialized = serialize_expr(builder, extern_func_argument.expr);
        return Halide::Serialize::CreateExternFuncArgument(builder, arg_type_serialized, -1, 0, expr_serialized.first, expr_serialized.second);
    } else {
        std::string image_param_name = extern_func_argument.image_param.defined() ? extern_func_argument.image_param.name() : "";
        if (extern_func_argument.defined() && external_parameters.find(image_param_name) == external_parameters.end()) {
            external_parameters[image_param_name] = extern_func_argument.image_param;
        }
        auto image_param_name_serialized = serialize_string(builder, image_param_name);
        return Halide::Serialize::CreateExternFuncArgument(builder, arg_type_serialized, -1, 0, Halide::Serialize::Expr_NONE, 0, image_param_name_serialized);
    }
}

flatbuffers::Offset<Halide::Serialize::Buffer> Serializer::serialize_buffer(flatbuffers::FlatBufferBuilder &builder, const Buffer<> &buffer) {
    if (!buffer.defined()) {
        return Halide::Serialize::CreateBuffer(builder, false);
    }
    debug(0) << "serialize buffer: " << buffer.name() << "\n";
    auto name_serialized = serialize_string(builder, buffer.name());
    auto type_serialized = serialize_type(builder, buffer.type());
    int32_t dimensions = buffer.dimensions();
    std::vector<flatbuffers::Offset<Halide::Serialize::BufferDimension>> buffer_dimensions_serialized;
    for (int i = 0; i < buffer.dimensions(); ++i) {
        int32_t min = buffer.dim(i).min();
        int32_t extent = buffer.dim(i).extent();
        int32_t stride = buffer.dim(i).stride();
        buffer_dimensions_serialized.push_back(Halide::Serialize::CreateBufferDimension(builder, min, extent, stride));
    }
    std::vector<uint8_t> data;
    data.resize(buffer.size_in_bytes());
    memcpy(data.data(), buffer.data(), buffer.size_in_bytes());
    return Halide::Serialize::CreateBuffer(builder, true, name_serialized, type_serialized, dimensions, builder.CreateVector(buffer_dimensions_serialized), builder.CreateVector(data));
}

std::vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> Serializer::serialize_wrapper_refs(flatbuffers::FlatBufferBuilder &builder, const std::map<std::string, FunctionPtr> &wrappers) {
    std::vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> wrapper_refs_serialized;
    wrapper_refs_serialized.reserve(wrappers.size());
    for (const auto &wrapper : wrappers) {
        auto wrapper_name_serialized = serialize_string(builder, wrapper.first);
        int func_index = -1;
        if (this->func_mappings.find(Function(wrapper.second).name()) != this->func_mappings.end()) {
            func_index = this->func_mappings[Function(wrapper.second).name()];
        }
        wrapper_refs_serialized.push_back(Halide::Serialize::CreateWrapperRef(builder, wrapper_name_serialized, func_index));
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

void Serializer::serialize(const Pipeline &pipeline, const std::string &filename) {
    flatbuffers::FlatBufferBuilder builder(1024);

    // extract the DAG, unwarp function from Funcs
    std::vector<Function> outputs_functions;
    for (const Func &func : pipeline.outputs()) {
        outputs_functions.push_back(func.function());
    }
    std::map<std::string, Function> env = build_environment(outputs_functions);
    build_function_mappings(env);

    std::vector<flatbuffers::Offset<flatbuffers::String>> func_names_in_order_serialized;
    std::vector<flatbuffers::Offset<Halide::Serialize::Func>> funcs_serialized;
    for (const auto &entry : env) {
        func_names_in_order_serialized.push_back(serialize_string(builder, entry.first));
        funcs_serialized.push_back(this->serialize_function(builder, entry.second));
    }

    auto outputs = pipeline.outputs();
    std::vector<flatbuffers::Offset<flatbuffers::String>> output_names_serialized;
    output_names_serialized.reserve(outputs.size());
    for (const auto &output : outputs) {
        output_names_serialized.push_back(serialize_string(builder, output.name()));
    }
    auto requirements = pipeline.requirements();
    std::vector<flatbuffers::Offset<void>> requirements_serialized;
    requirements_serialized.reserve(requirements.size());
    std::vector<uint8_t> requirements_types;
    requirements_types.reserve(requirements.size());
    for (const auto &stmt : requirements) {
        auto stmt_serialized = serialize_stmt(builder, stmt);
        requirements_serialized.push_back(stmt_serialized.second);
        requirements_types.push_back(stmt_serialized.first);
    }
    auto requirements_vector = builder.CreateVector(requirements_serialized);
    auto requirements_types_vector = builder.CreateVector(requirements_types);

    // For Parameters and buffers, to prevent we serialize the same object multiple times, we use a map to store the unique
    // objects seen in the whole pipeline and only serialize their names (strings) at the use sites,
    // then we do the actual serialization of the unique objects once
    std::vector<flatbuffers::Offset<Halide::Serialize::Parameter>> parameters_serialized;
    parameters_serialized.reserve(parameters_in_pipeline.size());
    for (const auto &param : parameters_in_pipeline) {
        // we only serialize internal parameters with the pipeline
        if (external_parameters.find(param.first) == external_parameters.end()) {
            parameters_serialized.push_back(serialize_parameter(builder, param.second));
        }
    }

    std::vector<flatbuffers::Offset<Halide::Serialize::Buffer>> buffers_serialized;
    buffers_serialized.reserve(buffers_in_pipeline.size());
    for (const auto &buffer : buffers_in_pipeline) {
        buffers_serialized.push_back(serialize_buffer(builder, buffer.second));
    }

    auto pipeline_obj = Halide::Serialize::CreatePipeline(builder, builder.CreateVector(funcs_serialized), builder.CreateVector(output_names_serialized),
                                                          requirements_types_vector, requirements_vector, builder.CreateVector(func_names_in_order_serialized),
                                                          builder.CreateVector(parameters_serialized), builder.CreateVector(buffers_serialized));
    builder.Finish(pipeline_obj);

    uint8_t *buf = builder.GetBufferPointer();
    int size = builder.GetSize();
    std::ofstream out(filename, std::ios::out | std::ios::binary);
    if (!out) {
        user_assert(false) << "failed to open file " << filename << "\n";
        exit(1);
    }
    out.write((char *)(buf), size);
    out.close();
}

}  // namespace Internal

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::unordered_map<std::string, Internal::Parameter> &params) {
    Internal::Serializer serializer;
    serializer.serialize(pipeline, filename);
    params = serializer.get_external_parameters();
}

}  // namespace Halide
