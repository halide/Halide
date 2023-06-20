#include "Serializer.h"
#include <fstream>
#include <iostream>
#include <map>

Halide::Serialize::MemoryType Serializer::serialize_memory_type(const Halide::MemoryType &memory_type) {
    switch (memory_type) {
    case Halide::MemoryType::Auto:
        return Halide::Serialize::MemoryType::MemoryType_Auto;
    case Halide::MemoryType::Heap:
        return Halide::Serialize::MemoryType::MemoryType_Heap;
    case Halide::MemoryType::Stack:
        return Halide::Serialize::MemoryType::MemoryType_Stack;
    case Halide::MemoryType::Register:
        return Halide::Serialize::MemoryType::MemoryType_Register;
    case Halide::MemoryType::GPUShared:
        return Halide::Serialize::MemoryType::MemoryType_GPUShared;
    case Halide::MemoryType::GPUTexture:
        return Halide::Serialize::MemoryType::MemoryType_GPUTexture;
    case Halide::MemoryType::LockedCache:
        return Halide::Serialize::MemoryType::MemoryType_LockedCache;
    case Halide::MemoryType::VTCM:
        return Halide::Serialize::MemoryType::MemoryType_VTCM;
    case Halide::MemoryType::AMXTile:
        return Halide::Serialize::MemoryType::MemoryType_AMXTile;
    default:
        std::cerr << "Unsupported memory type\n";
        exit(1);
    }
}

Halide::Serialize::ForType Serializer::serialize_for_type(const Halide::Internal::ForType &for_type) {
    switch (for_type) {
    case Halide::Internal::ForType::Serial:
        return Halide::Serialize::ForType::ForType_Serial;
    case Halide::Internal::ForType::Parallel:
        return Halide::Serialize::ForType::ForType_Parallel;
    case Halide::Internal::ForType::Vectorized:
        return Halide::Serialize::ForType::ForType_Vectorized;
    case Halide::Internal::ForType::Unrolled:
        return Halide::Serialize::ForType::ForType_Unrolled;
    case Halide::Internal::ForType::Extern:
        return Halide::Serialize::ForType::ForType_Extern;
    case Halide::Internal::ForType::GPUBlock:
        return Halide::Serialize::ForType::ForType_GPUBlock;
    case Halide::Internal::ForType::GPUThread:
        return Halide::Serialize::ForType::ForType_GPUThread;
    case Halide::Internal::ForType::GPULane:
        return Halide::Serialize::ForType::ForType_GPULane;
    default:
        std::cerr << "Unsupported for type\n";
        exit(1);
    }
}

Halide::Serialize::DeviceAPI Serializer::serialize_device_api(const Halide::DeviceAPI &device_api) {
    switch (device_api) {
    case Halide::DeviceAPI::None:
        return Halide::Serialize::DeviceAPI::DeviceAPI_None;
    case Halide::DeviceAPI::Host:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Host;
    case Halide::DeviceAPI::Default_GPU:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Default_GPU;
    case Halide::DeviceAPI::CUDA:
        return Halide::Serialize::DeviceAPI::DeviceAPI_CUDA;
    case Halide::DeviceAPI::OpenCL:
        return Halide::Serialize::DeviceAPI::DeviceAPI_OpenCL;
    case Halide::DeviceAPI::OpenGLCompute:
        return Halide::Serialize::DeviceAPI::DeviceAPI_OpenGLCompute;
    case Halide::DeviceAPI::Metal:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Metal;
    case Halide::DeviceAPI::Hexagon:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Hexagon;
    case Halide::DeviceAPI::HexagonDma:
        return Halide::Serialize::DeviceAPI::DeviceAPI_HexagonDma;
    case Halide::DeviceAPI::D3D12Compute:
        return Halide::Serialize::DeviceAPI::DeviceAPI_D3D12Compute;
    case Halide::DeviceAPI::Vulkan:
        return Halide::Serialize::DeviceAPI::DeviceAPI_Vulkan;
    case Halide::DeviceAPI::WebGPU:
        return Halide::Serialize::DeviceAPI::DeviceAPI_WebGPU;
    default:
        std::cerr << "Unsupported device API\n";
        exit(1);
    }
}

Halide::Serialize::CallType Serializer::serialize_call_type(const Halide::Internal::Call::CallType &call_type) {
    switch (call_type) {
    case Halide::Internal::Call::CallType::Image:
        return Halide::Serialize::CallType::CallType_Image;
    case Halide::Internal::Call::CallType::Extern:
        return Halide::Serialize::CallType::CallType_Extern;
    case Halide::Internal::Call::CallType::ExternCPlusPlus:
        return Halide::Serialize::CallType::CallType_ExternCPlusPlus;
    case Halide::Internal::Call::CallType::PureExtern:
        return Halide::Serialize::CallType::CallType_PureExtern;
    case Halide::Internal::Call::CallType::Halide:
        return Halide::Serialize::CallType::CallType_Halide;
    case Halide::Internal::Call::CallType::Intrinsic:
        return Halide::Serialize::CallType::CallType_Intrinsic;
    case Halide::Internal::Call::CallType::PureIntrinsic:
        return Halide::Serialize::CallType::CallType_PureIntrinsic;
    default:
        std::cerr << "Unsupported call type\n";
        exit(1);
    }
}

Halide::Serialize::VectorReduceOp Serializer::serialize_vector_reduce_op(const Halide::Internal::VectorReduce::Operator &vector_reduce_op) {
    switch (vector_reduce_op) {
    case Halide::Internal::VectorReduce::Operator::Add:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Add;
    case Halide::Internal::VectorReduce::Operator::SaturatingAdd:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_SaturatingAdd;
    case Halide::Internal::VectorReduce::Operator::Mul:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Mul;
    case Halide::Internal::VectorReduce::Operator::Min:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Min;
    case Halide::Internal::VectorReduce::Operator::Max:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Max;
    case Halide::Internal::VectorReduce::Operator::And:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_And;
    case Halide::Internal::VectorReduce::Operator::Or:
        return Halide::Serialize::VectorReduceOp::VectorReduceOp_Or;
    default:
        std::cerr << "Unsupported vector reduce op\n";
        exit(1);
    }
}

Halide::Serialize::PrefetchBoundStrategy Serializer::serialize_prefetch_bound_strategy(const Halide::PrefetchBoundStrategy &prefetch_bound_strategy) {
    switch (prefetch_bound_strategy) {
    case Halide::PrefetchBoundStrategy::Clamp:
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp;
    case Halide::PrefetchBoundStrategy::GuardWithIf:
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_GuardWithIf;
    case Halide::PrefetchBoundStrategy::NonFaulting:
        return Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_NonFaulting;
    default:
        std::cerr << "Unsupported prefetch bound strategy\n";
        exit(1);
    }
}

Halide::Serialize::NameMangling Serializer::serialize_name_mangling(const Halide::NameMangling &name_mangling) {
    switch (name_mangling) {
    case Halide::NameMangling::Default:
        return Halide::Serialize::NameMangling::NameMangling_Default;
    case Halide::NameMangling::C:
        return Halide::Serialize::NameMangling::NameMangling_C;
    case Halide::NameMangling::CPlusPlus:
        return Halide::Serialize::NameMangling::NameMangling_CPlusPlus;
    default:
        std::cerr << "Unsupported name mangling\n";
        exit(1);
    }
}

flatbuffers::Offset<flatbuffers::String> Serializer::serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str) {
    return builder.CreateString(str);
}

flatbuffers::Offset<Halide::Serialize::Type> Serializer::serialize_type(flatbuffers::FlatBufferBuilder &builder, const Halide::Type &type) {
    int bits = type.bits();
    int lanes = type.lanes();
    halide_type_code_t code = type.code();
    auto code_serialized = Halide::Serialize::TypeCode(code);
    auto type_obj = Halide::Serialize::CreateType(builder, code_serialized, bits, lanes);
    return type_obj;
}

std::pair<Halide::Serialize::Stmt, flatbuffers::Offset<void>> Serializer::serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Stmt &stmt) {
    if (!stmt.defined()) {
        return std::make_pair(Halide::Serialize::Stmt::Stmt_UndefinedStmt, Halide::Serialize::CreateUndefinedStmt(builder).Union());
    }
    switch (stmt->node_type) {
    case Halide::Internal::IRNodeType::LetStmt: {
        auto let_stmt = stmt.as<Halide::Internal::LetStmt>();
        auto name_serialized = serialize_string(builder, let_stmt->name);
        auto value_serialized = serialize_expr(builder, let_stmt->value);
        auto body_serialized = serialize_stmt(builder, let_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_LetStmt, Halide::Serialize::CreateLetStmt(builder, name_serialized, value_serialized.first, value_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::AssertStmt: {
        auto assert_stmt = stmt.as<Halide::Internal::AssertStmt>();
        auto condition_serialized = serialize_expr(builder, assert_stmt->condition);
        auto message_serialized = serialize_expr(builder, assert_stmt->message);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_AssertStmt, Halide::Serialize::CreateAssertStmt(builder, condition_serialized.first, condition_serialized.second, message_serialized.first, message_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::ProducerConsumer: {
        auto producer_consumer = stmt.as<Halide::Internal::ProducerConsumer>();
        auto name_serialized = serialize_string(builder, producer_consumer->name);
        auto body_serialized = serialize_stmt(builder, producer_consumer->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_ProducerConsumer, Halide::Serialize::CreateProducerConsumer(builder, name_serialized, producer_consumer->is_producer, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::For: {
        auto for_stmt = stmt.as<Halide::Internal::For>();
        auto name_serialized = serialize_string(builder, for_stmt->name);
        auto min_serialized = serialize_expr(builder, for_stmt->min);
        auto extent_serialized = serialize_expr(builder, for_stmt->extent);
        Halide::Serialize::ForType for_type = serialize_for_type(for_stmt->for_type);
        Halide::Serialize::DeviceAPI device_api = serialize_device_api(for_stmt->device_api);
        auto body_serialized = serialize_stmt(builder, for_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_For, Halide::Serialize::CreateFor(builder, name_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second, for_type, device_api, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Store: {
        auto store_stmt = stmt.as<Halide::Internal::Store>();
        auto name_serialized = serialize_string(builder, store_stmt->name);
        auto predicate_serialized = serialize_expr(builder, store_stmt->predicate);
        auto value_serialized = serialize_expr(builder, store_stmt->value);
        auto index_serialized = serialize_expr(builder, store_stmt->index);
        auto alignment_serialized = serialize_modulus_remainder(builder, store_stmt->alignment);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Store, Halide::Serialize::CreateStore(builder, name_serialized, predicate_serialized.first, predicate_serialized.second, value_serialized.first, value_serialized.second, index_serialized.first, index_serialized.second, alignment_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Provide: {
        auto provide_stmt = stmt.as<Halide::Internal::Provide>();
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
    case Halide::Internal::IRNodeType::Allocate: {
        auto allocate_stmt = stmt.as<Halide::Internal::Allocate>();
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
    case Halide::Internal::IRNodeType::Free: {
        auto free_stmt = stmt.as<Halide::Internal::Free>();
        auto name_serialized = serialize_string(builder, free_stmt->name);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Free, Halide::Serialize::CreateFree(builder, name_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Realize: {
        auto realize_stmt = stmt.as<Halide::Internal::Realize>();
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
    case Halide::Internal::IRNodeType::Block: {
        auto block_stmt = stmt.as<Halide::Internal::Block>();
        auto first_serialized = serialize_stmt(builder, block_stmt->first);
        auto rest_serialized = serialize_stmt(builder, block_stmt->rest);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Block, Halide::Serialize::CreateBlock(builder, first_serialized.first, first_serialized.second, rest_serialized.first, rest_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::IfThenElse: {
        auto if_then_else_stmt = stmt.as<Halide::Internal::IfThenElse>();
        auto condition_serialized = serialize_expr(builder, if_then_else_stmt->condition);
        auto then_case_serialized = serialize_stmt(builder, if_then_else_stmt->then_case);
        auto else_case_serialized = serialize_stmt(builder, if_then_else_stmt->else_case);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_IfThenElse, Halide::Serialize::CreateIfThenElse(builder, condition_serialized.first, condition_serialized.second, then_case_serialized.first, then_case_serialized.second, else_case_serialized.first, else_case_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Evaluate: {
        auto evaluate_stmt = stmt.as<Halide::Internal::Evaluate>();
        auto value_serialized = serialize_expr(builder, evaluate_stmt->value);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Evaluate, Halide::Serialize::CreateEvaluate(builder, value_serialized.first, value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Prefetch: {
        auto prefetch_stmt = stmt.as<Halide::Internal::Prefetch>();
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
    case Halide::Internal::IRNodeType::Acquire: {
        auto acquire_stmt = stmt.as<Halide::Internal::Acquire>();
        auto semaphore_serialized = serialize_expr(builder, acquire_stmt->semaphore);
        auto count_serialized = serialize_expr(builder, acquire_stmt->count);
        auto body_serialized = serialize_stmt(builder, acquire_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Acquire, Halide::Serialize::CreateAcquire(builder, semaphore_serialized.first, semaphore_serialized.second, count_serialized.first, count_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Fork: {
        auto fork_stmt = stmt.as<Halide::Internal::Fork>();
        auto first_serialized = serialize_stmt(builder, fork_stmt->first);
        auto rest_serialized = serialize_stmt(builder, fork_stmt->rest);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Fork, Halide::Serialize::CreateFork(builder, first_serialized.first, first_serialized.second, rest_serialized.first, rest_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Atomic: {
        auto atomic_stmt = stmt.as<Halide::Internal::Atomic>();
        auto producer_name_serialized = serialize_string(builder, atomic_stmt->producer_name);
        auto mutex_name_serialized = serialize_string(builder, atomic_stmt->mutex_name);
        auto body_serialized = serialize_stmt(builder, atomic_stmt->body);
        return std::make_pair(Halide::Serialize::Stmt::Stmt_Atomic, Halide::Serialize::CreateAtomic(builder, producer_name_serialized, mutex_name_serialized, body_serialized.first, body_serialized.second).Union());
    }
    default:
        std::cerr << "Unsupported stmt type\n";
        exit(1);
    }
}

std::pair<Halide::Serialize::Expr, flatbuffers::Offset<void>> Serializer::serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Halide::Expr &expr) {
    if (!expr.defined()) {
        return std::make_pair(Halide::Serialize::Expr::Expr_UndefinedExpr, Halide::Serialize::CreateUndefinedExpr(builder).Union());
    }
    switch (expr->node_type) {
    case Halide::Internal::IRNodeType::IntImm: {
        auto int_imm = expr.as<Halide::Internal::IntImm>();
        return std::make_pair(Halide::Serialize::Expr::Expr_IntImm, Halide::Serialize::CreateIntImm(builder, int_imm->value).Union());
    }
    case Halide::Internal::IRNodeType::UIntImm: {
        auto uint_imm = expr.as<Halide::Internal::UIntImm>();
        return std::make_pair(Halide::Serialize::Expr::Expr_UIntImm, Halide::Serialize::CreateUIntImm(builder, uint_imm->value).Union());
    }
    case Halide::Internal::IRNodeType::FloatImm: {
        auto float_imm = expr.as<Halide::Internal::FloatImm>();
        return std::make_pair(Halide::Serialize::Expr::Expr_FloatImm, Halide::Serialize::CreateFloatImm(builder, float_imm->value).Union());
    }
    case Halide::Internal::IRNodeType::StringImm: {
        auto string_imm = expr.as<Halide::Internal::StringImm>();
        auto value_serialized = serialize_string(builder, string_imm->value);
        return std::make_pair(Halide::Serialize::Expr::Expr_StringImm, Halide::Serialize::CreateStringImm(builder, value_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Cast: {
        auto cast_expr = expr.as<Halide::Internal::Cast>();
        auto value_serialized = serialize_expr(builder, cast_expr->value);
        return std::make_pair(Halide::Serialize::Expr::Expr_Cast, Halide::Serialize::CreateCast(builder, value_serialized.first, value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Reinterpret: {
        auto reinterpret_expr = expr.as<Halide::Internal::Reinterpret>();
        auto value_serialized = serialize_expr(builder, reinterpret_expr->value);
        return std::make_pair(Halide::Serialize::Expr::Expr_Reinterpret, Halide::Serialize::CreateReinterpret(builder, value_serialized.first, value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Add: {
        auto add_expr = expr.as<Halide::Internal::Add>();
        auto a_serialized = serialize_expr(builder, add_expr->a);
        auto b_serialized = serialize_expr(builder, add_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Add, Halide::Serialize::CreateAdd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Sub: {
        auto sub_expr = expr.as<Halide::Internal::Sub>();
        auto a_serialized = serialize_expr(builder, sub_expr->a);
        auto b_serialized = serialize_expr(builder, sub_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Sub, Halide::Serialize::CreateSub(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Mul: {
        auto mul_expr = expr.as<Halide::Internal::Mul>();
        auto a_serialized = serialize_expr(builder, mul_expr->a);
        auto b_serialized = serialize_expr(builder, mul_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Mul, Halide::Serialize::CreateMul(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Div: {
        auto div_expr = expr.as<Halide::Internal::Div>();
        auto a_serialized = serialize_expr(builder, div_expr->a);
        auto b_serialized = serialize_expr(builder, div_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Div, Halide::Serialize::CreateDiv(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Mod: {
        auto mod_expr = expr.as<Halide::Internal::Mod>();
        auto a_serialized = serialize_expr(builder, mod_expr->a);
        auto b_serialized = serialize_expr(builder, mod_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Mod, Halide::Serialize::CreateMod(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Min: {
        auto min_expr = expr.as<Halide::Internal::Min>();
        auto a_serialized = serialize_expr(builder, min_expr->a);
        auto b_serialized = serialize_expr(builder, min_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Min, Halide::Serialize::CreateMin(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Max: {
        auto max_expr = expr.as<Halide::Internal::Max>();
        auto a_serialized = serialize_expr(builder, max_expr->a);
        auto b_serialized = serialize_expr(builder, max_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Max, Halide::Serialize::CreateMax(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::EQ: {
        auto eq_expr = expr.as<Halide::Internal::EQ>();
        auto a = eq_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = eq_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serialize::Expr::Expr_EQ, Halide::Serialize::CreateEQ(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::NE: {
        auto ne_expr = expr.as<Halide::Internal::NE>();
        auto a = ne_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = ne_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serialize::Expr::Expr_NE, Halide::Serialize::CreateNE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::LT: {
        auto lt_expr = expr.as<Halide::Internal::LT>();
        auto a_serialized = serialize_expr(builder, lt_expr->a);
        auto b_serialized = serialize_expr(builder, lt_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_LT, Halide::Serialize::CreateLT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::LE: {
        auto le_expr = expr.as<Halide::Internal::LE>();
        auto a_serialized = serialize_expr(builder, le_expr->a);
        auto b_serialized = serialize_expr(builder, le_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_LE, Halide::Serialize::CreateLE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::GT: {
        auto gt_expr = expr.as<Halide::Internal::GT>();
        auto a_serialized = serialize_expr(builder, gt_expr->a);
        auto b_serialized = serialize_expr(builder, gt_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_GT, Halide::Serialize::CreateGT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::GE: {
        auto ge_expr = expr.as<Halide::Internal::GE>();
        auto a_serialized = serialize_expr(builder, ge_expr->a);
        auto b_serialized = serialize_expr(builder, ge_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_GE, Halide::Serialize::CreateGE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::And: {
        auto and_expr = expr.as<Halide::Internal::And>();
        auto a_serialized = serialize_expr(builder, and_expr->a);
        auto b_serialized = serialize_expr(builder, and_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_And, Halide::Serialize::CreateAnd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Or: {
        auto or_expr = expr.as<Halide::Internal::Or>();
        auto a_serialized = serialize_expr(builder, or_expr->a);
        auto b_serialized = serialize_expr(builder, or_expr->b);
        return std::make_pair(Halide::Serialize::Expr::Expr_Or, Halide::Serialize::CreateOr(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Not: {
        auto not_expr = expr.as<Halide::Internal::Not>();
        auto a_serialized = serialize_expr(builder, not_expr->a);
        return std::make_pair(Halide::Serialize::Expr::Expr_Not, Halide::Serialize::CreateNot(builder, a_serialized.first, a_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Select: {
        auto select_expr = expr.as<Halide::Internal::Select>();
        auto condition_serialized = serialize_expr(builder, select_expr->condition);
        auto true_value_serialized = serialize_expr(builder, select_expr->true_value);
        auto false_value_serialized = serialize_expr(builder, select_expr->false_value);
        return std::make_pair(Halide::Serialize::Expr::Expr_Select, Halide::Serialize::CreateSelect(builder, condition_serialized.first, condition_serialized.second, true_value_serialized.first, true_value_serialized.second, false_value_serialized.first, false_value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Load: {
        auto load_expr = expr.as<Halide::Internal::Load>();
        auto name_serialized = serialize_string(builder, load_expr->name);
        auto predicate_serialized = serialize_expr(builder, load_expr->predicate);
        auto index_serialized = serialize_expr(builder, load_expr->index);
        auto alignment_serialized = serialize_modulus_remainder(builder, load_expr->alignment);
        return std::make_pair(Halide::Serialize::Expr::Expr_Load, Halide::Serialize::CreateLoad(builder, name_serialized, predicate_serialized.first, predicate_serialized.second, index_serialized.first, index_serialized.second, alignment_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Ramp: {
        auto ramp_expr = expr.as<Halide::Internal::Ramp>();
        auto base_serialized = serialize_expr(builder, ramp_expr->base);
        auto stride_serialized = serialize_expr(builder, ramp_expr->stride);
        auto lanes = ramp_expr->lanes;
        return std::make_pair(Halide::Serialize::Expr::Expr_Ramp, Halide::Serialize::CreateRamp(builder, base_serialized.first, base_serialized.second, stride_serialized.first, stride_serialized.second, lanes).Union());
    }
    case Halide::Internal::IRNodeType::Broadcast: {
        auto broadcast_expr = expr.as<Halide::Internal::Broadcast>();
        auto value_serialized = serialize_expr(builder, broadcast_expr->value);
        auto lanes = broadcast_expr->lanes;
        return std::make_pair(Halide::Serialize::Expr::Expr_Broadcast, Halide::Serialize::CreateBroadcast(builder, value_serialized.first, value_serialized.second, lanes).Union());
    }
    case Halide::Internal::IRNodeType::Let: {
        auto let_expr = expr.as<Halide::Internal::Let>();
        auto name_serialized = serialize_string(builder, let_expr->name);
        auto value_serialized = serialize_expr(builder, let_expr->value);
        auto body_serialized = serialize_expr(builder, let_expr->body);
        return std::make_pair(Halide::Serialize::Expr::Expr_Let, Halide::Serialize::CreateLet(builder, name_serialized, value_serialized.first, value_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Call: {
        auto call_expr = expr.as<Halide::Internal::Call>();
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
        auto value_index = call_expr->value_index;
        return std::make_pair(Halide::Serialize::Expr::Expr_Call, Halide::Serialize::CreateCall(builder, name_serialized, builder.CreateVector(args_types), builder.CreateVector(args_serialized), call_type, value_index).Union());
    }
    case Halide::Internal::IRNodeType::Variable: {
        auto variable_expr = expr.as<Halide::Internal::Variable>();
        auto name_serialized = serialize_string(builder, variable_expr->name);
        auto reduction_domain_serialized = serialize_reduction_domain(builder, variable_expr->reduction_domain);
        return std::make_pair(Halide::Serialize::Expr::Expr_Variable, Halide::Serialize::CreateVariable(builder, name_serialized, reduction_domain_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Shuffle: {
        auto shuffle_expr = expr.as<Halide::Internal::Shuffle>();
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
    case Halide::Internal::IRNodeType::VectorReduce: {
        auto vector_reduce_expr = expr.as<Halide::Internal::VectorReduce>();
        auto value_serialized = serialize_expr(builder, vector_reduce_expr->value);
        auto reduction_op_serialized = serialize_vector_reduce_op(vector_reduce_expr->op);
        return std::make_pair(Halide::Serialize::Expr::Expr_VectorReduce, Halide::Serialize::CreateVectorReduce(builder, value_serialized.first, value_serialized.second, reduction_op_serialized).Union());
    }
    default:
        std::cerr << "Unsupported Expr type\n";
        exit(1);
    }
}

flatbuffers::Offset<Halide::Serialize::Func> Serializer::serialize_function(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Function &function) {
    auto name_serialized = serialize_string(builder, function.name());

    auto origin_name_serialized = serialize_string(builder, function.origin_name());

    std::vector<Halide::Type> output_types = function.output_types();
    std::vector<flatbuffers::Offset<Halide::Serialize::Type>> output_types_serialized;
    output_types_serialized.reserve(output_types.size());
    for (const auto &type : output_types) {
        output_types_serialized.push_back(serialize_type(builder, type));
    }
    auto output_types_vector = builder.CreateVector(output_types_serialized);

    std::vector<Halide::Type> required_types = function.required_types();
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
    auto extern_function_name_serialized = serialize_string(builder, function.extern_function_name());
    auto extern_mangling_serialized = serialize_name_mangling(function.extern_definition_name_mangling());
    auto extern_function_device_api_serialized = serialize_device_api(function.extern_function_device_api());
    auto extern_proxy_expr_serialized = serialize_expr(builder, function.extern_definition_proxy_expr());
    bool trace_loads = function.is_tracing_loads();
    bool trace_stores = function.is_tracing_stores();
    bool trace_realizations = function.is_tracing_realizations();
    std::vector<flatbuffers::Offset<flatbuffers::String>> trace_tags_serialized;
    trace_tags_serialized.reserve(function.get_trace_tags().size());
    for (const auto& tag: function.get_trace_tags()) {
        trace_tags_serialized.push_back(serialize_string(builder, tag));
    }
    bool frozen = function.frozen();
    auto func = Halide::Serialize::CreateFunc(builder, name_serialized, origin_name_serialized, output_types_vector, required_types_vector, required_dim, 
                                              args_vector, func_schedule_serialized, init_def_serialized, builder.CreateVector(updates_serialized), debug_file_serialized,
                                              extern_function_name_serialized, extern_mangling_serialized, extern_function_device_api_serialized, extern_proxy_expr_serialized.first,
                                              extern_proxy_expr_serialized.second, trace_loads, trace_stores, trace_realizations, builder.CreateVector(trace_tags_serialized), frozen);
    return func;
}

flatbuffers::Offset<Halide::Serialize::Range> Serializer::serialize_range(flatbuffers::FlatBufferBuilder &builder, const Halide::Range &range) {
    auto min = range.min;
    auto min_serialized = serialize_expr(builder, min);
    auto extent = range.extent;
    auto extent_serialized = serialize_expr(builder, extent);
    auto range_obj = Halide::Serialize::CreateRange(builder, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second);
    return range_obj;
}

flatbuffers::Offset<Halide::Serialize::Bound> Serializer::serialize_bound(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Bound &bound) {
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

flatbuffers::Offset<Halide::Serialize::StorageDim> Serializer::serialize_storage_dim(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::StorageDim &storage_dim) {
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

flatbuffers::Offset<Halide::Serialize::LoopLevel> Serializer::serialize_loop_level(flatbuffers::FlatBufferBuilder &builder, const Halide::LoopLevel &loop_level) {
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

flatbuffers::Offset<Halide::Serialize::FuncSchedule> Serializer::serialize_func_schedule(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::FuncSchedule &func_schedule) {
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
    // auto wrappers_serialized = serialize_wrapper_refs(builder, func_schedule.wrappers());
    Halide::Serialize::MemoryType memory_type = serialize_memory_type(func_schedule.memory_type());
    auto memoized = func_schedule.memoized();
    auto async = func_schedule.async();
    auto memoize_eviction_key = func_schedule.memoize_eviction_key();
    auto memoize_eviction_key_serialized = serialize_expr(builder, memoize_eviction_key);
    return Halide::Serialize::CreateFuncSchedule(builder, store_level_serialized, compute_level_serialized, builder.CreateVector(storage_dims_serialized), builder.CreateVector(bounds_serialized),
                                                 builder.CreateVector(estimates_serialized), memory_type, memoized, async, memoize_eviction_key_serialized.first, memoize_eviction_key_serialized.second);
}

flatbuffers::Offset<Halide::Serialize::Specialization> Serializer::serialize_specialization(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Specialization &specialization) {
    auto condition_serialized = serialize_expr(builder, specialization.condition);
    auto definition_serialized = serialize_definition(builder, specialization.definition);
    auto failure_message_serialized = serialize_string(builder, specialization.failure_message);
    return Halide::Serialize::CreateSpecialization(builder, condition_serialized.first, condition_serialized.second, definition_serialized, failure_message_serialized);
}

flatbuffers::Offset<Halide::Serialize::Definition> Serializer::serialize_definition(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Definition &definition) {
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
    std::vector<flatbuffers::Offset<Halide::Serialize::Specialization>> specializations_serialized;
    for (const auto &specialization : definition.specializations()) {
        specializations_serialized.push_back(serialize_specialization(builder, specialization));
    }
    auto source_location_serialized = serialize_string(builder, definition.source_location());
    return Halide::Serialize::CreateDefinition(builder, is_init, predicate_serialized.first, predicate_serialized.second, builder.CreateVector(values_types),
                                               builder.CreateVector(values_serialized), builder.CreateVector(args_types), builder.CreateVector(args_serialized), builder.CreateVector(specializations_serialized), source_location_serialized);
}

flatbuffers::Offset<Halide::Serialize::ReductionVariable> Serializer::serialize_reduction_variable(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::ReductionVariable &reduction_variable) {
    auto var_serialized = serialize_string(builder, reduction_variable.var);
    auto min_serialized = serialize_expr(builder, reduction_variable.min);
    auto extent_serialized = serialize_expr(builder, reduction_variable.extent);
    return Halide::Serialize::CreateReductionVariable(builder, var_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second);
}

flatbuffers::Offset<Halide::Serialize::ReductionDomain> Serializer::serialize_reduction_domain(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::ReductionDomain &reduction_domain) {
    std::vector<flatbuffers::Offset<Halide::Serialize::ReductionVariable>> domain_serialized;
    for (const auto &reduction_variable : reduction_domain.domain()) {
        domain_serialized.push_back(serialize_reduction_variable(builder, reduction_variable));
    }
    auto predicate_serialized = serialize_expr(builder, reduction_domain.predicate());
    return Halide::Serialize::CreateReductionDomain(builder, builder.CreateVector(domain_serialized), predicate_serialized.first, predicate_serialized.second, reduction_domain.frozen());
}

flatbuffers::Offset<Halide::Serialize::ModulusRemainder> Serializer::serialize_modulus_remainder(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::ModulusRemainder &modulus_remainder) {
    return Halide::Serialize::CreateModulusRemainder(builder, modulus_remainder.modulus, modulus_remainder.remainder);
}

flatbuffers::Offset<Halide::Serialize::PrefetchDirective> Serializer::serialize_prefetch_directive(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::PrefetchDirective &prefetch_directive) {
    auto name_serialized = serialize_string(builder, prefetch_directive.name);
    auto at_serialized = serialize_string(builder, prefetch_directive.at);
    auto from_serialized = serialize_string(builder, prefetch_directive.from);
    auto offset_serialized = serialize_expr(builder, prefetch_directive.offset);
    auto strategy_serialized = serialize_prefetch_bound_strategy(prefetch_directive.strategy);
    return Halide::Serialize::CreatePrefetchDirective(builder, name_serialized, at_serialized, from_serialized, offset_serialized.first, offset_serialized.second, strategy_serialized);
}

// std::vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> Serializer::serialize_wrapper_refs(flatbuffers::FlatBufferBuilder &builder, const std::map<std::string, Halide::Internal::FunctionPtr> &wrappers) {
//     // instead of storing the function pointer or raw function address,
//     // we store a pre-computed function index as the serialized format for WrapperRef
//     std::vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> wrapper_refs_serialized;
//     for (const auto& it : wrappers) {
//         std::string name = it.first;
//         const Halide::Internal::FunctionPtr& func_ptr = it.second;
//         // TODO: is `name` and `Function(it.second).name()` the same thing?
//         if (auto fm_it = this->func_mappings.find(Halide::Internal::Function(it.second).name()); it != this->func_mappings.end()) {
//             int32_t func_idx = fm_it->second;
//             auto name_serialized = serialize_string(builder, name);
//             wrapper_refs_serialized.push_back(Halide::Serialize::CreateWrapperRef(builder, name_serialized, func_idx));
//         } else {
//             std::cerr << "func " << name << " not found in func_mappings\n";
//             exit(1);
//         }
//     }
//     return wrapper_refs_serialized;
// }

// std::vector<flatbuffers::Offset<Halide::Serialize::FuncMapping>> Serializer::serialize_func_mappings(flatbuffers::FlatBufferBuilder &builder, const std::map<std::string, int32_t> &func_mappings) {
//     std::vector<flatbuffers::Offset<Halide::Serialize::FuncMapping>> func_mappings_serialized;
//     for (const auto& it : func_mappings) {
//         std::string name = it.first;
//         int32_t index = it.second;
//         auto name_serialized = serialize_string(builder, name);
//         func_mappings_serialized.push_back(Halide::Serialize::CreateFuncMapping(builder, name_serialized, index));
//     }
//     return func_mappings_serialized;
// }

void Serializer::serialize(const Halide::Pipeline &pipeline, const std::string &filename) {
    std::cout << "Serializing a pipeline into " << filename << "\n";
    flatbuffers::FlatBufferBuilder builder(1024);
    std::map<std::string, Halide::Internal::Function> env;
    std::map<std::string, int32_t> func_mappings;

    // extract the DAG, unwarp function from Funcs
    for (const Halide::Func &func : pipeline.outputs()) {
        const Halide::Internal::Function &f = func.function();
        std::map<std::string, Halide::Internal::Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    // construct the internal func mapping that will be used
    // through serialization/deserialization to reassamble the DAG
    // {
    //     int32_t i = 0;
    //     for (const auto& it: env) {
    //         func_mappings[it.first] = i++;
    //     }
    //     this->func_mappings = func_mappings;
    // }

    // serialize each func
    // TODO: this should be the correct way to serialize the whole DAG
    //       a vector of all funcs + an extra map to map from name to index
    //       but for now let me skip this
    // std::vector<flatbuffers::Offset<Halide::Serialize::Func>> func_vector;
    // for (auto it = env.begin(); it != env.end(); ++it) {
    //     func_vector.push_back(this->serialize_func(builder, it->second));
    // }
    // auto funcs = builder.CreateVector(func_vector);

    auto outpus = pipeline.outputs();
    std::vector<flatbuffers::Offset<Halide::Serialize::Func>> funcs_serialized;
    for (const auto &func : outpus) {
        funcs_serialized.push_back(this->serialize_function(builder, func.function()));
    }
    auto funcs = builder.CreateVector(funcs_serialized);

    // requirements
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

    // auto func_mappings_serialized = serialize_func_mappings(builder, func_mappings);

    auto pipeline_obj = Halide::Serialize::CreatePipeline(builder, funcs, requirements_types_vector, requirements_vector);
    builder.Finish(pipeline_obj);

    // write the binary file
    uint8_t *buf = builder.GetBufferPointer();
    int size = builder.GetSize();
    std::ofstream out(filename, std::ios::out | std::ios::binary);
    if (!out) {
        std::cerr << "failed to open file " << filename << "\n";
        return;
    }
    out.write((char *)(buf), size);
    out.close();
}
