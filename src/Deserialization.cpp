#include "Deserialization.h"

#ifdef WITH_SERIALIZATION

#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "Schedule.h"
#include "halide_ir.fbs.h"

#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Halide {
namespace Internal {

class Deserializer {
public:
    Deserializer() = default;

    explicit Deserializer(const std::map<std::string, Parameter> &user_params)
        : user_params(user_params) {
    }

    // Deserialize a pipeline from the given filename
    Pipeline deserialize(const std::string &filename);

    // Deserialize a pipeline from the given input stream
    Pipeline deserialize(std::istream &in);

    // Deserialize a pipeline from the given buffer of bytes
    Pipeline deserialize(const std::vector<uint8_t> &data);

    // Deserialize just the unbound external parameters that need to be defined for the pipeline from the given filename
    // (so they can be remapped and overridden with user parameters prior to deserializing the pipeline)
    std::map<std::string, Parameter> deserialize_parameters(const std::string &filename);

    // Deserialize just the unbound external parameters that need to be defined for the pipeline from the given input stream
    std::map<std::string, Parameter> deserialize_parameters(std::istream &in);

    // Deserialize just the unbound external parameters that need to be defined for the pipeline from the given buffer of bytes
    std::map<std::string, Parameter> deserialize_parameters(const std::vector<uint8_t> &data);

private:
    // Helper function to deserialize a homogenous vector from a flatbuffer vector,
    // does not apply to union types like Stmt and Expr or enum types like MemoryType
    template<typename src, typename dst>
    std::vector<dst> deserialize_vector(const flatbuffers::Vector<::flatbuffers::Offset<src>> *flatbuffer_vec,
                                        std::function<dst(Deserializer &, const src *)> deserialize_func) {
        user_assert(flatbuffer_vec != nullptr) << "deserializing a null vector\n";
        std::vector<dst> result;
        result.reserve(flatbuffer_vec->size());
        for (const auto &elem : *flatbuffer_vec) {
            result.push_back(deserialize_func(*this, elem));
        }
        return result;
    }

    // A lookup table for translating function ids to actual FunctionPtrs
    std::map<int32_t, FunctionPtr> reverse_function_mappings;

    // A lookup table for finding parameters object via their names,
    // used for preventing the same parameter being deserialized multiple times
    std::map<std::string, Parameter> parameters_in_pipeline;

    // A lookup table for finding buffer object via their names,
    // used for preventing the same buffer being deserialized multiple times
    std::map<std::string, Buffer<>> buffers_in_pipeline;

    // External parameters that are not deserialized but will be used in the pipeline
    std::map<std::string, Parameter> user_params;

    // Default external parameters that were created during deserialization
    std::map<std::string, Parameter> external_params;

    MemoryType deserialize_memory_type(Serialize::MemoryType memory_type);

    ForType deserialize_for_type(Serialize::ForType for_type);

    DeviceAPI deserialize_device_api(Serialize::DeviceAPI device_api);

    Partition deserialize_partition(Serialize::Partition partition);

    Call::CallType deserialize_call_type(Serialize::CallType call_type);

    VectorReduce::Operator deserialize_vector_reduce_op(Serialize::VectorReduceOp vector_reduce_op);

    PrefetchBoundStrategy deserialize_prefetch_bound_strategy(Serialize::PrefetchBoundStrategy prefetch_bound_strategy);

    NameMangling deserialize_name_mangling(Serialize::NameMangling name_mangling);

    TailStrategy deserialize_tail_strategy(Serialize::TailStrategy tail_strategy);

    Split::SplitType deserialize_split_type(Serialize::SplitType split_type);

    DimType deserialize_dim_type(Serialize::DimType dim_type);

    LoopAlignStrategy deserialize_loop_align_strategy(Serialize::LoopAlignStrategy loop_align_strategy);

    ExternFuncArgument::ArgType deserialize_extern_func_argument_type(Serialize::ExternFuncArgumentType extern_func_argument_type);

    std::string deserialize_string(const flatbuffers::String *str);

    Type deserialize_type(const Serialize::Type *type);

    void deserialize_function(const Serialize::Func *function, Function &hl_function);

    Stmt deserialize_stmt(Serialize::Stmt type_code, const void *stmt);

    Expr deserialize_expr(Serialize::Expr type_code, const void *expr);

    std::vector<Expr> deserialize_expr_vector(const flatbuffers::Vector<Serialize::Expr> *exprs_types, const flatbuffers::Vector<flatbuffers::Offset<void>> *exprs_serialized);

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

    Parameter deserialize_external_parameter(const Serialize::ExternalParameter *external_parameter);

    ExternFuncArgument deserialize_extern_func_argument(const Serialize::ExternFuncArgument *extern_func_argument);

    std::map<std::string, FunctionPtr> deserialize_wrapper_refs(const flatbuffers::Vector<flatbuffers::Offset<Serialize::WrapperRef>> *wrappers);

    Buffer<> deserialize_buffer(const Serialize::Buffer *buffer);

    void build_reverse_function_mappings(const std::vector<Function> &functions);
};

std::string Deserializer::deserialize_string(const flatbuffers::String *str) {
    user_assert(str != nullptr) << "deserializing a null string\n";
    return str->str();
}

MemoryType Deserializer::deserialize_memory_type(Serialize::MemoryType memory_type) {
    switch (memory_type) {
    case Serialize::MemoryType::Auto:
        return MemoryType::Auto;
    case Serialize::MemoryType::Heap:
        return MemoryType::Heap;
    case Serialize::MemoryType::Stack:
        return MemoryType::Stack;
    case Serialize::MemoryType::Register:
        return MemoryType::Register;
    case Serialize::MemoryType::GPUShared:
        return MemoryType::GPUShared;
    case Serialize::MemoryType::GPUTexture:
        return MemoryType::GPUTexture;
    case Serialize::MemoryType::LockedCache:
        return MemoryType::LockedCache;
    case Serialize::MemoryType::VTCM:
        return MemoryType::VTCM;
    case Serialize::MemoryType::AMXTile:
        return MemoryType::AMXTile;
    default:
        user_error << "unknown memory type " << (int)memory_type << "\n";
        return MemoryType::Auto;
    }
}

ForType Deserializer::deserialize_for_type(Serialize::ForType for_type) {
    switch (for_type) {
    case Serialize::ForType::Serial:
        return ForType::Serial;
    case Serialize::ForType::Parallel:
        return ForType::Parallel;
    case Serialize::ForType::Vectorized:
        return ForType::Vectorized;
    case Serialize::ForType::Unrolled:
        return ForType::Unrolled;
    case Serialize::ForType::Extern:
        return ForType::Extern;
    case Serialize::ForType::GPUBlock:
        return ForType::GPUBlock;
    case Serialize::ForType::GPUThread:
        return ForType::GPUThread;
    case Serialize::ForType::GPULane:
        return ForType::GPULane;
    default:
        user_error << "unknown for type " << (int)for_type << "\n";
        return ForType::Serial;
    }
}

Partition Deserializer::deserialize_partition(Serialize::Partition partition) {
    switch (partition) {
    case Serialize::Partition::Auto:
        return Halide::Partition::Auto;
    case Serialize::Partition::Never:
        return Halide::Partition::Never;
    case Serialize::Partition::Always:
        return Halide::Partition::Always;
    default:
        user_error << "unknown loop partition policy " << (int)partition << "\n";
        return Halide::Partition::Auto;
    }
}

DeviceAPI Deserializer::deserialize_device_api(Serialize::DeviceAPI device_api) {
    switch (device_api) {
    case Serialize::DeviceAPI::None:
        return DeviceAPI::None;
    case Serialize::DeviceAPI::Host:
        return DeviceAPI::Host;
    case Serialize::DeviceAPI::Default_GPU:
        return DeviceAPI::Default_GPU;
    case Serialize::DeviceAPI::CUDA:
        return DeviceAPI::CUDA;
    case Serialize::DeviceAPI::OpenCL:
        return DeviceAPI::OpenCL;
    case Serialize::DeviceAPI::OpenGLCompute:
        return DeviceAPI::OpenGLCompute;
    case Serialize::DeviceAPI::Metal:
        return DeviceAPI::Metal;
    case Serialize::DeviceAPI::Hexagon:
        return DeviceAPI::Hexagon;
    case Serialize::DeviceAPI::HexagonDma:
        return DeviceAPI::HexagonDma;
    case Serialize::DeviceAPI::D3D12Compute:
        return DeviceAPI::D3D12Compute;
    case Serialize::DeviceAPI::Vulkan:
        return DeviceAPI::Vulkan;
    case Serialize::DeviceAPI::WebGPU:
        return DeviceAPI::WebGPU;
    default:
        user_error << "unknown device api " << (int)device_api << "\n";
        return DeviceAPI::None;
    }
}

Call::CallType Deserializer::deserialize_call_type(Serialize::CallType call_type) {
    switch (call_type) {
    case Serialize::CallType::Image:
        return Call::CallType::Image;
    case Serialize::CallType::Extern:
        return Call::CallType::Extern;
    case Serialize::CallType::ExternCPlusPlus:
        return Call::CallType::ExternCPlusPlus;
    case Serialize::CallType::PureExtern:
        return Call::CallType::PureExtern;
    case Serialize::CallType::Halide:
        return Call::CallType::Halide;
    case Serialize::CallType::Intrinsic:
        return Call::CallType::Intrinsic;
    case Serialize::CallType::PureIntrinsic:
        return Call::CallType::PureIntrinsic;
    default:
        user_error << "unknown call type " << (int)call_type << "\n";
        return Call::CallType::Image;
    }
}

VectorReduce::Operator Deserializer::deserialize_vector_reduce_op(Serialize::VectorReduceOp vector_reduce_op) {
    switch (vector_reduce_op) {
    case Serialize::VectorReduceOp::Add:
        return VectorReduce::Operator::Add;
    case Serialize::VectorReduceOp::SaturatingAdd:
        return VectorReduce::Operator::SaturatingAdd;
    case Serialize::VectorReduceOp::Mul:
        return VectorReduce::Operator::Mul;
    case Serialize::VectorReduceOp::Min:
        return VectorReduce::Operator::Min;
    case Serialize::VectorReduceOp::Max:
        return VectorReduce::Operator::Max;
    case Serialize::VectorReduceOp::And:
        return VectorReduce::Operator::And;
    case Serialize::VectorReduceOp::Or:
        return VectorReduce::Operator::Or;
    default:
        user_error << "unknown vector reduce op " << (int)vector_reduce_op << "\n";
        return VectorReduce::Operator::Add;
    }
}

PrefetchBoundStrategy Deserializer::deserialize_prefetch_bound_strategy(Serialize::PrefetchBoundStrategy prefetch_bound_strategy) {
    switch (prefetch_bound_strategy) {
    case Serialize::PrefetchBoundStrategy::Clamp:
        return PrefetchBoundStrategy::Clamp;
    case Serialize::PrefetchBoundStrategy::GuardWithIf:
        return PrefetchBoundStrategy::GuardWithIf;
    case Serialize::PrefetchBoundStrategy::NonFaulting:
        return PrefetchBoundStrategy::NonFaulting;
    default:
        user_error << "unknown prefetch bound strategy " << (int)prefetch_bound_strategy << "\n";
        return PrefetchBoundStrategy::Clamp;
    }
}

NameMangling Deserializer::deserialize_name_mangling(Serialize::NameMangling name_mangling) {
    switch (name_mangling) {
    case Serialize::NameMangling::Default:
        return NameMangling::Default;
    case Serialize::NameMangling::C:
        return NameMangling::C;
    case Serialize::NameMangling::CPlusPlus:
        return NameMangling::CPlusPlus;
    default:
        user_error << "unknown name mangling " << (int)name_mangling << "\n";
        return NameMangling::Default;
    }
}

TailStrategy Deserializer::deserialize_tail_strategy(Serialize::TailStrategy tail_strategy) {
    switch (tail_strategy) {
    case Serialize::TailStrategy::RoundUp:
        return TailStrategy::RoundUp;
    case Serialize::TailStrategy::GuardWithIf:
        return TailStrategy::GuardWithIf;
    case Serialize::TailStrategy::Predicate:
        return TailStrategy::Predicate;
    case Serialize::TailStrategy::PredicateLoads:
        return TailStrategy::PredicateLoads;
    case Serialize::TailStrategy::PredicateStores:
        return TailStrategy::PredicateStores;
    case Serialize::TailStrategy::ShiftInwards:
        return TailStrategy::ShiftInwards;
    case Serialize::TailStrategy::ShiftInwardsAndBlend:
        return TailStrategy::ShiftInwardsAndBlend;
    case Serialize::TailStrategy::RoundUpAndBlend:
        return TailStrategy::RoundUpAndBlend;
    case Serialize::TailStrategy::Auto:
        return TailStrategy::Auto;
    default:
        user_error << "unknown tail strategy " << (int)tail_strategy << "\n";
        return TailStrategy::RoundUp;
    }
}

Split::SplitType Deserializer::deserialize_split_type(Serialize::SplitType split_type) {
    switch (split_type) {
    case Serialize::SplitType::SplitVar:
        return Split::SplitType::SplitVar;
    case Serialize::SplitType::RenameVar:
        return Split::SplitType::RenameVar;
    case Serialize::SplitType::FuseVars:
        return Split::SplitType::FuseVars;
    case Serialize::SplitType::PurifyRVar:
        return Split::SplitType::PurifyRVar;
    default:
        user_error << "unknown split type " << (int)split_type << "\n";
        return Split::SplitType::SplitVar;
    }
}

DimType Deserializer::deserialize_dim_type(Serialize::DimType dim_type) {
    switch (dim_type) {
    case Serialize::DimType::PureVar:
        return DimType::PureVar;
    case Serialize::DimType::PureRVar:
        return DimType::PureRVar;
    case Serialize::DimType::ImpureRVar:
        return DimType::ImpureRVar;
    default:
        user_error << "unknown dim type " << (int)dim_type << "\n";
        return DimType::PureVar;
    }
}

LoopAlignStrategy Deserializer::deserialize_loop_align_strategy(Serialize::LoopAlignStrategy loop_align_strategy) {
    switch (loop_align_strategy) {
    case Serialize::LoopAlignStrategy::AlignStart:
        return LoopAlignStrategy::AlignStart;
    case Serialize::LoopAlignStrategy::AlignEnd:
        return LoopAlignStrategy::AlignEnd;
    case Serialize::LoopAlignStrategy::NoAlign:
        return LoopAlignStrategy::NoAlign;
    case Serialize::LoopAlignStrategy::Auto:
        return LoopAlignStrategy::Auto;
    default:
        user_error << "unknown loop align strategy " << (int)loop_align_strategy << "\n";
        return LoopAlignStrategy::AlignStart;
    }
}

ExternFuncArgument::ArgType Deserializer::deserialize_extern_func_argument_type(Serialize::ExternFuncArgumentType extern_func_argument_type) {
    switch (extern_func_argument_type) {
    case Serialize::ExternFuncArgumentType::UndefinedArg:
        return ExternFuncArgument::ArgType::UndefinedArg;
    case Serialize::ExternFuncArgumentType::FuncArg:
        return ExternFuncArgument::ArgType::FuncArg;
    case Serialize::ExternFuncArgumentType::BufferArg:
        return ExternFuncArgument::ArgType::BufferArg;
    case Serialize::ExternFuncArgumentType::ExprArg:
        return ExternFuncArgument::ArgType::ExprArg;
    case Serialize::ExternFuncArgumentType::ImageParamArg:
        return ExternFuncArgument::ArgType::ImageParamArg;
    default:
        user_error << "unknown extern func argument type " << (int)extern_func_argument_type << "\n";
        return ExternFuncArgument::ArgType::UndefinedArg;
    }
}

Type Deserializer::deserialize_type(const Serialize::Type *type) {
    user_assert(type != nullptr) << "deserializing a null Type\n";
    using Serialize::TypeCode;
    const int bits = type->bits();
    const int lanes = type->lanes();
    const TypeCode code_deserialized = type->code();
    halide_type_code_t code = halide_type_uint;
    switch (code_deserialized) {
    case TypeCode::Int:
        code = halide_type_int;
        break;
    case TypeCode::UInt:
        code = halide_type_uint;
        break;
    case TypeCode::Float:
        code = halide_type_float;
        break;
    case TypeCode::Handle:
        code = halide_type_handle;
        break;
    case TypeCode::BFloat:
        code = halide_type_bfloat;
        break;
    default:
        user_error << "unknown type code " << (int)code_deserialized << "\n";
    }

    return Type(code, bits, lanes);
}

void Deserializer::deserialize_function(const Serialize::Func *function, Function &hl_function) {
    user_assert(function != nullptr) << "deserializing a null Function\n";
    const std::string name = deserialize_string(function->name());
    const std::string origin_name = deserialize_string(function->origin_name());
    const std::vector<Type> output_types =
        deserialize_vector<Serialize::Type, Type>(function->output_types(),
                                                  &Deserializer::deserialize_type);
    const std::vector<Type> required_types =
        deserialize_vector<Serialize::Type, Type>(function->required_types(),
                                                  &Deserializer::deserialize_type);
    const int required_dim = function->required_dims();
    const std::vector<std::string> args =
        deserialize_vector<flatbuffers::String, std::string>(function->args(),
                                                             &Deserializer::deserialize_string);
    const auto func_schedule = deserialize_func_schedule(function->func_schedule());
    const auto init_def = deserialize_definition(function->init_def());
    const std::vector<Definition> updates =
        deserialize_vector<Serialize::Definition, Definition>(function->updates(),
                                                              &Deserializer::deserialize_definition);
    const std::string debug_file = deserialize_string(function->debug_file());

    std::vector<Parameter> output_buffers;
    output_buffers.reserve(function->output_buffers_names()->size());
    for (const auto &output_buffer_name_serialized : *function->output_buffers_names()) {
        auto output_buffer_name = deserialize_string(output_buffer_name_serialized);
        Parameter output_buffer;
        if (auto it = user_params.find(output_buffer_name); it != user_params.end()) {
            output_buffer = it->second;
        } else if (auto it = external_params.find(output_buffer_name); it != external_params.end()) {
            output_buffer = it->second;
        } else if (auto it = parameters_in_pipeline.find(output_buffer_name); it != parameters_in_pipeline.end()) {
            output_buffer = it->second;
        } else if (!output_buffer_name.empty()) {
            user_error << "unknown output buffer used in pipeline '" << output_buffer_name << "'\n";
        }
        output_buffers.push_back(output_buffer);
    }
    const std::vector<ExternFuncArgument> extern_arguments =
        deserialize_vector<Serialize::ExternFuncArgument, ExternFuncArgument>(function->extern_arguments(),
                                                                              &Deserializer::deserialize_extern_func_argument);
    const std::string extern_function_name = deserialize_string(function->extern_function_name());
    const auto name_mangling = deserialize_name_mangling(function->extern_mangling());
    const auto extern_function_device_api = deserialize_device_api(function->extern_function_device_api());
    const auto extern_proxy_expr = deserialize_expr(function->extern_proxy_expr_type(), function->extern_proxy_expr());
    const bool trace_loads = function->trace_loads();
    const bool trace_stores = function->trace_stores();
    const bool trace_realizations = function->trace_realizations();
    const std::vector<std::string> trace_tags =
        deserialize_vector<flatbuffers::String, std::string>(function->trace_tags(),
                                                             &Deserializer::deserialize_string);
    const bool frozen = function->frozen();
    hl_function.update_with_deserialization(name, origin_name, output_types, required_types,
                                            required_dim, args, func_schedule, init_def, updates,
                                            debug_file, output_buffers, extern_arguments, extern_function_name,
                                            name_mangling, extern_function_device_api, extern_proxy_expr,
                                            trace_loads, trace_stores, trace_realizations, trace_tags, frozen);
}

Stmt Deserializer::deserialize_stmt(Serialize::Stmt type_code, const void *stmt) {
    user_assert(stmt != nullptr) << "deserializing a null Stmt\n";
    switch (type_code) {
    case Serialize::Stmt::LetStmt: {
        const auto *let_stmt = (const Serialize::LetStmt *)stmt;
        const auto name = deserialize_string(let_stmt->name());
        const auto value = deserialize_expr(let_stmt->value_type(), let_stmt->value());
        const auto body = deserialize_stmt(let_stmt->body_type(), let_stmt->body());
        return LetStmt::make(name, value, body);
    }
    case Serialize::Stmt::AssertStmt: {
        const auto *assert_stmt = (const Serialize::AssertStmt *)stmt;
        const auto condition = deserialize_expr(assert_stmt->condition_type(), assert_stmt->condition());
        const auto message = deserialize_expr(assert_stmt->message_type(), assert_stmt->message());
        return AssertStmt::make(condition, message);
    }
    case Serialize::Stmt::ProducerConsumer: {
        const auto *producer_consumer = (const Serialize::ProducerConsumer *)stmt;
        const auto name = deserialize_string(producer_consumer->name());
        const auto is_producer = producer_consumer->is_producer();
        const auto body = deserialize_stmt(producer_consumer->body_type(), producer_consumer->body());
        return ProducerConsumer::make(name, is_producer, body);
    }
    case Serialize::Stmt::For: {
        const auto *for_stmt = (const Serialize::For *)stmt;
        const auto name = deserialize_string(for_stmt->name());
        const auto min = deserialize_expr(for_stmt->min_type(), for_stmt->min());
        const auto extent = deserialize_expr(for_stmt->extent_type(), for_stmt->extent());
        const ForType for_type = deserialize_for_type(for_stmt->for_type());
        const Partition partition_policy = deserialize_partition(for_stmt->partition_policy());
        const DeviceAPI device_api = deserialize_device_api(for_stmt->device_api());
        const auto body = deserialize_stmt(for_stmt->body_type(), for_stmt->body());
        return For::make(name, min, extent, for_type, partition_policy, device_api, body);
    }
    case Serialize::Stmt::Store: {
        const auto *store_stmt = (const Serialize::Store *)stmt;
        const auto name = deserialize_string(store_stmt->name());
        const auto predicate = deserialize_expr(store_stmt->predicate_type(), store_stmt->predicate());
        const auto value = deserialize_expr(store_stmt->value_type(), store_stmt->value());
        const auto index = deserialize_expr(store_stmt->index_type(), store_stmt->index());
        const auto param_name = deserialize_string(store_stmt->param_name());
        Parameter param;
        if (auto it = user_params.find(param_name); it != user_params.end()) {
            param = it->second;
        } else if (auto it = external_params.find(param_name); it != external_params.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        } else if (!param_name.empty()) {
            user_error << "unknown parameter used in pipeline '" << param_name << "'\n";
        }
        const auto alignment = deserialize_modulus_remainder(store_stmt->alignment());
        return Store::make(name, value, index, param, predicate, alignment);
    }
    case Serialize::Stmt::Provide: {
        const auto *provide_stmt = (const Serialize::Provide *)stmt;
        const auto name = deserialize_string(provide_stmt->name());
        const std::vector<Expr> values = deserialize_expr_vector(provide_stmt->values_type(), provide_stmt->values());
        const std::vector<Expr> args = deserialize_expr_vector(provide_stmt->args_type(), provide_stmt->args());
        const auto predicate = deserialize_expr(provide_stmt->predicate_type(), provide_stmt->predicate());
        return Provide::make(name, values, args, predicate);
    }
    case Serialize::Stmt::Allocate: {
        const auto *allocate_stmt = (const Serialize::Allocate *)stmt;
        const auto name = deserialize_string(allocate_stmt->name());
        const auto type = deserialize_type(allocate_stmt->type());
        const MemoryType memory_type = deserialize_memory_type(allocate_stmt->memory_type());
        const std::vector<Expr> extents = deserialize_expr_vector(allocate_stmt->extents_type(), allocate_stmt->extents());
        const auto condition = deserialize_expr(allocate_stmt->condition_type(), allocate_stmt->condition());
        const auto new_expr = deserialize_expr(allocate_stmt->new_expr_type(), allocate_stmt->new_expr());
        const auto free_function = deserialize_string(allocate_stmt->free_function());
        const auto padding = allocate_stmt->padding();
        const auto body = deserialize_stmt(allocate_stmt->body_type(), allocate_stmt->body());
        return Allocate::make(name, type, memory_type, extents, condition, body, new_expr, free_function, padding);
    }
    case Serialize::Stmt::Free: {
        const auto *free_stmt = (const Serialize::Free *)stmt;
        const auto name = deserialize_string(free_stmt->name());
        return Free::make(name);
    }
    case Serialize::Stmt::Realize: {
        const auto *realize_stmt = (const Serialize::Realize *)stmt;
        const auto name = deserialize_string(realize_stmt->name());
        const std::vector<Type> types = deserialize_vector<Serialize::Type, Type>(realize_stmt->types(),
                                                                                  &Deserializer::deserialize_type);
        const MemoryType memory_type = deserialize_memory_type(realize_stmt->memory_type());
        const std::vector<Range> bounds = deserialize_vector<Serialize::Range, Range>(realize_stmt->bounds(),
                                                                                      &Deserializer::deserialize_range);
        const auto condition = deserialize_expr(realize_stmt->condition_type(), realize_stmt->condition());
        const auto body = deserialize_stmt(realize_stmt->body_type(), realize_stmt->body());
        return Realize::make(name, types, memory_type, bounds, condition, body);
    }
    case Serialize::Stmt::Block: {
        const auto *block_stmt = (const Serialize::Block *)stmt;
        const auto first = deserialize_stmt(block_stmt->first_type(), block_stmt->first());
        const auto rest = deserialize_stmt(block_stmt->rest_type(), block_stmt->rest());
        return Block::make(first, rest);
    }
    case Serialize::Stmt::IfThenElse: {
        const auto *if_then_else_stmt = (const Serialize::IfThenElse *)stmt;
        const auto condition = deserialize_expr(if_then_else_stmt->condition_type(), if_then_else_stmt->condition());
        const auto then_case = deserialize_stmt(if_then_else_stmt->then_case_type(), if_then_else_stmt->then_case());
        const auto else_case = deserialize_stmt(if_then_else_stmt->else_case_type(), if_then_else_stmt->else_case());
        return IfThenElse::make(condition, then_case, else_case);
    }
    case Serialize::Stmt::Evaluate: {
        const auto *evaluate_stmt = (const Serialize::Evaluate *)stmt;
        const auto value = deserialize_expr(evaluate_stmt->value_type(), evaluate_stmt->value());
        return Evaluate::make(value);
    }
    case Serialize::Stmt::Prefetch: {
        const auto *prefetch_stmt = (const Serialize::Prefetch *)stmt;
        const auto name = deserialize_string(prefetch_stmt->name());
        const std::vector<Type> types = deserialize_vector<Serialize::Type, Type>(prefetch_stmt->types(),
                                                                                  &Deserializer::deserialize_type);
        const std::vector<Range> bounds = deserialize_vector<Serialize::Range, Range>(prefetch_stmt->bounds(),
                                                                                      &Deserializer::deserialize_range);
        const auto prefetch = deserialize_prefetch_directive(prefetch_stmt->prefetch());
        const auto condition = deserialize_expr(prefetch_stmt->condition_type(), prefetch_stmt->condition());
        const auto body = deserialize_stmt(prefetch_stmt->body_type(), prefetch_stmt->body());
        return Prefetch::make(name, types, bounds, prefetch, condition, body);
    }
    case Serialize::Stmt::Acquire: {
        const auto *acquire_stmt = (const Serialize::Acquire *)stmt;
        const auto semaphore = deserialize_expr(acquire_stmt->semaphore_type(), acquire_stmt->semaphore());
        const auto count = deserialize_expr(acquire_stmt->count_type(), acquire_stmt->count());
        const auto body = deserialize_stmt(acquire_stmt->body_type(), acquire_stmt->body());
        return Acquire::make(semaphore, count, body);
    }
    case Serialize::Stmt::Fork: {
        const auto *fork_stmt = (const Serialize::Fork *)stmt;
        const auto first = deserialize_stmt(fork_stmt->first_type(), fork_stmt->first());
        const auto rest = deserialize_stmt(fork_stmt->rest_type(), fork_stmt->rest());
        return Fork::make(first, rest);
    }
    case Serialize::Stmt::Atomic: {
        const auto *atomic_stmt = (const Serialize::Atomic *)stmt;
        const auto producer_name = deserialize_string(atomic_stmt->producer_name());
        const auto mutex_name = deserialize_string(atomic_stmt->mutex_name());
        const auto body = deserialize_stmt(atomic_stmt->body_type(), atomic_stmt->body());
        return Atomic::make(producer_name, mutex_name, body);
    }
    case Serialize::Stmt::HoistedStorage: {
        const auto *hoisted_storage_stmt = (const Serialize::HoistedStorage *)stmt;
        const auto name = deserialize_string(hoisted_storage_stmt->name());
        const auto body = deserialize_stmt(hoisted_storage_stmt->body_type(), hoisted_storage_stmt->body());
        return HoistedStorage::make(name, body);
    }
    case Serialize::Stmt::UndefinedStmt: {
        return Stmt();
    }
    default:
        user_error << "unknown type code " << (int)type_code << "\n";
        return Stmt();
    }
}

Expr Deserializer::deserialize_expr(Serialize::Expr type_code, const void *expr) {
    user_assert(expr != nullptr);
    switch (type_code) {
    case Serialize::Expr::IntImm: {
        const auto *int_imm_expr = (const Serialize::IntImm *)expr;
        const auto value = int_imm_expr->value();
        const auto type = deserialize_type(int_imm_expr->type());
        return IntImm::make(type, value);
    }
    case Serialize::Expr::UIntImm: {
        const auto *uint_imm_expr = (const Serialize::UIntImm *)expr;
        const auto value = uint_imm_expr->value();
        const auto type = deserialize_type(uint_imm_expr->type());
        return UIntImm::make(type, value);
    }
    case Serialize::Expr::FloatImm: {
        const auto *float_imm_expr = (const Serialize::FloatImm *)expr;
        const auto value = float_imm_expr->value();
        const auto type = deserialize_type(float_imm_expr->type());
        return FloatImm::make(type, value);
    }
    case Serialize::Expr::StringImm: {
        const auto *string_imm_expr = (const Serialize::StringImm *)expr;
        const auto value = deserialize_string(string_imm_expr->value());
        return StringImm::make(value);
    }
    case Serialize::Expr::Cast: {
        const auto *cast_expr = (const Serialize::Cast *)expr;
        const auto value = deserialize_expr(cast_expr->value_type(), cast_expr->value());
        const auto type = deserialize_type(cast_expr->type());
        return Cast::make(type, value);
    }
    case Serialize::Expr::Reinterpret: {
        const auto *reinterpret_expr = (const Serialize::Reinterpret *)expr;
        const auto value = deserialize_expr(reinterpret_expr->value_type(), reinterpret_expr->value());
        const auto type = deserialize_type(reinterpret_expr->type());
        return Reinterpret::make(type, value);
    }
    case Serialize::Expr::Add: {
        const auto *add_expr = (const Serialize::Add *)expr;
        const auto a = deserialize_expr(add_expr->a_type(), add_expr->a());
        const auto b = deserialize_expr(add_expr->b_type(), add_expr->b());
        return Add::make(a, b);
    }
    case Serialize::Expr::Sub: {
        const auto *sub_expr = (const Serialize::Sub *)expr;
        const auto a = deserialize_expr(sub_expr->a_type(), sub_expr->a());
        const auto b = deserialize_expr(sub_expr->b_type(), sub_expr->b());
        return Sub::make(a, b);
    }
    case Serialize::Expr::Mul: {
        const auto *mul_expr = (const Serialize::Mul *)expr;
        const auto a = deserialize_expr(mul_expr->a_type(), mul_expr->a());
        const auto b = deserialize_expr(mul_expr->b_type(), mul_expr->b());
        return Mul::make(a, b);
    }
    case Serialize::Expr::Div: {
        const auto *div_expr = (const Serialize::Div *)expr;
        const auto a = deserialize_expr(div_expr->a_type(), div_expr->a());
        const auto b = deserialize_expr(div_expr->b_type(), div_expr->b());
        return Div::make(a, b);
    }
    case Serialize::Expr::Mod: {
        const auto *mod_expr = (const Serialize::Mod *)expr;
        const auto a = deserialize_expr(mod_expr->a_type(), mod_expr->a());
        const auto b = deserialize_expr(mod_expr->b_type(), mod_expr->b());
        return Mod::make(a, b);
    }
    case Serialize::Expr::Min: {
        const auto *min_expr = (const Serialize::Min *)expr;
        const auto a = deserialize_expr(min_expr->a_type(), min_expr->a());
        const auto b = deserialize_expr(min_expr->b_type(), min_expr->b());
        return Min::make(a, b);
    }
    case Serialize::Expr::Max: {
        const auto *max_expr = (const Serialize::Max *)expr;
        const auto a = deserialize_expr(max_expr->a_type(), max_expr->a());
        const auto b = deserialize_expr(max_expr->b_type(), max_expr->b());
        return Max::make(a, b);
    }
    case Serialize::Expr::EQ: {
        const auto *eq_expr = (const Serialize::EQ *)expr;
        const auto a = deserialize_expr(eq_expr->a_type(), eq_expr->a());
        const auto b = deserialize_expr(eq_expr->b_type(), eq_expr->b());
        return EQ::make(a, b);
    }
    case Serialize::Expr::NE: {
        const auto *ne_expr = (const Serialize::NE *)expr;
        const auto a = deserialize_expr(ne_expr->a_type(), ne_expr->a());
        const auto b = deserialize_expr(ne_expr->b_type(), ne_expr->b());
        return NE::make(a, b);
    }
    case Serialize::Expr::LT: {
        const Serialize::LT *lt_expr = (const Serialize::LT *)expr;
        const auto a = deserialize_expr(lt_expr->a_type(), lt_expr->a());
        const auto b = deserialize_expr(lt_expr->b_type(), lt_expr->b());
        return LT::make(a, b);
    }
    case Serialize::Expr::LE: {
        const auto *le_expr = (const Serialize::LE *)expr;
        const auto a = deserialize_expr(le_expr->a_type(), le_expr->a());
        const auto b = deserialize_expr(le_expr->b_type(), le_expr->b());
        return LE::make(a, b);
    }
    case Serialize::Expr::GT: {
        const auto *gt_expr = (const Serialize::GT *)expr;
        const auto a = deserialize_expr(gt_expr->a_type(), gt_expr->a());
        const auto b = deserialize_expr(gt_expr->b_type(), gt_expr->b());
        return GT::make(a, b);
    }
    case Serialize::Expr::GE: {
        const auto *ge_expr = (const Serialize::GE *)expr;
        const auto a = deserialize_expr(ge_expr->a_type(), ge_expr->a());
        const auto b = deserialize_expr(ge_expr->b_type(), ge_expr->b());
        return GE::make(a, b);
    }
    case Serialize::Expr::And: {
        const auto *and_expr = (const Serialize::And *)expr;
        const auto a = deserialize_expr(and_expr->a_type(), and_expr->a());
        const auto b = deserialize_expr(and_expr->b_type(), and_expr->b());
        return And::make(a, b);
    }
    case Serialize::Expr::Or: {
        const auto *or_expr = (const Serialize::Or *)expr;
        const auto a = deserialize_expr(or_expr->a_type(), or_expr->a());
        const auto b = deserialize_expr(or_expr->b_type(), or_expr->b());
        return Or::make(a, b);
    }
    case Serialize::Expr::Not: {
        const auto *not_expr = (const Serialize::Not *)expr;
        const auto a = deserialize_expr(not_expr->a_type(), not_expr->a());
        return Not::make(a);
    }
    case Serialize::Expr::Select: {
        const auto *select_expr = (const Serialize::Select *)expr;
        const auto condition = deserialize_expr(select_expr->condition_type(), select_expr->condition());
        const auto true_value = deserialize_expr(select_expr->true_value_type(), select_expr->true_value());
        const auto false_value = deserialize_expr(select_expr->false_value_type(), select_expr->false_value());
        return Select::make(condition, true_value, false_value);
    }
    case Serialize::Expr::Load: {
        const auto *load_expr = (const Serialize::Load *)expr;
        const auto name = deserialize_string(load_expr->name());
        const auto predicate = deserialize_expr(load_expr->predicate_type(), load_expr->predicate());
        const auto index = deserialize_expr(load_expr->index_type(), load_expr->index());
        Buffer<> image;
        const auto image_name = deserialize_string(load_expr->image_name());
        if (auto it = buffers_in_pipeline.find(image_name); it != buffers_in_pipeline.end()) {
            image = it->second;
        }
        const auto param_name = deserialize_string(load_expr->param_name());
        Parameter param;
        if (auto it = user_params.find(param_name); it != user_params.end()) {
            param = it->second;
        } else if (auto it = external_params.find(param_name); it != external_params.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        } else if (!param_name.empty()) {
            user_error << "unknown parameter used in pipeline '" << param_name << "'\n";
        }
        const auto alignment = deserialize_modulus_remainder(load_expr->alignment());
        const auto type = deserialize_type(load_expr->type());
        return Load::make(type, name, index, image, param, predicate, alignment);
    }
    case Serialize::Expr::Ramp: {
        const auto *ramp_expr = (const Serialize::Ramp *)expr;
        const auto base = deserialize_expr(ramp_expr->base_type(), ramp_expr->base());
        const auto stride = deserialize_expr(ramp_expr->stride_type(), ramp_expr->stride());
        const auto lanes = ramp_expr->lanes();
        return Ramp::make(base, stride, lanes);
    }
    case Serialize::Expr::Broadcast: {
        const auto *broadcast_expr = (const Serialize::Broadcast *)expr;
        const auto value = deserialize_expr(broadcast_expr->value_type(), broadcast_expr->value());
        const auto lanes = broadcast_expr->lanes();
        return Broadcast::make(value, lanes);
    }
    case Serialize::Expr::Let: {
        const auto *let_expr = (const Serialize::Let *)expr;
        auto name = deserialize_string(let_expr->name());
        auto value = deserialize_expr(let_expr->value_type(), let_expr->value());
        auto body = deserialize_expr(let_expr->body_type(), let_expr->body());
        return Let::make(name, value, body);
    }
    case Serialize::Expr::Call: {
        const auto *call_expr = (const Serialize::Call *)expr;
        const auto name = deserialize_string(call_expr->name());
        std::vector<Expr> args = deserialize_expr_vector(call_expr->args_type(), call_expr->args());
        const auto value_index = call_expr->value_index();
        const int func_index = call_expr->func_index();
        FunctionPtr func_ptr;
        if (auto it = this->reverse_function_mappings.find(func_index); it != this->reverse_function_mappings.end() && func_index != -1) {
            FunctionPtr called_func_ptr = it->second;
            func_ptr.weak = called_func_ptr.group();
            func_ptr.idx = called_func_ptr.idx;
        }
        const auto call_type = deserialize_call_type(call_expr->call_type());
        const auto image_name = deserialize_string(call_expr->image_name());
        Buffer<> image;
        if (auto it = buffers_in_pipeline.find(image_name); it != buffers_in_pipeline.end()) {
            image = it->second;
        }
        const auto param_name = deserialize_string(call_expr->param_name());
        Parameter param;
        if (auto it = user_params.find(param_name); it != user_params.end()) {
            param = it->second;
        } else if (auto it = external_params.find(param_name); it != external_params.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        } else if (!param_name.empty()) {
            user_error << "unknown parameter used in pipeline '" << param_name << "'\n";
        }
        const auto type = deserialize_type(call_expr->type());
        return Call::make(type, name, args, call_type, func_ptr, value_index, image, param);
    }
    case Serialize::Expr::Variable: {
        const auto *variable_expr = (const Serialize::Variable *)expr;
        const auto name = deserialize_string(variable_expr->name());
        const auto type = deserialize_type(variable_expr->type());
        const auto param_name = deserialize_string(variable_expr->param_name());
        Parameter param;
        if (auto it = user_params.find(param_name); it != user_params.end()) {
            param = it->second;
        } else if (auto it = external_params.find(param_name); it != external_params.end()) {
            param = it->second;
        } else if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
            param = it->second;
        } else if (!param_name.empty()) {
            user_error << "unknown parameter used in pipeline '" << param_name << "'\n";
        }
        auto image_name = deserialize_string(variable_expr->image_name());
        Buffer<> image;
        if (auto it = buffers_in_pipeline.find(image_name); it != buffers_in_pipeline.end()) {
            image = it->second;
        }
        const auto reduction_domain = deserialize_reduction_domain(variable_expr->reduction_domain());
        return Variable::make(type, name, image, param, reduction_domain);
    }
    case Serialize::Expr::Shuffle: {
        const auto *shuffle_expr = (const Serialize::Shuffle *)expr;
        std::vector<Expr> vectors = deserialize_expr_vector(shuffle_expr->vectors_type(), shuffle_expr->vectors());
        const auto *const indices_serialized = shuffle_expr->indices();
        std::vector<int32_t> indices;
        indices.reserve(indices_serialized->size());
        for (size_t i = 0; i < indices_serialized->size(); ++i) {
            indices.push_back(indices_serialized->Get(i));
        }
        return Shuffle::make(vectors, indices);
    }
    case Serialize::Expr::VectorReduce: {
        const auto *vector_reduce_expr = (const Serialize::VectorReduce *)expr;
        const auto value = deserialize_expr(vector_reduce_expr->value_type(), vector_reduce_expr->value());
        const auto reduction_op = deserialize_vector_reduce_op(vector_reduce_expr->reduction_op());
        const int32_t lanes = vector_reduce_expr->lanes();
        return VectorReduce::make(reduction_op, value, lanes);
    }
    case Serialize::Expr::UndefinedExpr: {
        return Expr();
    }
    default: {
        user_error << "unknown type code " << (int)type_code << "\n";
        return Expr();
    }
    }
}

std::vector<Expr> Deserializer::deserialize_expr_vector(const flatbuffers::Vector<Serialize::Expr> *exprs_types,
                                                        const flatbuffers::Vector<flatbuffers::Offset<void>> *exprs_serialized) {
    user_assert(exprs_types != nullptr);
    user_assert(exprs_serialized != nullptr);
    std::vector<Expr> result;
    result.reserve(exprs_serialized->size());
    for (size_t i = 0; i < exprs_serialized->size(); ++i) {
        auto expr = deserialize_expr(static_cast<Serialize::Expr>(exprs_types->Get(i)), exprs_serialized->Get(i));
        result.push_back(expr);
    }
    return result;
}

Range Deserializer::deserialize_range(const Serialize::Range *range) {
    user_assert(range != nullptr);
    const auto min = deserialize_expr(range->min_type(), range->min());
    const auto extent = deserialize_expr(range->extent_type(), range->extent());
    return Range(min, extent);
}

Bound Deserializer::deserialize_bound(const Serialize::Bound *bound) {
    user_assert(bound != nullptr);
    const auto var = deserialize_string(bound->var());
    const auto min = deserialize_expr(bound->min_type(), bound->min());
    const auto extent = deserialize_expr(bound->extent_type(), bound->extent());
    const auto modulus = deserialize_expr(bound->modulus_type(), bound->modulus());
    const auto remainder = deserialize_expr(bound->remainder_type(), bound->remainder());
    auto hl_bound = Bound();
    hl_bound.var = var;
    hl_bound.min = min;
    hl_bound.extent = extent;
    hl_bound.modulus = modulus;
    hl_bound.remainder = remainder;
    return hl_bound;
}

StorageDim Deserializer::deserialize_storage_dim(const Serialize::StorageDim *storage_dim) {
    user_assert(storage_dim != nullptr);
    const auto var = deserialize_string(storage_dim->var());
    const auto alignment = deserialize_expr(storage_dim->alignment_type(), storage_dim->alignment());
    const auto bound = deserialize_expr(storage_dim->bound_type(), storage_dim->bound());
    const auto fold_factor = deserialize_expr(storage_dim->fold_factor_type(), storage_dim->fold_factor());
    const auto fold_forward = storage_dim->fold_forward();
    auto hl_storage_dim = StorageDim();
    hl_storage_dim.var = var;
    hl_storage_dim.alignment = alignment;
    hl_storage_dim.bound = bound;
    hl_storage_dim.fold_factor = fold_factor;
    hl_storage_dim.fold_forward = fold_forward;
    return hl_storage_dim;
}

LoopLevel Deserializer::deserialize_loop_level(const Serialize::LoopLevel *loop_level) {
    user_assert(loop_level != nullptr);
    const auto func_name = deserialize_string(loop_level->func_name());
    const auto stage_index = loop_level->stage_index();
    const auto var_name = deserialize_string(loop_level->var_name());
    const auto is_rvar = loop_level->is_rvar();
    const auto locked = loop_level->locked();
    return LoopLevel(func_name, var_name, is_rvar, stage_index, locked);
}

FuncSchedule Deserializer::deserialize_func_schedule(const Serialize::FuncSchedule *func_schedule) {
    user_assert(func_schedule != nullptr);
    const auto store_level = deserialize_loop_level(func_schedule->store_level());
    const auto compute_level = deserialize_loop_level(func_schedule->compute_level());
    const auto hoist_storage_level = deserialize_loop_level(func_schedule->hoist_storage_level());
    const std::vector<StorageDim> storage_dims =
        deserialize_vector<Serialize::StorageDim, StorageDim>(func_schedule->storage_dims(),
                                                              &Deserializer::deserialize_storage_dim);
    const std::vector<Bound> bounds = deserialize_vector<Serialize::Bound, Bound>(func_schedule->bounds(),
                                                                                  &Deserializer::deserialize_bound);
    const std::vector<Bound> estimates = deserialize_vector<Serialize::Bound, Bound>(func_schedule->estimates(),
                                                                                     &Deserializer::deserialize_bound);
    const std::map<std::string, FunctionPtr> wrappers = deserialize_wrapper_refs(func_schedule->wrappers());
    const auto memory_type = deserialize_memory_type(func_schedule->memory_type());
    const auto memoized = func_schedule->memoized();
    const auto async = func_schedule->async();
    const auto ring_buffer = deserialize_expr(func_schedule->ring_buffer_type(), func_schedule->ring_buffer());
    const auto memoize_eviction_key = deserialize_expr(func_schedule->memoize_eviction_key_type(), func_schedule->memoize_eviction_key());
    auto hl_func_schedule = FuncSchedule();
    hl_func_schedule.store_level() = store_level;
    hl_func_schedule.compute_level() = compute_level;
    hl_func_schedule.hoist_storage_level() = hoist_storage_level;
    hl_func_schedule.storage_dims() = storage_dims;
    hl_func_schedule.bounds() = bounds;
    hl_func_schedule.estimates() = estimates;
    hl_func_schedule.wrappers() = wrappers;
    hl_func_schedule.memory_type() = memory_type;
    hl_func_schedule.memoized() = memoized;
    hl_func_schedule.async() = async;
    hl_func_schedule.ring_buffer() = ring_buffer;
    hl_func_schedule.memoize_eviction_key() = memoize_eviction_key;
    return hl_func_schedule;
}

Specialization Deserializer::deserialize_specialization(const Serialize::Specialization *specialization) {
    user_assert(specialization != nullptr);
    const auto condition = deserialize_expr(specialization->condition_type(), specialization->condition());
    const auto defintion = deserialize_definition(specialization->definition());
    const auto failure_message = deserialize_string(specialization->failure_message());
    Specialization hl_specialization;
    hl_specialization.condition = condition;
    hl_specialization.definition = defintion;
    hl_specialization.failure_message = failure_message;
    return hl_specialization;
}

Definition Deserializer::deserialize_definition(const Serialize::Definition *definition) {
    user_assert(definition != nullptr);
    const auto is_init = definition->is_init();
    const auto predicate = deserialize_expr(definition->predicate_type(), definition->predicate());
    const auto args = deserialize_expr_vector(definition->args_type(), definition->args());
    const auto values = deserialize_expr_vector(definition->values_type(), definition->values());
    const auto stage_schedule = deserialize_stage_schedule(definition->stage_schedule());
    const std::vector<Specialization> specializations =
        deserialize_vector<Serialize::Specialization, Specialization>(definition->specializations(),
                                                                      &Deserializer::deserialize_specialization);
    const auto source_location = deserialize_string(definition->source_location());
    return Definition(is_init, predicate, args, values, stage_schedule, specializations, source_location);
}

ReductionVariable Deserializer::deserialize_reduction_variable(const Serialize::ReductionVariable *reduction_variable) {
    user_assert(reduction_variable != nullptr);
    const auto var = deserialize_string(reduction_variable->var());
    const auto min = deserialize_expr(reduction_variable->min_type(), reduction_variable->min());
    const auto extent = deserialize_expr(reduction_variable->extent_type(), reduction_variable->extent());
    auto hl_reduction_variable = ReductionVariable();
    hl_reduction_variable.var = var;
    hl_reduction_variable.min = min;
    hl_reduction_variable.extent = extent;
    return hl_reduction_variable;
}

ReductionDomain Deserializer::deserialize_reduction_domain(const Serialize::ReductionDomain *reduction_domain) {
    user_assert(reduction_domain != nullptr);
    const bool defined = reduction_domain->defined();
    if (!defined) {
        return ReductionDomain();
    }
    const std::vector<ReductionVariable> domain =
        deserialize_vector<Serialize::ReductionVariable, ReductionVariable>(reduction_domain->domain(),
                                                                            &Deserializer::deserialize_reduction_variable);
    const auto predicate = deserialize_expr(reduction_domain->predicate_type(), reduction_domain->predicate());
    const auto frozen = reduction_domain->frozen();
    return ReductionDomain(domain, predicate, frozen);
}

ModulusRemainder Deserializer::deserialize_modulus_remainder(const Serialize::ModulusRemainder *modulus_remainder) {
    user_assert(modulus_remainder != nullptr);
    return ModulusRemainder(modulus_remainder->modulus(), modulus_remainder->remainder());
}

PrefetchDirective Deserializer::deserialize_prefetch_directive(const Serialize::PrefetchDirective *prefetch_directive) {
    user_assert(prefetch_directive != nullptr);
    const auto name = deserialize_string(prefetch_directive->name());
    const auto at = deserialize_string(prefetch_directive->at());
    const auto from = deserialize_string(prefetch_directive->from());
    const auto offset = deserialize_expr(prefetch_directive->offset_type(), prefetch_directive->offset());
    const auto strategy = deserialize_prefetch_bound_strategy(prefetch_directive->strategy());
    const auto param_name = deserialize_string(prefetch_directive->param_name());
    Parameter param;
    if (auto it = parameters_in_pipeline.find(param_name); it != parameters_in_pipeline.end()) {
        param = it->second;
    } else if (!param_name.empty()) {
        user_error << "unknown parameter used in pipeline '" << param_name << "'\n";
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
    user_assert(split != nullptr);
    const auto old_var = deserialize_string(split->old_var());
    const auto outer = deserialize_string(split->outer());
    const auto inner = deserialize_string(split->inner());
    const auto factor = deserialize_expr(split->factor_type(), split->factor());
    const auto exact = split->exact();
    const auto tail = deserialize_tail_strategy(split->tail());
    const auto split_type = deserialize_split_type(split->split_type());
    auto hl_split = Split();
    hl_split.old_var = old_var;
    hl_split.outer = outer;
    hl_split.inner = inner;
    hl_split.factor = factor;
    hl_split.exact = exact;
    hl_split.tail = tail;
    hl_split.split_type = split_type;
    return hl_split;
}

Dim Deserializer::deserialize_dim(const Serialize::Dim *dim) {
    user_assert(dim != nullptr);
    const auto var = deserialize_string(dim->var());
    const auto for_type = deserialize_for_type(dim->for_type());
    const auto device_api = deserialize_device_api(dim->device_api());
    const auto dim_type = deserialize_dim_type(dim->dim_type());
    const auto partition_policy = deserialize_partition(dim->partition_policy());
    auto hl_dim = Dim();
    hl_dim.var = var;
    hl_dim.for_type = for_type;
    hl_dim.device_api = device_api;
    hl_dim.dim_type = dim_type;
    hl_dim.partition_policy = partition_policy;
    return hl_dim;
}

FuseLoopLevel Deserializer::deserialize_fuse_loop_level(const Serialize::FuseLoopLevel *fuse_loop_level) {
    user_assert(fuse_loop_level != nullptr);
    const auto fuse_level = deserialize_loop_level(fuse_loop_level->fuse_level());
    const std::vector<std::string> align_dimension_names =
        deserialize_vector<flatbuffers::String, std::string>(fuse_loop_level->align_dimension_names(),
                                                             &Deserializer::deserialize_string);
    std::vector<LoopAlignStrategy> align_strategies;
    align_strategies.reserve(fuse_loop_level->align_strategies()->size());
    for (const auto &align_strategy : *fuse_loop_level->align_strategies()) {
        align_strategies.push_back(deserialize_loop_align_strategy((Serialize::LoopAlignStrategy)align_strategy));
    }
    std::map<std::string, LoopAlignStrategy> align;
    for (size_t i = 0; i < align_dimension_names.size(); ++i) {
        align[align_dimension_names[i]] = align_strategies[i];
    }
    return FuseLoopLevel(fuse_level, align);
}

FusedPair Deserializer::deserialize_fused_pair(const Serialize::FusedPair *fused_pair) {
    user_assert(fused_pair != nullptr);
    const auto func_1 = deserialize_string(fused_pair->func_1());
    const auto func_2 = deserialize_string(fused_pair->func_2());
    const auto var_name = deserialize_string(fused_pair->var_name());
    return FusedPair(func_1, fused_pair->stage_1(), func_2, fused_pair->stage_2(), var_name);
}

StageSchedule Deserializer::deserialize_stage_schedule(const Serialize::StageSchedule *stage_schedule) {
    user_assert(stage_schedule != nullptr);
    const std::vector<ReductionVariable> rvars =
        deserialize_vector<Serialize::ReductionVariable, ReductionVariable>(stage_schedule->rvars(),
                                                                            &Deserializer::deserialize_reduction_variable);
    const std::vector<Split> splits =
        deserialize_vector<Serialize::Split, Split>(stage_schedule->splits(),
                                                    &Deserializer::deserialize_split);
    const std::vector<Dim> dims =
        deserialize_vector<Serialize::Dim, Dim>(stage_schedule->dims(),
                                                &Deserializer::deserialize_dim);
    const std::vector<PrefetchDirective> prefetches =
        deserialize_vector<Serialize::PrefetchDirective, PrefetchDirective>(stage_schedule->prefetches(),
                                                                            &Deserializer::deserialize_prefetch_directive);
    const FuseLoopLevel fuse_level = deserialize_fuse_loop_level(stage_schedule->fuse_level());
    const std::vector<FusedPair> fused_pairs =
        deserialize_vector<Serialize::FusedPair, FusedPair>(stage_schedule->fused_pairs(),
                                                            &Deserializer::deserialize_fused_pair);
    const bool touched = stage_schedule->touched();
    const bool allow_race_conditions = stage_schedule->allow_race_conditions();
    const bool atomic = stage_schedule->atomic();
    const bool override_atomic_associativity_test = stage_schedule->override_atomic_associativity_test();
    return StageSchedule(rvars, splits, dims, prefetches, fuse_level, fused_pairs, touched,
                         allow_race_conditions, atomic, override_atomic_associativity_test);
}

BufferConstraint Deserializer::deserialize_buffer_constraint(const Serialize::BufferConstraint *buffer_constraint) {
    user_assert(buffer_constraint != nullptr);
    const auto min = deserialize_expr(buffer_constraint->min_type(), buffer_constraint->min());
    const auto extent = deserialize_expr(buffer_constraint->extent_type(), buffer_constraint->extent());
    const auto stride = deserialize_expr(buffer_constraint->stride_type(), buffer_constraint->stride());
    const auto min_estimate = deserialize_expr(buffer_constraint->min_estimate_type(), buffer_constraint->min_estimate());
    const auto extent_estimate = deserialize_expr(buffer_constraint->extent_estimate_type(), buffer_constraint->extent_estimate());
    auto hl_buffer_constraint = BufferConstraint();
    hl_buffer_constraint.min = min;
    hl_buffer_constraint.extent = extent;
    hl_buffer_constraint.stride = stride;
    hl_buffer_constraint.min_estimate = min_estimate;
    return hl_buffer_constraint;
}

Parameter Deserializer::deserialize_parameter(const Serialize::Parameter *parameter) {
    user_assert(parameter != nullptr);
    const bool defined = parameter->defined();
    if (!defined) {
        return Parameter();
    }
    const bool is_buffer = parameter->is_buffer();
    const auto type = deserialize_type(parameter->type());
    const int dimensions = parameter->dimensions();
    const std::string name = deserialize_string(parameter->name());
    if (is_buffer) {
        const int host_alignment = parameter->host_alignment();
        std::vector<BufferConstraint> buffer_constraints =
            deserialize_vector<Serialize::BufferConstraint, BufferConstraint>(parameter->buffer_constraints(),
                                                                              &Deserializer::deserialize_buffer_constraint);
        const auto memory_type = deserialize_memory_type(parameter->memory_type());
        return Parameter(type, dimensions, name, Buffer<>(), host_alignment, buffer_constraints, memory_type);
    } else {
        static_assert(FLATBUFFERS_USE_STD_OPTIONAL);
        const auto make_optional_halide_scalar_value_t = [](const std::optional<uint64_t> &v) -> std::optional<halide_scalar_value_t> {
            if (v.has_value()) {
                halide_scalar_value_t scalar_data;
                scalar_data.u.u64 = v.value();
                return std::optional<halide_scalar_value_t>(scalar_data);
            } else {
                return std::nullopt;
            }
        };
        const std::optional<halide_scalar_value_t> scalar_data = make_optional_halide_scalar_value_t(parameter->scalar_data());
        const auto scalar_default = deserialize_expr(parameter->scalar_default_type(), parameter->scalar_default());
        const auto scalar_min = deserialize_expr(parameter->scalar_min_type(), parameter->scalar_min());
        const auto scalar_max = deserialize_expr(parameter->scalar_max_type(), parameter->scalar_max());
        const auto scalar_estimate = deserialize_expr(parameter->scalar_estimate_type(), parameter->scalar_estimate());
        return Parameter(type, dimensions, name, scalar_data, scalar_default, scalar_min, scalar_max, scalar_estimate);
    }
}

Parameter Deserializer::deserialize_external_parameter(const Serialize::ExternalParameter *external_parameter) {
    user_assert(external_parameter != nullptr);
    const bool is_buffer = external_parameter->is_buffer();
    const auto type = deserialize_type(external_parameter->type());
    const int dimensions = external_parameter->dimensions();
    const std::string name = deserialize_string(external_parameter->name());
    return Parameter(type, is_buffer, dimensions, name);
}

ExternFuncArgument Deserializer::deserialize_extern_func_argument(const Serialize::ExternFuncArgument *extern_func_argument) {
    user_assert(extern_func_argument != nullptr);
    const auto arg_type = deserialize_extern_func_argument_type(extern_func_argument->arg_type());
    if (arg_type == ExternFuncArgument::ArgType::UndefinedArg) {
        return ExternFuncArgument();
    } else if (arg_type == ExternFuncArgument::ArgType::FuncArg) {
        const int32_t func_index = extern_func_argument->func_index();
        FunctionPtr func_ptr;
        if (auto it = this->reverse_function_mappings.find(func_index); it != this->reverse_function_mappings.end() && func_index != -1) {
            func_ptr = it->second;
        }
        return ExternFuncArgument(func_ptr);
    } else if (arg_type == ExternFuncArgument::ArgType::BufferArg) {
        const auto buffer_name = deserialize_string(extern_func_argument->buffer_name());
        Buffer<> buffer;
        if (auto it = buffers_in_pipeline.find(buffer_name); it != buffers_in_pipeline.end()) {
            buffer = it->second;
        }
        return ExternFuncArgument(buffer);
    } else if (arg_type == ExternFuncArgument::ArgType::ExprArg) {
        const auto expr = deserialize_expr(extern_func_argument->expr_type(), extern_func_argument->expr());
        return ExternFuncArgument(expr);
    } else {
        const auto image_param_name = deserialize_string(extern_func_argument->image_param_name());
        Parameter image_param;
        if (auto it = user_params.find(image_param_name); it != user_params.end()) {
            image_param = it->second;
        } else if (auto it = external_params.find(image_param_name); it != external_params.end()) {
            image_param = it->second;
        } else if (auto it = parameters_in_pipeline.find(image_param_name); it != parameters_in_pipeline.end()) {
            image_param = it->second;
        } else if (!image_param_name.empty()) {
            user_error << "unknown image parameter used in pipeline '" << image_param_name << "'\n";
        }
        return ExternFuncArgument(image_param);
    }
}

Buffer<> Deserializer::deserialize_buffer(const Serialize::Buffer *buffer) {
    user_assert(buffer != nullptr);
    if (!buffer->defined()) {
        return Buffer<>();
    }
    const std::string name = deserialize_string(buffer->name());
    const auto type = deserialize_type(buffer->type());
    const int32_t dimensions = buffer->dimensions();
    std::vector<halide_dimension_t> hl_buffer_dimensions;
    std::vector<halide_dimension_t> dense_buffer_dimensions;
    hl_buffer_dimensions.reserve(dimensions);
    dense_buffer_dimensions.reserve(dimensions);
    int32_t stride = -1;
    for (int i = 0; i < dimensions; ++i) {
        const auto *dim = buffer->dims()->Get(i);
        halide_dimension_t hl_dim, dense_dim;
        hl_dim.min = dim->min();
        hl_dim.extent = dim->extent();
        hl_dim.stride = dim->stride();
        hl_buffer_dimensions.push_back(hl_dim);
        dense_dim.min = hl_dim.min;
        dense_dim.extent = hl_dim.extent;
        if (i == 0) {
            dense_dim.stride = hl_dim.stride;
            stride = hl_dim.stride * hl_dim.extent;
        } else {
            dense_dim.stride = stride;
            stride *= hl_dim.extent;
        }
        dense_buffer_dimensions.push_back(dense_dim);
    }
    // To handle cropped buffer, we create a dense buffer and serialize into it,
    // then create a (potential sparse) buffer with orignal dimension infos and copy from the dense buffer
    auto fake_dense_buffer = Buffer<>(type, nullptr, dimensions, dense_buffer_dimensions.data(), name + "_dense_fake");
    auto dense_buffer = Buffer<>::make_with_shape_of(fake_dense_buffer, nullptr, nullptr, name + "_dense_tmp");
    memcpy(dense_buffer.data(), buffer->data()->data(), buffer->data()->size());
    auto fake_buffer = Buffer<>(type, nullptr, dimensions, hl_buffer_dimensions.data(), name + "_fake");
    auto hl_buffer = Buffer<>::make_with_shape_of(fake_buffer, nullptr, nullptr, name);
    hl_buffer.copy_from(dense_buffer);
    return hl_buffer;
}

std::map<std::string, FunctionPtr> Deserializer::deserialize_wrapper_refs(const flatbuffers::Vector<flatbuffers::Offset<Serialize::WrapperRef>> *wrappers) {
    user_assert(wrappers != nullptr);
    std::map<std::string, FunctionPtr> result;
    for (const auto &wrapper : *wrappers) {
        const auto name = deserialize_string(wrapper->func_name());
        const int32_t func_index = wrapper->func_index();
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
    int count = 0;
    for (const auto &f : functions) {
        // The reverse function mappings are used in places where only weak references are needed.
        FunctionPtr ptr;
        ptr.strong = nullptr;
        ptr.weak = f.get_contents().group();
        ptr.idx = f.get_contents().idx;
        this->reverse_function_mappings[count++] = ptr;
    }
}

Pipeline Deserializer::deserialize(const std::string &filename) {
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    if (!in) {
        user_error << "failed to open file " << filename << "\n";
        return Pipeline();
    }
    Pipeline result = deserialize(in);
    if (!in.good()) {
        user_error << "failed to deserialize from file " << filename << " properly\n";
        return Pipeline();
    }
    in.close();
    return result;
}

Pipeline Deserializer::deserialize(std::istream &in) {
    if (!in) {
        user_error << "failed to open input stream\n";
        return Pipeline();
    }
    in.seekg(0, std::ios::end);
    int size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    in.read((char *)data.data(), size);
    return deserialize(data);
}

Pipeline Deserializer::deserialize(const std::vector<uint8_t> &data) {
    const auto *pipeline_obj = Serialize::GetPipeline(data.data());
    if (pipeline_obj == nullptr) {
        user_warning << "deserialized pipeline is empty\n";
        return Pipeline();
    }

    std::string deserialized_halide_version = deserialize_string(pipeline_obj->halide_version());
    std::string halide_version = std::to_string(HALIDE_VERSION_MAJOR) + "." +
                                 std::to_string(HALIDE_VERSION_MINOR) + "." +
                                 std::to_string(HALIDE_VERSION_PATCH);
    if (deserialized_halide_version != halide_version) {
        user_warning << "deserialized pipeline is built with Halide version " << deserialized_halide_version
                     << ", but current Halide version is " << halide_version << "\n";
    }

    std::string deserialized_serialization_version = deserialize_string(pipeline_obj->serialization_version());
    std::string serialization_version = std::to_string((int)Serialize::SerializationVersionMajor::Value) + "." +
                                        std::to_string((int)Serialize::SerializationVersionMinor::Value) + "." +
                                        std::to_string((int)Serialize::SerializationVersionPatch::Value);

    if (deserialized_serialization_version != serialization_version) {
        user_error << "deserialized pipeline is built with Halide serialization version " << deserialized_serialization_version
                   << ", but current Halide serialization version is " << serialization_version << "\n";
    }

    const std::vector<std::string> func_names_in_order =
        deserialize_vector<flatbuffers::String, std::string>(pipeline_obj->func_names_in_order(),
                                                             &Deserializer::deserialize_string);

    // We use the first realized function to build the group and all other functions below to this same group
    std::vector<Function> functions;
    functions.reserve(func_names_in_order.size());
    if (!func_names_in_order.empty()) {
        functions.emplace_back(func_names_in_order[0]);
        for (size_t i = 1; i < func_names_in_order.size(); ++i) {
            functions.push_back(functions[0].new_function_in_same_group(func_names_in_order[i]));
        }
    }
    build_reverse_function_mappings(functions);

    // Buffers need to be deserialized first as Parameters may reference them
    const std::vector<Buffer<>> buffers =
        deserialize_vector<Serialize::Buffer, Buffer<>>(pipeline_obj->buffers(),
                                                        &Deserializer::deserialize_buffer);
    for (const auto &buffer : buffers) {
        user_assert(buffers_in_pipeline.count(buffer.name()) == 0) << "duplicate buffer " << buffer.name() << " in pipeline\n";
        buffers_in_pipeline[buffer.name()] = buffer;
    }
    const std::vector<Parameter> parameters =
        deserialize_vector<Serialize::Parameter, Parameter>(pipeline_obj->parameters(),
                                                            &Deserializer::deserialize_parameter);
    for (const auto &param : parameters) {
        user_assert(parameters_in_pipeline.count(param.name()) == 0) << "duplicate parameter " << param.name() << " in pipeline\n";
        parameters_in_pipeline[param.name()] = param;
    }

    const std::vector<Parameter> parameters_external =
        deserialize_vector<Serialize::ExternalParameter, Parameter>(pipeline_obj->external_parameters(),
                                                                    &Deserializer::deserialize_external_parameter);
    for (const auto &param : parameters_external) {
        external_params[param.name()] = param;
    }

    std::vector<Func> funcs;
    for (size_t i = 0; i < pipeline_obj->funcs()->size(); ++i) {
        deserialize_function(pipeline_obj->funcs()->Get(i), functions[i]);
        funcs.emplace_back(functions[i]);
    }

    const std::vector<std::string> output_names =
        deserialize_vector<flatbuffers::String, std::string>(pipeline_obj->output_names(),
                                                             &Deserializer::deserialize_string);
    std::vector<Func> output_funcs;
    for (const auto &output_name : output_names) {
        for (const auto &f : funcs) {
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
    return Pipeline(output_funcs, requirements);
}

std::map<std::string, Parameter> Deserializer::deserialize_parameters(const std::string &filename) {
    std::map<std::string, Parameter> empty;
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    if (!in) {
        user_error << "failed to open file " << filename << "\n";
        return empty;
    }
    std::map<std::string, Parameter> params = deserialize_parameters(in);
    if (!in.good()) {
        user_error << "failed to deserialize from file " << filename << " properly\n";
        return empty;
    }
    in.close();
    return params;
}

std::map<std::string, Parameter> Deserializer::deserialize_parameters(std::istream &in) {
    std::map<std::string, Parameter> empty;
    if (!in) {
        user_error << "failed to open input stream\n";
        return empty;
    }
    in.seekg(0, std::ios::end);
    int size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    in.read((char *)data.data(), size);
    return deserialize_parameters(data);
}

std::map<std::string, Parameter> Deserializer::deserialize_parameters(const std::vector<uint8_t> &data) {
    std::map<std::string, Parameter> external_parameters_by_name;
    const auto *pipeline_obj = Serialize::GetPipeline(data.data());
    if (pipeline_obj == nullptr) {
        user_warning << "deserialized pipeline is empty\n";
        return external_parameters_by_name;
    }

    const std::vector<Parameter> external_parameters =
        deserialize_vector<Serialize::ExternalParameter, Parameter>(pipeline_obj->external_parameters(),
                                                                    &Deserializer::deserialize_external_parameter);

    for (const auto &param : external_parameters) {
        external_parameters_by_name[param.name()] = param;
    }
    return external_parameters_by_name;
}

}  // namespace Internal

Pipeline deserialize_pipeline(const std::string &filename, const std::map<std::string, Parameter> &user_params) {
    Internal::Deserializer deserializer(user_params);
    return deserializer.deserialize(filename);
}

Pipeline deserialize_pipeline(std::istream &in, const std::map<std::string, Parameter> &user_params) {
    Internal::Deserializer deserializer(user_params);
    return deserializer.deserialize(in);
}

Pipeline deserialize_pipeline(const std::vector<uint8_t> &buffer, const std::map<std::string, Parameter> &user_params) {
    Internal::Deserializer deserializer(user_params);
    return deserializer.deserialize(buffer);
}

std::map<std::string, Parameter> deserialize_parameters(const std::string &filename) {
    Internal::Deserializer deserializer;
    return deserializer.deserialize_parameters(filename);
}

std::map<std::string, Parameter> deserialize_parameters(std::istream &in) {
    Internal::Deserializer deserializer;
    return deserializer.deserialize_parameters(in);
}

std::map<std::string, Parameter> deserialize_parameters(const std::vector<uint8_t> &buffer) {
    Internal::Deserializer deserializer;
    return deserializer.deserialize_parameters(buffer);
}

}  // namespace Halide

#else  // WITH_SERIALIZATION

namespace Halide {

Pipeline deserialize_pipeline(const std::string &filename, const std::map<std::string, Parameter> &user_params) {
    user_error << "Deserialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
    return Pipeline();
}

Pipeline deserialize_pipeline(std::istream &in, const std::map<std::string, Parameter> &user_params) {
    user_error << "Deserialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
    return Pipeline();
}

Pipeline deserialize_pipeline(const std::vector<uint8_t> &buffer, const std::map<std::string, Parameter> &user_params) {
    user_error << "Deserialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
    return Pipeline();
}

std::map<std::string, Parameter> deserialize_parameters(const std::string &filename) {
    user_error << "Deserialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
    return {};
}

std::map<std::string, Parameter> deserialize_parameters(std::istream &in) {
    user_error << "Deserialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
    return {};
}

std::map<std::string, Parameter> deserialize_parameters(const std::vector<uint8_t> &buffer) {
    user_error << "Deserialization is not supported in this build of Halide; try rebuilding with WITH_SERIALIZATION=ON.";
    return {};
}

}  // namespace Halide

#endif  // WITH_SERIALIZATION
