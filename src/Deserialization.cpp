#include "Deserialization.h"
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "Schedule.h"
#include "halide_ir_generated.h"

namespace Halide {
namespace Internal {
class Deserializer {
public:
    Deserializer() = default;

    Deserializer(const std::unordered_map<std::string, Parameter> &params)
        : non_serialized_parameters(params) {
    }

    Pipeline deserialize(const std::string &filename);

private:
    std::unordered_map<int32_t, FunctionPtr> reverse_function_mappings;

    std::unordered_map<std::string, Parameter> parameters_in_pipeline;

    std::unordered_map<std::string, Buffer<>> buffers_in_pipeline;

    std::unordered_map<std::string, Parameter> non_serialized_parameters;

    // helper functions to deserialize each type of object
    MemoryType deserialize_memory_type(const Serialize::MemoryType memory_type);

    ForType deserialize_for_type(const Serialize::ForType for_type);

    DeviceAPI deserialize_device_api(const Serialize::DeviceAPI device_api);

    Call::CallType deserialize_call_type(const Serialize::CallType call_type);

    VectorReduce::Operator deserialize_vector_reduce_op(const Serialize::VectorReduceOp vector_reduce_op);

    PrefetchBoundStrategy deserialize_prefetch_bound_strategy(const Serialize::PrefetchBoundStrategy prefetch_bound_strategy);

    NameMangling deserialize_name_mangling(const Serialize::NameMangling name_mangling);

    TailStrategy deserialize_tail_strategy(const Serialize::TailStrategy tail_strategy);

    Split::SplitType deserialize_split_type(const Serialize::SplitType split_type);

    DimType deserialize_dim_type(const Serialize::DimType dim_type);

    LoopAlignStrategy deserialize_loop_align_strategy(const Serialize::LoopAlignStrategy loop_align_strategy);

    ExternFuncArgument::ArgType deserialize_extern_func_argument_type(const Serialize::ExternFuncArgumentType extern_func_argument_type);

    std::string deserialize_string(const flatbuffers::String *str);

    Type deserialize_type(const Serialize::Type *type);

    void deserialize_function(const Serialize::Func *function, Function &hl_function);

    Stmt deserialize_stmt(Serialize::Stmt type_code, const void *stmt);

    Expr deserialize_expr(Serialize::Expr type_code, const void *expr);

    std::vector<Expr> deserialize_expr_vector(const flatbuffers::Vector<uint8_t> *exprs_types, const flatbuffers::Vector<flatbuffers::Offset<void>> *exprs_serialized);

    Range deserialize_range(const Serialize::Range *range);

    Bound deserialize_bound(const Serialize::Bound *bound);

    StorageDim deserialize_storage_dim(const Serialize::StorageDim *storage_dim);

    LoopLevel deserialize_loop_level(const Serialize::LoopLevel *loop_level);

    FuncSchedule deserialize_func_schedule(const Serialize::FuncSchedule *func_schedule);

    Specialization deserialize_specialization(const Serialize::Specialization *specialization);

    Definition deserialize_definition(const Serialize::Definition *definition);

    ReductionVariable deserialize_reduction_variable(const Serialize::ReductionVariable *reduction_variable);

    ReductionDomain deserialize_reduction_domain(const Serialize::ReductionDomain *reduction_domain);

    ModulusRemainder deserialize_modulus_remainder(const Serialize::ModulusRemainder *modulus_remainder);

    PrefetchDirective deserialize_prefetch_directive(const Serialize::PrefetchDirective *prefetch_directive);

    Split deserialize_split(const Serialize::Split *split);

    Dim deserialize_dim(const Serialize::Dim *dim);

    FuseLoopLevel deserialize_fuse_loop_level(const Serialize::FuseLoopLevel *fuse_loop_level);

    FusedPair deserialize_fused_pair(const Serialize::FusedPair *fused_pair);

    StageSchedule deserialize_stage_schedule(const Serialize::StageSchedule *stage_schedule);

    BufferConstraint deserialize_buffer_constraint(const Serialize::BufferConstraint *buffer_constraint);

    Parameter deserialize_parameter(const Serialize::Parameter *parameter);

    ExternFuncArgument deserialize_extern_func_argument(const Serialize::ExternFuncArgument *extern_func_argument);

    std::map<std::string, FunctionPtr> deserialize_wrapper_refs(const flatbuffers::Vector<flatbuffers::Offset<Serialize::WrapperRef>> *wrappers);

    Buffer<> deserialize_buffer(const Serialize::Buffer *buffer);

    void build_reverse_function_mappings(const std::vector<Function> &functions);
};

std::string Deserializer::deserialize_string(const flatbuffers::String *str) {
    user_assert(str != nullptr) << "deserializing a null string\n";
    return str->str();
}

MemoryType Deserializer::deserialize_memory_type(const Serialize::MemoryType memory_type) {
    switch (memory_type) {
    case Serialize::MemoryType::MemoryType_Auto:
        return MemoryType::Auto;
    case Serialize::MemoryType::MemoryType_Heap:
        return MemoryType::Heap;
    case Serialize::MemoryType::MemoryType_Stack:
        return MemoryType::Stack;
    case Serialize::MemoryType::MemoryType_Register:
        return MemoryType::Register;
    case Serialize::MemoryType::MemoryType_GPUShared:
        return MemoryType::GPUShared;
    case Serialize::MemoryType::MemoryType_GPUTexture:
        return MemoryType::GPUTexture;
    case Serialize::MemoryType::MemoryType_LockedCache:
        return MemoryType::LockedCache;
    case Serialize::MemoryType::MemoryType_VTCM:
        return MemoryType::VTCM;
    case Serialize::MemoryType::MemoryType_AMXTile:
        return MemoryType::AMXTile;
    default:
        user_assert(false) << "unknown memory type " << memory_type << "\n";
        return MemoryType::Auto;
    }
}

ForType Deserializer::deserialize_for_type(const Serialize::ForType for_type) {
    switch (for_type) {
    case Serialize::ForType::ForType_Serial:
        return ForType::Serial;
    case Serialize::ForType::ForType_Parallel:
        return ForType::Parallel;
    case Serialize::ForType::ForType_Vectorized:
        return ForType::Vectorized;
    case Serialize::ForType::ForType_Unrolled:
        return ForType::Unrolled;
    case Serialize::ForType::ForType_Extern:
        return ForType::Extern;
    case Serialize::ForType::ForType_GPUBlock:
        return ForType::GPUBlock;
    case Serialize::ForType::ForType_GPUThread:
        return ForType::GPUThread;
    case Serialize::ForType::ForType_GPULane:
        return ForType::GPULane;
    default:
        user_assert(false) << "unknown for type " << for_type << "\n";
        return ForType::Serial;
    }
}

DeviceAPI Deserializer::deserialize_device_api(const Serialize::DeviceAPI device_api) {
    switch (device_api) {
    case Serialize::DeviceAPI::DeviceAPI_None:
        return DeviceAPI::None;
    case Serialize::DeviceAPI::DeviceAPI_Host:
        return DeviceAPI::Host;
    case Serialize::DeviceAPI::DeviceAPI_Default_GPU:
        return DeviceAPI::Default_GPU;
    case Serialize::DeviceAPI::DeviceAPI_CUDA:
        return DeviceAPI::CUDA;
    case Serialize::DeviceAPI::DeviceAPI_OpenCL:
        return DeviceAPI::OpenCL;
    case Serialize::DeviceAPI::DeviceAPI_OpenGLCompute:
        return DeviceAPI::OpenGLCompute;
    case Serialize::DeviceAPI::DeviceAPI_Metal:
        return DeviceAPI::Metal;
    case Serialize::DeviceAPI::DeviceAPI_Hexagon:
        return DeviceAPI::Hexagon;
    case Serialize::DeviceAPI::DeviceAPI_HexagonDma:
        return DeviceAPI::HexagonDma;
    case Serialize::DeviceAPI::DeviceAPI_D3D12Compute:
        return DeviceAPI::D3D12Compute;
    case Serialize::DeviceAPI::DeviceAPI_Vulkan:
        return DeviceAPI::Vulkan;
    case Serialize::DeviceAPI::DeviceAPI_WebGPU:
        return DeviceAPI::WebGPU;
    default:
        user_assert(false) << "unknown device api " << device_api << "\n";
        return DeviceAPI::None;
    }
}

Call::CallType Deserializer::deserialize_call_type(const Serialize::CallType call_type) {
    switch (call_type) {
    case Serialize::CallType::CallType_Image:
        return Call::CallType::Image;
    case Serialize::CallType::CallType_Extern:
        return Call::CallType::Extern;
    case Serialize::CallType::CallType_ExternCPlusPlus:
        return Call::CallType::ExternCPlusPlus;
    case Serialize::CallType::CallType_PureExtern:
        return Call::CallType::PureExtern;
    case Serialize::CallType::CallType_Halide:
        return Call::CallType::Halide;
    case Serialize::CallType::CallType_Intrinsic:
        return Call::CallType::Intrinsic;
    case Serialize::CallType::CallType_PureIntrinsic:
        return Call::CallType::PureIntrinsic;
    default:
        user_assert(false) << "unknown call type " << call_type << "\n";
        return Call::CallType::Image;
    }
}

VectorReduce::Operator Deserializer::deserialize_vector_reduce_op(const Serialize::VectorReduceOp vector_reduce_op) {
    switch (vector_reduce_op) {
    case Serialize::VectorReduceOp::VectorReduceOp_Add:
        return VectorReduce::Operator::Add;
    case Serialize::VectorReduceOp::VectorReduceOp_SaturatingAdd:
        return VectorReduce::Operator::SaturatingAdd;
    case Serialize::VectorReduceOp::VectorReduceOp_Mul:
        return VectorReduce::Operator::Mul;
    case Serialize::VectorReduceOp::VectorReduceOp_Min:
        return VectorReduce::Operator::Min;
    case Serialize::VectorReduceOp::VectorReduceOp_Max:
        return VectorReduce::Operator::Max;
    case Serialize::VectorReduceOp::VectorReduceOp_And:
        return VectorReduce::Operator::And;
    case Serialize::VectorReduceOp::VectorReduceOp_Or:
        return VectorReduce::Operator::Or;
    default:
        user_assert(false) << "unknown vector reduce op " << vector_reduce_op << "\n";
        return VectorReduce::Operator::Add;
    }
}

PrefetchBoundStrategy Deserializer::deserialize_prefetch_bound_strategy(const Serialize::PrefetchBoundStrategy prefetch_bound_strategy) {
    switch (prefetch_bound_strategy) {
    case Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp:
        return PrefetchBoundStrategy::Clamp;
    case Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_GuardWithIf:
        return PrefetchBoundStrategy::GuardWithIf;
    case Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_NonFaulting:
        return PrefetchBoundStrategy::NonFaulting;
    default:
        user_assert(false) << "unknown prefetch bound strategy " << prefetch_bound_strategy << "\n";
        return PrefetchBoundStrategy::Clamp;
    }
}

NameMangling Deserializer::deserialize_name_mangling(const Serialize::NameMangling name_mangling) {
    switch (name_mangling) {
    case Serialize::NameMangling::NameMangling_Default:
        return NameMangling::Default;
    case Serialize::NameMangling::NameMangling_C:
        return NameMangling::C;
    case Serialize::NameMangling::NameMangling_CPlusPlus:
        return NameMangling::CPlusPlus;
    default:
        user_assert(false) << "unknown name mangling " << name_mangling << "\n";
        return NameMangling::Default;
    }
}

TailStrategy Deserializer::deserialize_tail_strategy(const Serialize::TailStrategy tail_strategy) {
    switch (tail_strategy) {
    case Serialize::TailStrategy::TailStrategy_RoundUp:
        return TailStrategy::RoundUp;
    case Serialize::TailStrategy::TailStrategy_GuardWithIf:
        return TailStrategy::GuardWithIf;
    case Serialize::TailStrategy::TailStrategy_Predicate:
        return TailStrategy::Predicate;
    case Serialize::TailStrategy::TailStrategy_PredicateLoads:
        return TailStrategy::PredicateLoads;
    case Serialize::TailStrategy::TailStrategy_PredicateStores:
        return TailStrategy::PredicateStores;
    case Serialize::TailStrategy::TailStrategy_ShiftInwards:
        return TailStrategy::ShiftInwards;
    case Serialize::TailStrategy::TailStrategy_Auto:
        return TailStrategy::Auto;
    default:
        user_assert(false) << "unknown tail strategy " << tail_strategy << "\n";
        return TailStrategy::RoundUp;
    }
}

Split::SplitType Deserializer::deserialize_split_type(const Serialize::SplitType split_type) {
    switch (split_type) {
    case Serialize::SplitType::SplitType_SplitVar:
        return Split::SplitType::SplitVar;
    case Serialize::SplitType::SplitType_RenameVar:
        return Split::SplitType::RenameVar;
    case Serialize::SplitType::SplitType_FuseVars:
        return Split::SplitType::FuseVars;
    case Serialize::SplitType::SplitType_PurifyRVar:
        return Split::SplitType::PurifyRVar;
    default:
        user_assert(false) << "unknown split type " << split_type << "\n";
        return Split::SplitType::SplitVar;
    }
}

DimType Deserializer::deserialize_dim_type(const Serialize::DimType dim_type) {
    switch (dim_type) {
    case Serialize::DimType::DimType_PureVar:
        return DimType::PureVar;
    case Serialize::DimType::DimType_PureRVar:
        return DimType::PureRVar;
    case Serialize::DimType::DimType_ImpureRVar:
        return DimType::ImpureRVar;
    default:
        user_assert(false) << "unknown dim type " << dim_type << "\n";
        return DimType::PureVar;
    }
}

LoopAlignStrategy Deserializer::deserialize_loop_align_strategy(const Serialize::LoopAlignStrategy loop_align_strategy) {
    switch (loop_align_strategy) {
    case Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignStart:
        return LoopAlignStrategy::AlignStart;
    case Serialize::LoopAlignStrategy::LoopAlignStrategy_AlignEnd:
        return LoopAlignStrategy::AlignEnd;
    case Serialize::LoopAlignStrategy::LoopAlignStrategy_NoAlign:
        return LoopAlignStrategy::NoAlign;
    case Serialize::LoopAlignStrategy::LoopAlignStrategy_Auto:
        return LoopAlignStrategy::Auto;
    default:
        user_assert(false) << "unknown loop align strategy " << loop_align_strategy << "\n";
        return LoopAlignStrategy::AlignStart;
    }
}

ExternFuncArgument::ArgType Deserializer::deserialize_extern_func_argument_type(const Serialize::ExternFuncArgumentType extern_func_argument_type) {
    switch (extern_func_argument_type) {
    case Serialize::ExternFuncArgumentType::ExternFuncArgumentType_UndefinedArg:
        return ExternFuncArgument::ArgType::UndefinedArg;
    case Serialize::ExternFuncArgumentType::ExternFuncArgumentType_FuncArg:
        return ExternFuncArgument::ArgType::FuncArg;
    case Serialize::ExternFuncArgumentType::ExternFuncArgumentType_BufferArg:
        return ExternFuncArgument::ArgType::BufferArg;
    case Serialize::ExternFuncArgumentType::ExternFuncArgumentType_ExprArg:
        return ExternFuncArgument::ArgType::ExprArg;
    case Serialize::ExternFuncArgumentType::ExternFuncArgumentType_ImageParamArg:
        return ExternFuncArgument::ArgType::ImageParamArg;
    default:
        user_assert(false) << "unknown extern func argument type " << extern_func_argument_type << "\n";
        return ExternFuncArgument::ArgType::UndefinedArg;
    }
}

Type Deserializer::deserialize_type(const Serialize::Type *type) {
    user_assert(type != nullptr) << "deserializing a null Type\n";
    using Serialize::TypeCode;
    int bits = type->bits();
    int lanes = type->lanes();
    TypeCode code_deserialized = type->code();
    halide_type_code_t code = halide_type_uint;
    switch (code_deserialized) {
    case TypeCode::TypeCode_Int:
        code = halide_type_int;
        break;
    case TypeCode::TypeCode_UInt:
        code = halide_type_uint;
        break;
    case TypeCode::TypeCode_Float:
        code = halide_type_float;
        break;
    case TypeCode::TypeCode_Handle:
        code = halide_type_handle;
        break;
    case TypeCode::TypeCode_BFloat:
        code = halide_type_bfloat;
        break;
    }

    return Type(code, bits, lanes);
}

void Deserializer::deserialize_function(const Serialize::Func *function, Function &hl_function) {
    user_assert(function != nullptr) << "deserializing a null Function\n";
    std::string name = deserialize_string(function->name());
    std::string origin_name = deserialize_string(function->origin_name());
    std::vector<Type> output_types;
    output_types.reserve(function->output_types()->size());
    for (const auto &type : *function->output_types()) {
        output_types.push_back(deserialize_type(type));
    }
    std::vector<Type> required_types;
    required_types.reserve(function->required_types()->size());
    for (const auto &type : *function->required_types()) {
        required_types.push_back(deserialize_type(type));
    }
    int required_dim = function->required_dims();
    std::vector<std::string> args;
    args.reserve(function->args()->size());
    for (const auto &arg : *function->args()) {
        args.push_back(deserialize_string(arg));
    }

    auto func_schedule = deserialize_func_schedule(function->func_schedule());

    auto init_def = deserialize_definition(function->init_def());

    std::vector<Definition> updates;
    for (const auto &update : *function->updates()) {
        updates.push_back(deserialize_definition(update));
    }
    std::string debug_file = deserialize_string(function->debug_file());
    std::vector<Parameter> output_buffers;
    output_buffers.reserve(function->output_buffers_names()->size());
    for (const auto &output_buffer_name_serialized : *function->output_buffers_names()) {
        auto output_buffer_name = deserialize_string(output_buffer_name_serialized);
        Parameter output_buffer;
        if (auto it = non_serialized_parameters.find(output_buffer_name); it != non_serialized_parameters.end()) {
            output_buffer = it->second;
        } else if (auto it = parameters_in_pipeline.find(output_buffer_name); it != parameters_in_pipeline.end()) {
            output_buffer = it->second;
        }
        output_buffers.push_back(output_buffer);
    }
    std::vector<ExternFuncArgument> extern_arguments;
    extern_arguments.reserve(function->extern_arguments()->size());
    for (const auto &extern_argument : *function->extern_arguments()) {
        extern_arguments.push_back(deserialize_extern_func_argument(extern_argument));
    }

    std::string extern_function_name = deserialize_string(function->extern_function_name());
    auto name_mangling = deserialize_name_mangling(function->extern_mangling());
    auto extern_function_device_api = deserialize_device_api(function->extern_function_device_api());
    auto extern_proxy_expr = deserialize_expr(function->extern_proxy_expr_type(), function->extern_proxy_expr());
    bool trace_loads = function->trace_loads(), trace_stores = function->trace_stores(), trace_realizations = function->trace_realizations();
    std::vector<std::string> trace_tags;
    trace_tags.reserve(function->trace_tags()->size());
    for (const auto &tag : *function->trace_tags()) {
        trace_tags.push_back(deserialize_string(tag));
    }
    bool frozen = function->frozen();

    hl_function.update_with_deserialization(name, origin_name, output_types, required_types,
                                            required_dim, args, func_schedule, init_def, updates,
                                            debug_file, output_buffers, extern_arguments, extern_function_name,
                                            name_mangling, extern_function_device_api, extern_proxy_expr,
                                            trace_loads, trace_stores, trace_realizations, trace_tags, frozen);
}

Stmt Deserializer::deserialize_stmt(Serialize::Stmt type_code, const void *stmt) {
    user_assert(stmt != nullptr) << "deserializing a null Stmt\n";
    switch (type_code) {
    case Serialize::Stmt_LetStmt: {
        const Serialize::LetStmt *let_stmt = (const Serialize::LetStmt *)stmt;
        auto name = deserialize_string(let_stmt->name());
        auto value = deserialize_expr(let_stmt->value_type(), let_stmt->value());
        auto body = deserialize_stmt(let_stmt->body_type(), let_stmt->body());
        return LetStmt::make(name, value, body);
    }
    case Serialize::Stmt_AssertStmt: {
        const Serialize::AssertStmt *assert_stmt = (const Serialize::AssertStmt *)stmt;
        auto condition = deserialize_expr(assert_stmt->condition_type(), assert_stmt->condition());
        auto message = deserialize_expr(assert_stmt->message_type(), assert_stmt->message());
        return AssertStmt::make(condition, message);
    }
    case Serialize::Stmt_ProducerConsumer: {
        const Serialize::ProducerConsumer *producer_consumer = (const Serialize::ProducerConsumer *)stmt;
        auto name = deserialize_string(producer_consumer->name());
        auto is_producer = producer_consumer->is_producer();
        auto body = deserialize_stmt(producer_consumer->body_type(), producer_consumer->body());
        return ProducerConsumer::make(name, is_producer, body);
    }
    case Serialize::Stmt_For: {
        const Serialize::For *for_stmt = (const Serialize::For *)stmt;
        auto name = deserialize_string(for_stmt->name());
        auto min = deserialize_expr(for_stmt->min_type(), for_stmt->min());
        auto extent = deserialize_expr(for_stmt->extent_type(), for_stmt->extent());
        ForType for_type = deserialize_for_type(for_stmt->for_type());

        DeviceAPI device_api = deserialize_device_api(for_stmt->device_api());
        auto body = deserialize_stmt(for_stmt->body_type(), for_stmt->body());
        return For::make(name, min, extent, for_type, device_api, body);
    }
    case Serialize::Stmt_Store: {
        const Serialize::Store *store_stmt = (const Serialize::Store *)stmt;
        auto name = deserialize_string(store_stmt->name());
        auto predicate = deserialize_expr(store_stmt->predicate_type(), store_stmt->predicate());
        auto value = deserialize_expr(store_stmt->value_type(), store_stmt->value());
        auto index = deserialize_expr(store_stmt->index_type(), store_stmt->index());
        auto param_name = deserialize_string(store_stmt->param_name());
        Parameter param;
        if (auto it = non_serialized_parameters.find(param_name); it != non_serialized_parameters.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        }
        auto alignment = deserialize_modulus_remainder(store_stmt->alignment());
        return Store::make(name, value, index, param, predicate, alignment);
    }
    case Serialize::Stmt_Provide: {
        const Serialize::Provide *provide_stmt = (const Serialize::Provide *)stmt;
        auto name = deserialize_string(provide_stmt->name());
        std::vector<Expr> values = deserialize_expr_vector(provide_stmt->values_type(), provide_stmt->values());
        std::vector<Expr> args = deserialize_expr_vector(provide_stmt->args_type(), provide_stmt->args());
        auto predicate = deserialize_expr(provide_stmt->predicate_type(), provide_stmt->predicate());
        return Provide::make(name, values, args, predicate);
    }
    case Serialize::Stmt_Allocate: {
        const Serialize::Allocate *allocate_stmt = (const Serialize::Allocate *)stmt;
        auto name = deserialize_string(allocate_stmt->name());
        auto type = deserialize_type(allocate_stmt->type());
        MemoryType memory_type = deserialize_memory_type(allocate_stmt->memory_type());
        std::vector<Expr> extents = deserialize_expr_vector(allocate_stmt->extents_type(), allocate_stmt->extents());
        auto condition = deserialize_expr(allocate_stmt->condition_type(), allocate_stmt->condition());
        auto new_expr = deserialize_expr(allocate_stmt->new_expr_type(), allocate_stmt->new_expr());
        auto free_function = deserialize_string(allocate_stmt->free_function());
        auto padding = allocate_stmt->padding();
        auto body = deserialize_stmt(allocate_stmt->body_type(), allocate_stmt->body());
        return Allocate::make(name, type, memory_type, extents, condition, body, new_expr, free_function, padding);
    }
    case Serialize::Stmt_Free: {
        const Serialize::Free *free_stmt = (const Serialize::Free *)stmt;
        auto name = deserialize_string(free_stmt->name());
        return Free::make(name);
    }
    case Serialize::Stmt_Realize: {
        const Serialize::Realize *realize_stmt = (const Serialize::Realize *)stmt;
        auto name = deserialize_string(realize_stmt->name());
        std::vector<Type> types;
        types.reserve(realize_stmt->types()->size());
        for (const auto &type : *realize_stmt->types()) {
            types.push_back(deserialize_type(type));
        }
        MemoryType memory_type = deserialize_memory_type(realize_stmt->memory_type());
        std::vector<Range> bounds;
        bounds.reserve(realize_stmt->bounds()->size());
        for (const auto &bound : *realize_stmt->bounds()) {
            bounds.push_back(deserialize_range(bound));
        }
        auto condition = deserialize_expr(realize_stmt->condition_type(), realize_stmt->condition());
        auto body = deserialize_stmt(realize_stmt->body_type(), realize_stmt->body());
        return Realize::make(name, types, memory_type, bounds, condition, body);
    }
    case Serialize::Stmt_Block: {
        const Serialize::Block *block_stmt = (const Serialize::Block *)stmt;
        auto first = deserialize_stmt(block_stmt->first_type(), block_stmt->first());
        auto rest = deserialize_stmt(block_stmt->rest_type(), block_stmt->rest());
        return Block::make(first, rest);
    }
    case Serialize::Stmt_IfThenElse: {
        const Serialize::IfThenElse *if_then_else_stmt = (const Serialize::IfThenElse *)stmt;
        auto condition = deserialize_expr(if_then_else_stmt->condition_type(), if_then_else_stmt->condition());
        auto then_case = deserialize_stmt(if_then_else_stmt->then_case_type(), if_then_else_stmt->then_case());
        auto else_case = deserialize_stmt(if_then_else_stmt->else_case_type(), if_then_else_stmt->else_case());
        return IfThenElse::make(condition, then_case, else_case);
    }
    case Serialize::Stmt_Evaluate: {
        const Serialize::Evaluate *evaluate_stmt = (const Serialize::Evaluate *)stmt;
        auto value = deserialize_expr(evaluate_stmt->value_type(), evaluate_stmt->value());
        return Evaluate::make(value);
    }
    case Serialize::Stmt_Prefetch: {
        const Serialize::Prefetch *prefetch_stmt = (const Serialize::Prefetch *)stmt;
        auto name = deserialize_string(prefetch_stmt->name());
        std::vector<Type> types;
        types.reserve(prefetch_stmt->types()->size());
        for (const auto &type : *prefetch_stmt->types()) {
            types.push_back(deserialize_type(type));
        }
        std::vector<Range> bounds;
        bounds.reserve(prefetch_stmt->bounds()->size());
        for (const auto &bound : *prefetch_stmt->bounds()) {
            bounds.push_back(deserialize_range(bound));
        }
        auto prefetch = deserialize_prefetch_directive(prefetch_stmt->prefetch());
        auto condition = deserialize_expr(prefetch_stmt->condition_type(), prefetch_stmt->condition());
        auto body = deserialize_stmt(prefetch_stmt->body_type(), prefetch_stmt->body());
        return Prefetch::make(name, types, bounds, prefetch, condition, body);
    }
    case Serialize::Stmt_Acquire: {
        const Serialize::Acquire *acquire_stmt = (const Serialize::Acquire *)stmt;
        auto semaphore = deserialize_expr(acquire_stmt->semaphore_type(), acquire_stmt->semaphore());
        auto count = deserialize_expr(acquire_stmt->count_type(), acquire_stmt->count());
        auto body = deserialize_stmt(acquire_stmt->body_type(), acquire_stmt->body());
        return Acquire::make(semaphore, count, body);
    }
    case Serialize::Stmt_Fork: {
        const Serialize::Fork *fork_stmt = (const Serialize::Fork *)stmt;
        auto first = deserialize_stmt(fork_stmt->first_type(), fork_stmt->first());
        auto rest = deserialize_stmt(fork_stmt->rest_type(), fork_stmt->rest());
        return Fork::make(first, rest);
    }
    case Serialize::Stmt_Atomic: {
        const Serialize::Atomic *atomic_stmt = (const Serialize::Atomic *)stmt;
        auto producer_name = deserialize_string(atomic_stmt->producer_name());
        auto mutex_name = deserialize_string(atomic_stmt->mutex_name());
        auto body = deserialize_stmt(atomic_stmt->body_type(), atomic_stmt->body());
        return Atomic::make(producer_name, mutex_name, body);
    }
    case Serialize::Stmt_UndefinedStmt: {
        return Stmt();
    }
    default:
        user_assert(false) << "unknown type code " << type_code << "\n";
        return Stmt();
    }
}

Expr Deserializer::deserialize_expr(Serialize::Expr type_code, const void *expr) {
    assert(expr != nullptr);
    switch (type_code) {
    case Serialize::Expr::Expr_IntImm: {
        const Serialize::IntImm *int_imm_expr = (const Serialize::IntImm *)expr;
        auto value = int_imm_expr->value();
        auto type = deserialize_type(int_imm_expr->type());
        return IntImm::make(type, value);
    }
    case Serialize::Expr::Expr_UIntImm: {
        const Serialize::UIntImm *uint_imm_expr = (const Serialize::UIntImm *)expr;
        auto value = uint_imm_expr->value();
        auto type = deserialize_type(uint_imm_expr->type());
        return UIntImm::make(type, value);
    }
    case Serialize::Expr::Expr_FloatImm: {
        const Serialize::FloatImm *float_imm_expr = (const Serialize::FloatImm *)expr;
        auto value = float_imm_expr->value();
        auto type = deserialize_type(float_imm_expr->type());
        return FloatImm::make(type, value);
    }
    case Serialize::Expr::Expr_StringImm: {
        const Serialize::StringImm *string_imm_expr = (const Serialize::StringImm *)expr;
        auto value = deserialize_string(string_imm_expr->value());
        return StringImm::make(value);
    }
    case Serialize::Expr::Expr_Cast: {
        const Serialize::Cast *cast_expr = (const Serialize::Cast *)expr;
        auto value = deserialize_expr(cast_expr->value_type(), cast_expr->value());
        auto type = deserialize_type(cast_expr->type());
        return Cast::make(type, value);
    }
    case Serialize::Expr::Expr_Reinterpret: {
        const Serialize::Reinterpret *reinterpret_expr = (const Serialize::Reinterpret *)expr;
        auto value = deserialize_expr(reinterpret_expr->value_type(), reinterpret_expr->value());
        auto type = deserialize_type(reinterpret_expr->type());
        return Reinterpret::make(type, value);
    }
    case Serialize::Expr::Expr_Add: {
        const Serialize::Add *add_expr = (const Serialize::Add *)expr;
        auto a = deserialize_expr(add_expr->a_type(), add_expr->a());
        auto b = deserialize_expr(add_expr->b_type(), add_expr->b());
        return Add::make(a, b);
    }
    case Serialize::Expr::Expr_Sub: {
        const Serialize::Sub *sub_expr = (const Serialize::Sub *)expr;
        auto a = deserialize_expr(sub_expr->a_type(), sub_expr->a());
        auto b = deserialize_expr(sub_expr->b_type(), sub_expr->b());
        return Sub::make(a, b);
    }
    case Serialize::Expr::Expr_Mul: {
        const Serialize::Mul *mul_expr = (const Serialize::Mul *)expr;
        auto a = deserialize_expr(mul_expr->a_type(), mul_expr->a());
        auto b = deserialize_expr(mul_expr->b_type(), mul_expr->b());
        return Mul::make(a, b);
    }
    case Serialize::Expr::Expr_Div: {
        const Serialize::Div *div_expr = (const Serialize::Div *)expr;
        auto a = deserialize_expr(div_expr->a_type(), div_expr->a());
        auto b = deserialize_expr(div_expr->b_type(), div_expr->b());
        return Div::make(a, b);
    }
    case Serialize::Expr::Expr_Mod: {
        const Serialize::Mod *mod_expr = (const Serialize::Mod *)expr;
        auto a = deserialize_expr(mod_expr->a_type(), mod_expr->a());
        auto b = deserialize_expr(mod_expr->b_type(), mod_expr->b());
        return Mod::make(a, b);
    }
    case Serialize::Expr::Expr_Min: {
        const Serialize::Min *min_expr = (const Serialize::Min *)expr;
        auto a = deserialize_expr(min_expr->a_type(), min_expr->a());
        auto b = deserialize_expr(min_expr->b_type(), min_expr->b());
        return Min::make(a, b);
    }
    case Serialize::Expr::Expr_Max: {
        const Serialize::Max *max_expr = (const Serialize::Max *)expr;
        auto a = deserialize_expr(max_expr->a_type(), max_expr->a());
        auto b = deserialize_expr(max_expr->b_type(), max_expr->b());
        return Max::make(a, b);
    }
    case Serialize::Expr::Expr_EQ: {
        const Serialize::EQ *eq_expr = (const Serialize::EQ *)expr;
        auto a = deserialize_expr(eq_expr->a_type(), eq_expr->a());
        auto b = deserialize_expr(eq_expr->b_type(), eq_expr->b());
        return EQ::make(a, b);
    }
    case Serialize::Expr::Expr_NE: {
        const Serialize::NE *ne_expr = (const Serialize::NE *)expr;
        auto a = deserialize_expr(ne_expr->a_type(), ne_expr->a());
        auto b = deserialize_expr(ne_expr->b_type(), ne_expr->b());
        return NE::make(a, b);
    }
    case Serialize::Expr::Expr_LT: {
        const Serialize::LT *lt_expr = (const Serialize::LT *)expr;
        auto a = deserialize_expr(lt_expr->a_type(), lt_expr->a());
        auto b = deserialize_expr(lt_expr->b_type(), lt_expr->b());
        return LT::make(a, b);
    }
    case Serialize::Expr::Expr_LE: {
        const Serialize::LE *le_expr = (const Serialize::LE *)expr;
        auto a = deserialize_expr(le_expr->a_type(), le_expr->a());
        auto b = deserialize_expr(le_expr->b_type(), le_expr->b());
        return LE::make(a, b);
    }
    case Serialize::Expr::Expr_GT: {
        const Serialize::GT *gt_expr = (const Serialize::GT *)expr;
        auto a = deserialize_expr(gt_expr->a_type(), gt_expr->a());
        auto b = deserialize_expr(gt_expr->b_type(), gt_expr->b());
        return GT::make(a, b);
    }
    case Serialize::Expr::Expr_GE: {
        const Serialize::GE *ge_expr = (const Serialize::GE *)expr;
        auto a = deserialize_expr(ge_expr->a_type(), ge_expr->a());
        auto b = deserialize_expr(ge_expr->b_type(), ge_expr->b());
        return GE::make(a, b);
    }
    case Serialize::Expr::Expr_And: {
        const Serialize::And *and_expr = (const Serialize::And *)expr;
        auto a = deserialize_expr(and_expr->a_type(), and_expr->a());
        auto b = deserialize_expr(and_expr->b_type(), and_expr->b());
        return And::make(a, b);
    }
    case Serialize::Expr::Expr_Or: {
        const Serialize::Or *or_expr = (const Serialize::Or *)expr;
        auto a = deserialize_expr(or_expr->a_type(), or_expr->a());
        auto b = deserialize_expr(or_expr->b_type(), or_expr->b());
        return Or::make(a, b);
    }
    case Serialize::Expr::Expr_Not: {
        const Serialize::Not *not_expr = (const Serialize::Not *)expr;
        auto a = deserialize_expr(not_expr->a_type(), not_expr->a());
        return Not::make(a);
    }
    case Serialize::Expr::Expr_Select: {
        const Serialize::Select *select_expr = (const Serialize::Select *)expr;
        auto condition = deserialize_expr(select_expr->condition_type(), select_expr->condition());
        auto true_value = deserialize_expr(select_expr->true_value_type(), select_expr->true_value());
        auto false_value = deserialize_expr(select_expr->false_value_type(), select_expr->false_value());
        return Select::make(condition, true_value, false_value);
    }
    case Serialize::Expr::Expr_Load: {
        const Serialize::Load *load_expr = (const Serialize::Load *)expr;
        auto name = deserialize_string(load_expr->name());
        auto predicate = deserialize_expr(load_expr->predicate_type(), load_expr->predicate());
        auto index = deserialize_expr(load_expr->index_type(), load_expr->index());
        Buffer<> image;
        auto image_name = deserialize_string(load_expr->image_name());
        if (auto it = buffers_in_pipeline.find(image_name); it != buffers_in_pipeline.end()) {
            image = it->second;
        }
        auto param_name = deserialize_string(load_expr->param_name());
        Parameter param;
        if (auto it = non_serialized_parameters.find(param_name); it != non_serialized_parameters.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        }
        auto alignment = deserialize_modulus_remainder(load_expr->alignment());
        auto type = deserialize_type(load_expr->type());
        return Load::make(type, name, index, image, param, predicate, alignment);
    }
    case Serialize::Expr::Expr_Ramp: {
        const Serialize::Ramp *ramp_expr = (const Serialize::Ramp *)expr;
        auto base = deserialize_expr(ramp_expr->base_type(), ramp_expr->base());
        auto stride = deserialize_expr(ramp_expr->stride_type(), ramp_expr->stride());
        auto lanes = ramp_expr->lanes();
        return Ramp::make(base, stride, lanes);
    }
    case Serialize::Expr::Expr_Broadcast: {
        const Serialize::Broadcast *broadcast_expr = (const Serialize::Broadcast *)expr;
        auto value = deserialize_expr(broadcast_expr->value_type(), broadcast_expr->value());
        auto lanes = broadcast_expr->lanes();
        return Broadcast::make(value, lanes);
    }
    case Serialize::Expr::Expr_Let: {
        const Serialize::Let *let_expr = (const Serialize::Let *)expr;
        auto name = deserialize_string(let_expr->name());
        auto value = deserialize_expr(let_expr->value_type(), let_expr->value());
        auto body = deserialize_expr(let_expr->body_type(), let_expr->body());
        return Let::make(name, value, body);
    }
    case Serialize::Expr::Expr_Call: {
        const Serialize::Call *call_expr = (const Serialize::Call *)expr;
        auto name = deserialize_string(call_expr->name());
        std::vector<Expr> args = deserialize_expr_vector(call_expr->args_type(), call_expr->args());
        auto value_index = call_expr->value_index();
        int func_index = call_expr->func_index();
        FunctionPtr func_ptr;
        if (auto it = this->reverse_function_mappings.find(func_index); it != this->reverse_function_mappings.end() && func_index != -1) {
            FunctionPtr called_func_ptr = it->second;
            func_ptr.weak = called_func_ptr.group();
            func_ptr.idx = called_func_ptr.idx;
        }
        auto call_type = deserialize_call_type(call_expr->call_type());
        Buffer<> image;
        auto image_name = deserialize_string(call_expr->image_name());
        if (auto it = buffers_in_pipeline.find(image_name); it != buffers_in_pipeline.end()) {
            image = it->second;
        }
        auto param_name = deserialize_string(call_expr->param_name());
        Parameter param;
        if (auto it = non_serialized_parameters.find(param_name); it != non_serialized_parameters.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        }
        auto type = deserialize_type(call_expr->type());
        return Call::make(type, name, args, call_type, func_ptr, value_index, image, param);
    }
    case Serialize::Expr::Expr_Variable: {
        const Serialize::Variable *variable_expr = (const Serialize::Variable *)expr;
        auto name = deserialize_string(variable_expr->name());
        auto type = deserialize_type(variable_expr->type());
        auto param_name = deserialize_string(variable_expr->param_name());
        Parameter param;
        if (auto it = non_serialized_parameters.find(param_name); it != non_serialized_parameters.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        }
        Buffer<> image;
        auto image_name = deserialize_string(variable_expr->image_name());
        if (auto it = buffers_in_pipeline.find(image_name); it != buffers_in_pipeline.end()) {
            image = it->second;
        }
        auto reduction_domain = deserialize_reduction_domain(variable_expr->reduction_domain());
        return Variable::make(type, name, image, param, reduction_domain);
    }
    case Serialize::Expr::Expr_Shuffle: {
        const Serialize::Shuffle *shuffle_expr = (const Serialize::Shuffle *)expr;
        std::vector<Expr> vectors = deserialize_expr_vector(shuffle_expr->vectors_type(), shuffle_expr->vectors());
        auto indices_serialized = shuffle_expr->indices();
        std::vector<int32_t> indices;
        indices.reserve(indices_serialized->size());
        for (size_t i = 0; i < indices_serialized->size(); ++i) {
            indices.push_back(indices_serialized->Get(i));
        }
        return Shuffle::make(vectors, indices);
    }
    case Serialize::Expr::Expr_VectorReduce: {
        const Serialize::VectorReduce *vector_reduce_expr = (const Serialize::VectorReduce *)expr;
        auto value = deserialize_expr(vector_reduce_expr->value_type(), vector_reduce_expr->value());
        auto reduction_op = deserialize_vector_reduce_op(vector_reduce_expr->reduction_op());
        int32_t lanes = vector_reduce_expr->lanes();
        return VectorReduce::make(reduction_op, value, lanes);
    }
    case Serialize::Expr::Expr_UndefinedExpr: {
        return Expr();
    }
    default: {
        user_assert(false) << "unknown type code " << type_code << "\n";
        return Expr();
    }
    }
}

std::vector<Expr> Deserializer::deserialize_expr_vector(const flatbuffers::Vector<uint8_t> *exprs_types, const flatbuffers::Vector<flatbuffers::Offset<void>> *exprs_serialized) {
    assert(exprs_types != nullptr);
    assert(exprs_serialized != nullptr);
    std::vector<Expr> result;
    result.reserve(exprs_serialized->size());
    for (size_t i = 0; i < exprs_serialized->size(); ++i) {
        auto expr = deserialize_expr(static_cast<Serialize::Expr>(exprs_types->Get(i)), exprs_serialized->Get(i));
        result.push_back(expr);
    }
    return result;
}

Range Deserializer::deserialize_range(const Serialize::Range *range) {
    assert(range != nullptr);
    auto min = deserialize_expr(range->min_type(), range->min());
    auto extent = deserialize_expr(range->extent_type(), range->extent());
    return Range(min, extent);
}

Bound Deserializer::deserialize_bound(const Serialize::Bound *bound) {
    assert(bound != nullptr);
    auto var = deserialize_string(bound->var());
    auto min = deserialize_expr(bound->min_type(), bound->min());
    auto extent = deserialize_expr(bound->extent_type(), bound->extent());
    auto modulus = deserialize_expr(bound->modulus_type(), bound->modulus());
    auto remainder = deserialize_expr(bound->remainder_type(), bound->remainder());
    auto hl_bound = Bound();
    hl_bound.var = var;
    hl_bound.min = min;
    hl_bound.extent = extent;
    hl_bound.modulus = modulus;
    hl_bound.remainder = remainder;
    return hl_bound;
}

StorageDim Deserializer::deserialize_storage_dim(const Serialize::StorageDim *storage_dim) {
    assert(storage_dim != nullptr);
    auto var = deserialize_string(storage_dim->var());
    auto alignment = deserialize_expr(storage_dim->alignment_type(), storage_dim->alignment());
    auto bound = deserialize_expr(storage_dim->bound_type(), storage_dim->bound());
    auto fold_factor = deserialize_expr(storage_dim->fold_factor_type(), storage_dim->fold_factor());
    auto fold_forward = storage_dim->fold_forward();
    auto hl_storage_dim = StorageDim();
    hl_storage_dim.var = var;
    hl_storage_dim.alignment = alignment;
    hl_storage_dim.bound = bound;
    hl_storage_dim.fold_factor = fold_factor;
    hl_storage_dim.fold_forward = fold_forward;
    return hl_storage_dim;
}

LoopLevel Deserializer::deserialize_loop_level(const Serialize::LoopLevel *loop_level) {
    assert(loop_level != nullptr);
    auto func_name = deserialize_string(loop_level->func_name());
    auto stage_index = loop_level->stage_index();
    auto var_name = deserialize_string(loop_level->var_name());
    auto is_rvar = loop_level->is_rvar();
    auto locked = loop_level->locked();
    return LoopLevel(func_name, var_name, is_rvar, stage_index, locked);
}

FuncSchedule Deserializer::deserialize_func_schedule(const Serialize::FuncSchedule *func_schedule) {
    assert(func_schedule != nullptr);
    auto store_level = deserialize_loop_level(func_schedule->store_level());
    auto compute_level = deserialize_loop_level(func_schedule->compute_level());
    std::vector<StorageDim> storage_dims;
    for (const auto &storage_dim : *func_schedule->storage_dims()) {
        storage_dims.push_back(deserialize_storage_dim(storage_dim));
    }
    std::vector<Bound> bounds;
    for (const auto &bound : *func_schedule->bounds()) {
        bounds.push_back(deserialize_bound(bound));
    }
    std::vector<Bound> estimates;
    for (const auto &estimate : *func_schedule->estimates()) {
        estimates.push_back(deserialize_bound(estimate));
    }
    std::map<std::string, FunctionPtr> wrappers = deserialize_wrapper_refs(func_schedule->wrappers());
    auto memory_type = deserialize_memory_type(func_schedule->memory_type());
    auto memoized = func_schedule->memoized();
    auto async = func_schedule->async();
    auto memoize_eviction_key = deserialize_expr(func_schedule->memoize_eviction_key_type(), func_schedule->memoize_eviction_key());
    auto hl_func_schedule = FuncSchedule();
    hl_func_schedule.store_level() = store_level;
    hl_func_schedule.compute_level() = compute_level;
    hl_func_schedule.storage_dims() = storage_dims;
    hl_func_schedule.bounds() = bounds;
    hl_func_schedule.estimates() = estimates;
    hl_func_schedule.wrappers() = wrappers;
    hl_func_schedule.memory_type() = memory_type;
    hl_func_schedule.memoized() = memoized;
    hl_func_schedule.async() = async;
    hl_func_schedule.memoize_eviction_key() = memoize_eviction_key;
    return hl_func_schedule;
}

Specialization Deserializer::deserialize_specialization(const Serialize::Specialization *specialization) {
    assert(specialization != nullptr);
    auto condition = deserialize_expr(specialization->condition_type(), specialization->condition());
    auto defintion = deserialize_definition(specialization->definition());
    auto failure_message = deserialize_string(specialization->failure_message());
    Specialization hl_specialization;
    hl_specialization.condition = condition;
    hl_specialization.definition = defintion;
    hl_specialization.failure_message = failure_message;
    return hl_specialization;
}

Definition Deserializer::deserialize_definition(const Serialize::Definition *definition) {
    assert(definition != nullptr);
    auto is_init = definition->is_init();
    auto predicate = deserialize_expr(definition->predicate_type(), definition->predicate());
    auto args = deserialize_expr_vector(definition->args_type(), definition->args());
    auto values = deserialize_expr_vector(definition->values_type(), definition->values());
    auto stage_schedule = deserialize_stage_schedule(definition->stage_schedule());
    std::vector<Specialization> specializations;
    for (const auto &specialization : *definition->specializations()) {
        specializations.push_back(deserialize_specialization(specialization));
    }
    auto source_location = deserialize_string(definition->source_location());
    return Definition(is_init, predicate, args, values, stage_schedule, specializations, source_location);
}

ReductionVariable Deserializer::deserialize_reduction_variable(const Serialize::ReductionVariable *reduction_variable) {
    assert(reduction_variable != nullptr);
    auto var = deserialize_string(reduction_variable->var());
    auto min = deserialize_expr(reduction_variable->min_type(), reduction_variable->min());
    auto extent = deserialize_expr(reduction_variable->extent_type(), reduction_variable->extent());
    auto hl_reduction_variable = ReductionVariable();
    hl_reduction_variable.var = var;
    hl_reduction_variable.min = min;
    hl_reduction_variable.extent = extent;
    return hl_reduction_variable;
}

ReductionDomain Deserializer::deserialize_reduction_domain(const Serialize::ReductionDomain *reduction_domain) {
    assert(reduction_domain != nullptr);
    bool defined = reduction_domain->defined();
    if (!defined) {
        return ReductionDomain();
    }
    std::vector<ReductionVariable> domain;
    for (const auto &reduction_variable : *reduction_domain->domain()) {
        domain.push_back(deserialize_reduction_variable(reduction_variable));
    }
    auto predicate = deserialize_expr(reduction_domain->predicate_type(), reduction_domain->predicate());
    auto frozen = reduction_domain->frozen();
    return ReductionDomain(domain, predicate, frozen);
}

ModulusRemainder Deserializer::deserialize_modulus_remainder(const Serialize::ModulusRemainder *modulus_remainder) {
    assert(modulus_remainder != nullptr);
    return ModulusRemainder(modulus_remainder->modulus(), modulus_remainder->remainder());
}

PrefetchDirective Deserializer::deserialize_prefetch_directive(const Serialize::PrefetchDirective *prefetch_directive) {
    assert(prefetch_directive != nullptr);
    auto name = deserialize_string(prefetch_directive->name());
    auto at = deserialize_string(prefetch_directive->at());
    auto from = deserialize_string(prefetch_directive->from());
    auto offset = deserialize_expr(prefetch_directive->offset_type(), prefetch_directive->offset());
    auto strategy = deserialize_prefetch_bound_strategy(prefetch_directive->strategy());
    auto param_name = deserialize_string(prefetch_directive->param_name());
    Parameter param;
    if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
        param = it->second;
    }
    auto hl_prefetch_directive = PrefetchDirective();
    hl_prefetch_directive.name = name;
    hl_prefetch_directive.at = at;
    hl_prefetch_directive.from = from;
    hl_prefetch_directive.offset = offset;
    hl_prefetch_directive.strategy = strategy;
    hl_prefetch_directive.param = param;
    return hl_prefetch_directive;
}

Split Deserializer::deserialize_split(const Serialize::Split *split) {
    assert(split != nullptr);
    auto old_var = deserialize_string(split->old_var());
    auto outer = deserialize_string(split->outer());
    auto inner = deserialize_string(split->inner());
    auto factor = deserialize_expr(split->factor_type(), split->factor());
    auto tail = deserialize_tail_strategy(split->tail());
    auto split_type = deserialize_split_type(split->split_type());
    auto hl_split = Split();
    hl_split.old_var = old_var;
    hl_split.outer = outer;
    hl_split.inner = inner;
    hl_split.factor = factor;
    hl_split.tail = tail;
    hl_split.split_type = split_type;
    return hl_split;
}

Dim Deserializer::deserialize_dim(const Serialize::Dim *dim) {
    assert(dim != nullptr);
    auto var = deserialize_string(dim->var());
    auto for_type = deserialize_for_type(dim->for_type());
    auto device_api = deserialize_device_api(dim->device_api());
    auto dim_type = deserialize_dim_type(dim->dim_type());
    auto hl_dim = Dim();
    hl_dim.var = var;
    hl_dim.for_type = for_type;
    hl_dim.device_api = device_api;
    hl_dim.dim_type = dim_type;
    return hl_dim;
}

FuseLoopLevel Deserializer::deserialize_fuse_loop_level(const Serialize::FuseLoopLevel *fuse_loop_level) {
    assert(fuse_loop_level != nullptr);
    auto fuse_level = deserialize_loop_level(fuse_loop_level->fuse_level());
    std::vector<std::string> align_dimension_names;
    std::vector<LoopAlignStrategy> align_strategies;
    std::map<std::string, LoopAlignStrategy> align;
    for (const auto &align_dimension_name : *fuse_loop_level->align_dimension_names()) {
        align_dimension_names.push_back(deserialize_string(align_dimension_name));
    }
    for (const auto &align_strategy : *fuse_loop_level->align_strategies()) {
        align_strategies.push_back(deserialize_loop_align_strategy((Serialize::LoopAlignStrategy)align_strategy));
    }
    for (size_t i = 0; i < align_dimension_names.size(); ++i) {
        align[align_dimension_names[i]] = align_strategies[i];
    }
    return FuseLoopLevel(fuse_level, align);
}

FusedPair Deserializer::deserialize_fused_pair(const Serialize::FusedPair *fused_pair) {
    assert(fused_pair != nullptr);
    auto func_1 = deserialize_string(fused_pair->func_1());
    auto func_2 = deserialize_string(fused_pair->func_2());
    auto var_name = deserialize_string(fused_pair->var_name());
    return FusedPair(func_1, fused_pair->stage_1(), func_2, fused_pair->stage_2(), var_name);
}

StageSchedule Deserializer::deserialize_stage_schedule(const Serialize::StageSchedule *stage_schedule) {
    assert(stage_schedule != nullptr);
    std::vector<ReductionVariable> rvars;
    rvars.reserve(stage_schedule->rvars()->size());
    for (const auto &rvar : *stage_schedule->rvars()) {
        rvars.push_back(deserialize_reduction_variable(rvar));
    }
    std::vector<Split> splits;
    splits.reserve(stage_schedule->splits()->size());
    for (const auto &split : *stage_schedule->splits()) {
        splits.push_back(deserialize_split(split));
    }
    std::vector<Dim> dims;
    dims.reserve(stage_schedule->dims()->size());
    for (const auto &dim : *stage_schedule->dims()) {
        dims.push_back(deserialize_dim(dim));
    }
    std::vector<PrefetchDirective> prefetches;
    prefetches.reserve(stage_schedule->prefetches()->size());
    for (const auto &prefetch : *stage_schedule->prefetches()) {
        prefetches.push_back(deserialize_prefetch_directive(prefetch));
    }
    FuseLoopLevel fuse_level = deserialize_fuse_loop_level(stage_schedule->fuse_level());
    std::vector<FusedPair> fused_pairs;
    fused_pairs.reserve(stage_schedule->fused_pairs()->size());
    for (const auto &fused_pair : *stage_schedule->fused_pairs()) {
        fused_pairs.push_back(deserialize_fused_pair(fused_pair));
    }
    bool touched = stage_schedule->touched();
    bool allow_race_conditions = stage_schedule->allow_race_conditions();
    bool atomic = stage_schedule->atomic();
    bool override_atomic_associativity_test = stage_schedule->override_atomic_associativity_test();
    return StageSchedule(rvars, splits, dims, prefetches, fuse_level, fused_pairs, touched,
                         allow_race_conditions, atomic, override_atomic_associativity_test);
}

BufferConstraint Deserializer::deserialize_buffer_constraint(const Serialize::BufferConstraint *buffer_constraint) {
    assert(buffer_constraint != nullptr);
    auto min = deserialize_expr(buffer_constraint->min_type(), buffer_constraint->min());
    auto extent = deserialize_expr(buffer_constraint->extent_type(), buffer_constraint->extent());
    auto stride = deserialize_expr(buffer_constraint->stride_type(), buffer_constraint->stride());
    auto min_estimate = deserialize_expr(buffer_constraint->min_estimate_type(), buffer_constraint->min_estimate());
    auto extent_estimate = deserialize_expr(buffer_constraint->extent_estimate_type(), buffer_constraint->extent_estimate());
    auto hl_buffer_constraint = BufferConstraint();
    hl_buffer_constraint.min = min;
    hl_buffer_constraint.extent = extent;
    hl_buffer_constraint.stride = stride;
    hl_buffer_constraint.min_estimate = min_estimate;
    return hl_buffer_constraint;
}

Parameter Deserializer::deserialize_parameter(const Serialize::Parameter *parameter) {
    assert(parameter != nullptr);
    bool defined = parameter->defined();
    if (!defined) {
        return Parameter();
    }
    bool is_buffer = parameter->is_buffer();
    auto type = deserialize_type(parameter->type());
    int dimensions = parameter->dimensions();
    std::string name = deserialize_string(parameter->name());
    if (is_buffer) {
        return Parameter(type, is_buffer, dimensions, name);
    } else {
        uint64_t data = parameter->data();
        auto scalar_default = deserialize_expr(parameter->scalar_default_type(), parameter->scalar_default());
        auto scalar_min = deserialize_expr(parameter->scalar_min_type(), parameter->scalar_min());
        auto scalar_max = deserialize_expr(parameter->scalar_max_type(), parameter->scalar_max());
        auto scalar_estimate = deserialize_expr(parameter->scalar_estimate_type(), parameter->scalar_estimate());
        return Parameter(type, is_buffer, dimensions, name, data, scalar_default, scalar_min, scalar_max, scalar_estimate);
    }
}

ExternFuncArgument Deserializer::deserialize_extern_func_argument(const Serialize::ExternFuncArgument *extern_func_argument) {
    assert(extern_func_argument != nullptr);
    auto arg_type = deserialize_extern_func_argument_type(extern_func_argument->arg_type());
    if (arg_type == ExternFuncArgument::ArgType::UndefinedArg) {
        return ExternFuncArgument();
    } else if (arg_type == ExternFuncArgument::ArgType::FuncArg) {
        int32_t func_index = extern_func_argument->func_index();
        FunctionPtr func_ptr;
        if (auto it = this->reverse_function_mappings.find(func_index); it != this->reverse_function_mappings.end() && func_index != -1) {
            func_ptr = it->second;
        }
        return ExternFuncArgument(func_ptr);
    } else if (arg_type == ExternFuncArgument::ArgType::BufferArg) {
        Buffer<> buffer;
        auto buffer_name = deserialize_string(extern_func_argument->buffer_name());
        if (auto it = buffers_in_pipeline.find(buffer_name); it != buffers_in_pipeline.end()) {
            buffer = it->second;
        }
        return ExternFuncArgument(buffer);
    } else if (arg_type == ExternFuncArgument::ArgType::ExprArg) {
        auto expr = deserialize_expr(extern_func_argument->expr_type(), extern_func_argument->expr());
        return ExternFuncArgument(expr);
    } else {
        auto image_param_name = deserialize_string(extern_func_argument->image_param_name());
        Parameter image_param;
        if (auto it = non_serialized_parameters.find(image_param_name); it != non_serialized_parameters.end()) {
            image_param = it->second;
        } else if (auto it = parameters_in_pipeline.find(image_param_name); it != parameters_in_pipeline.end()) {
            image_param = it->second;
        }
        return ExternFuncArgument(image_param);
    }
}

Buffer<> Deserializer::deserialize_buffer(const Serialize::Buffer *buffer) {
    assert(buffer != nullptr);
    if (!buffer->defined()) {
        return Buffer<>();
    }
    std::string name = deserialize_string(buffer->name());
    auto type = deserialize_type(buffer->type());
    int32_t dimensions = buffer->dimensions();
    std::vector<halide_dimension_t> buffer_dimensions;
    buffer_dimensions.reserve(dimensions);
    for (int i = 0; i < dimensions; ++i) {
        auto dim = buffer->dims()->Get(i);
        halide_dimension_t hl_dim;
        hl_dim.min = dim->min();
        hl_dim.extent = dim->extent();
        hl_dim.stride = dim->stride();
        buffer_dimensions.push_back(hl_dim);
    }
    auto fake_buffer = Buffer<>(type, nullptr, dimensions, buffer_dimensions.data(), name + "_fake");
    auto hl_buffer = Buffer<>::make_with_shape_of(fake_buffer, nullptr, nullptr, name);
    memcpy(hl_buffer.data(), buffer->data()->data(), buffer->data()->size());
    return hl_buffer;
}

std::map<std::string, FunctionPtr> Deserializer::deserialize_wrapper_refs(const flatbuffers::Vector<flatbuffers::Offset<Serialize::WrapperRef>> *wrappers) {
    assert(wrappers != nullptr);
    std::map<std::string, FunctionPtr> result;
    for (const auto &wrapper : *wrappers) {
        auto name = deserialize_string(wrapper->func_name());
        int32_t func_index = wrapper->func_index();
        FunctionPtr func_ptr;
        if (auto it = this->reverse_function_mappings.find(func_index); it != this->reverse_function_mappings.end() && func_index != -1) {
            func_ptr = it->second;
        }
        result[name] = func_ptr;
    }
    return result;
}

void Deserializer::build_reverse_function_mappings(const std::vector<Function> &functions) {
    if (!this->reverse_function_mappings.empty()) {
        this->reverse_function_mappings.clear();
    }
    int cnt = 0;
    for (const auto &f : functions) {
        this->reverse_function_mappings[cnt++] = f.get_contents();
    }
}

Pipeline Deserializer::deserialize(const std::string &filename) {
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    if (!in) {
        user_assert(false) << "failed to open file " << filename << "\n";
        return Pipeline();
    }
    in.seekg(0, std::ios::end);
    int size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> data(size);
    in.read(data.data(), size);
    in.close();

    const auto *pipeline_obj = Serialize::GetPipeline(data.data());
    std::vector<std::string> func_names_in_order;
    for (const auto &func_name : *pipeline_obj->func_names_in_order()) {
        func_names_in_order.push_back(deserialize_string(func_name));
    }

    // We use the first realized function to build the group and all other functions below to this same group
    std::vector<Function> functions;
    functions.reserve(func_names_in_order.size());
    if (!func_names_in_order.empty()) {
        functions.push_back(Function(func_names_in_order[0]));
        for (size_t i = 1; i < func_names_in_order.size(); ++i) {
            functions.push_back(functions[0].new_function_in_same_group(func_names_in_order[i]));
        }
    }
    build_reverse_function_mappings(functions);

    // Buffers need to be deserialized first as Parameters may reference them
    std::vector<Buffer<>> buffers;
    buffers.reserve(pipeline_obj->buffers()->size());
    for (const auto &buffer : *pipeline_obj->buffers()) {
        buffers.push_back(deserialize_buffer(buffer));
    }
    for (const auto &buffer : buffers) {
        if (buffers_in_pipeline.find(buffer.name()) == buffers_in_pipeline.end()) {
            buffers_in_pipeline[buffer.name()] = buffer;
        } else {
            user_assert(false) << "duplicate buffer " << buffer.name() << " in pipeline\n";
        }
    }
    std::vector<Parameter> parameters;
    parameters.reserve(pipeline_obj->parameters()->size());
    for (const auto &parameter : *pipeline_obj->parameters()) {
        parameters.push_back(deserialize_parameter(parameter));
    }
    for (const auto &param : parameters) {
        if (parameters_in_pipeline.find(param.name()) == parameters_in_pipeline.end()) {
            parameters_in_pipeline[param.name()] = param;
        } else {
            user_assert(false) << "duplicate parameter " << param.name() << " in pipeline\n";
        }
    }

    std::vector<Func> funcs;
    for (size_t i = 0; i < pipeline_obj->funcs()->size(); ++i) {
        deserialize_function(pipeline_obj->funcs()->Get(i), functions[i]);
        funcs.push_back(Func(functions[i]));
    }

    std::vector<std::string> output_names;
    output_names.reserve(pipeline_obj->output_names()->size());
    for (const auto &output_name : *pipeline_obj->output_names()) {
        output_names.push_back(deserialize_string(output_name));
    }
    std::vector<Func> output_funcs;
    for (const auto &f : funcs) {
        for (const auto &output_name : output_names) {
            if (f.name() == output_name) {
                output_funcs.push_back(f);
            }
        }
    }

    const auto *requirements_objs = pipeline_obj->requirements();
    const auto *requirement_type_objs = pipeline_obj->requirements_type();

    std::vector<Stmt> requirements;
    requirements.reserve(requirements_objs->size());
    for (size_t i = 0; i < requirements_objs->size(); ++i) {
        auto requirement_deserialized = deserialize_stmt(static_cast<Serialize::Stmt>(requirement_type_objs->Get(i)), requirements_objs->Get(i));
        requirements.push_back(requirement_deserialized);
    }
    return Pipeline(output_funcs);
}
}  // namespace Internal

Pipeline deserialize_pipeline(const std::string &filename, const std::unordered_map<std::string, Internal::Parameter> &params) {
    Internal::Deserializer deserializer(params);
    return deserializer.deserialize(filename);
}

}  // namespace Halide