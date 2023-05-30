#include "Serializer.h"
#include <fstream>
#include <iostream>
#include <map>

using Halide::Internal::Function;

flatbuffers::Offset<flatbuffers::String> Serializer::serialize_string(flatbuffers::FlatBufferBuilder &builder, const std::string &str) {
    return builder.CreateString(str);
}

flatbuffers::Offset<Halide::Serdes::Type> Serializer::serialize_type(flatbuffers::FlatBufferBuilder &builder, const Halide::Type &type) {
    // bits
    int bits = type.bits();

    // lanes
    int lanes = type.lanes();

    // code
    halide_type_code_t code = type.code();

    auto code_serialized = Halide::Serdes::TypeCode(code);

    auto type_obj = Halide::Serdes::CreateType(builder, code_serialized, bits, lanes);
    return type_obj;
}

std::pair<Halide::Serdes::Stmt, flatbuffers::Offset<void>> Serializer::serialize_stmt(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Stmt &stmt) {
    switch (stmt->node_type) {
    case Halide::Internal::IRNodeType::LetStmt: {
        auto let_stmt = stmt.as<Halide::Internal::LetStmt>();
        std::string name = let_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto value = let_stmt->value;
        auto value_serialized = serialize_expr(builder, value);
        auto body = let_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_LetStmt, Halide::Serdes::CreateLetStmt(builder, name_serialized, value_serialized.first, value_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::AssertStmt: {
        auto assert_stmt = stmt.as<Halide::Internal::AssertStmt>();
        auto condition = assert_stmt->condition;
        auto condition_serialized = serialize_expr(builder, condition);
        auto message = assert_stmt->message;
        auto message_serialized = serialize_expr(builder, message);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_AssertStmt, Halide::Serdes::CreateAssertStmt(builder, condition_serialized.first, condition_serialized.second, message_serialized.first, message_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::ProducerConsumer: {
        auto producer_consumer = stmt.as<Halide::Internal::ProducerConsumer>();
        std::string name = producer_consumer->name;
        auto name_serialized = serialize_string(builder, name);
        bool is_producer = producer_consumer->is_producer;
        auto body = producer_consumer->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_ProducerConsumer, Halide::Serdes::CreateProducerConsumer(builder, name_serialized, is_producer, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::For: {
        auto for_stmt = stmt.as<Halide::Internal::For>();
        std::string name = for_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto min = for_stmt->min;
        auto min_serialized = serialize_expr(builder, min);
        auto extent = for_stmt->extent;
        auto extent_serialized = serialize_expr(builder, extent);
        Halide::Serdes::ForType for_type = Halide::Serdes::ForType::ForType_Serial;
        switch (for_stmt->for_type) {
        case Halide::Internal::ForType::Serial: {
            for_type = Halide::Serdes::ForType::ForType_Serial;
            break;
        }
        case Halide::Internal::ForType::Parallel: {
            for_type = Halide::Serdes::ForType::ForType_Parallel;
            break;
        }
        case Halide::Internal::ForType::Vectorized: {
            for_type = Halide::Serdes::ForType::ForType_Vectorized;
            break;
        }
        case Halide::Internal::ForType::Unrolled: {
            for_type = Halide::Serdes::ForType::ForType_Unrolled;
            break;
        }
        case Halide::Internal::ForType::Extern: {
            for_type = Halide::Serdes::ForType::ForType_Extern;
            break;
        }
        case Halide::Internal::ForType::GPUBlock: {
            for_type = Halide::Serdes::ForType::ForType_GPUBlock;
            break;
        }
        case Halide::Internal::ForType::GPUThread: {
            for_type = Halide::Serdes::ForType::ForType_GPUThread;
            break;
        }
        case Halide::Internal::ForType::GPULane: {
            for_type = Halide::Serdes::ForType::ForType_GPULane;
            break;
        }
        }
        Halide::Serdes::DeviceAPI device_api = Halide::Serdes::DeviceAPI::DeviceAPI_None;
        switch (for_stmt->device_api) {
        case Halide::DeviceAPI::None: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_None;
            break;
        }
        case Halide::DeviceAPI::Host: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_Host;
            break;
        }
        case Halide::DeviceAPI::Default_GPU: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_Default_GPU;
            break;
        }
        case Halide::DeviceAPI::CUDA: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_CUDA;
            break;
        }
        case Halide::DeviceAPI::OpenCL: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_OpenCL;
            break;
        }
        case Halide::DeviceAPI::OpenGLCompute: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_OpenGLCompute;
            break;
        }
        case Halide::DeviceAPI::Metal: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_Metal;
            break;
        }
        case Halide::DeviceAPI::Hexagon: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_Hexagon;
            break;
        }
        case Halide::DeviceAPI::HexagonDma: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_HexagonDma;
            break;
        }
        case Halide::DeviceAPI::D3D12Compute: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_D3D12Compute;
            break;
        }
        case Halide::DeviceAPI::Vulkan: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_Vulkan;
            break;
        }
        case Halide::DeviceAPI::WebGPU: {
            device_api = Halide::Serdes::DeviceAPI::DeviceAPI_WebGPU;
            break;
        }
        }
        auto body = for_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_For, Halide::Serdes::CreateFor(builder, name_serialized, min_serialized.first, min_serialized.second, extent_serialized.first, extent_serialized.second, for_type, device_api, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Store: {
        auto store_stmt = stmt.as<Halide::Internal::Store>();
        std::string name = store_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto predicate = store_stmt->predicate;
        auto predicate_serialized = serialize_expr(builder, predicate);
        auto value = store_stmt->value;
        auto value_serialized = serialize_expr(builder, value);
        auto index = store_stmt->index;
        auto index_serialized = serialize_expr(builder, index);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Store, Halide::Serdes::CreateStore(builder, name_serialized, predicate_serialized.first, predicate_serialized.second, value_serialized.first, value_serialized.second, index_serialized.first, index_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Provide: {
        auto provide_stmt = stmt.as<Halide::Internal::Provide>();
        std::string name = provide_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto values = provide_stmt->values;
        std::vector<uint8_t> values_types;
        std::vector<flatbuffers::Offset<void>> values_serialized;
        for (const auto &value : values) {
            auto value_serialized = serialize_expr(builder, value);
            values_types.push_back(value_serialized.first);
            values_serialized.push_back(value_serialized.second);
        }
        auto args = provide_stmt->args;
        std::vector<uint8_t> args_types;
        std::vector<flatbuffers::Offset<void>> args_serialized;
        for (const auto &arg : args) {
            auto arg_serialized = serialize_expr(builder, arg);
            args_types.push_back(arg_serialized.first);
            args_serialized.push_back(arg_serialized.second);
        }
        auto predicate = provide_stmt->predicate;
        auto predicate_serialized = serialize_expr(builder, predicate);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Provide, Halide::Serdes::CreateProvide(builder, name_serialized, builder.CreateVector(values_types), builder.CreateVector(values_serialized), builder.CreateVector(args_types), builder.CreateVector(args_serialized), predicate_serialized.first, predicate_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Allocate: {
        auto allocate_stmt = stmt.as<Halide::Internal::Allocate>();
        std::string name = allocate_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto type = allocate_stmt->type;
        auto type_serialized = serialize_type(builder, type);
        Halide::Serdes::MemoryType memory_type = Halide::Serdes::MemoryType::MemoryType_Auto;
        switch (allocate_stmt->memory_type) {
        case Halide::MemoryType::Auto: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Auto;
            break;
        }
        case Halide::MemoryType::Heap: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Heap;
            break;
        }
        case Halide::MemoryType::Stack: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Stack;
            break;
        }
        case Halide::MemoryType::Register: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Register;
            break;
        }
        case Halide::MemoryType::GPUShared: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_GPUShared;
            break;
        }
        case Halide::MemoryType::GPUTexture: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_GPUTexture;
            break;
        }
        case Halide::MemoryType::LockedCache: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_LockedCache;
            break;
        }
        case Halide::MemoryType::VTCM: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_VTCM;
            break;
        }
        case Halide::MemoryType::AMXTile: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_AMXTile;
            break;
        }
        }
        auto extents = allocate_stmt->extents;
        std::vector<uint8_t> extents_types;
        std::vector<flatbuffers::Offset<void>> extents_serialized;
        for (const auto &extent : extents) {
            auto extent_serialized = serialize_expr(builder, extent);
            extents_types.push_back(extent_serialized.first);
            extents_serialized.push_back(extent_serialized.second);
        }
        auto condition = allocate_stmt->condition;
        auto condition_serialized = serialize_expr(builder, condition);
        auto new_expr = allocate_stmt->new_expr;
        auto new_expr_serialized = serialize_expr(builder, new_expr);
        auto free_function = allocate_stmt->free_function;
        auto free_function_serialized = serialize_string(builder, free_function);
        auto padding = allocate_stmt->padding;
        auto body = allocate_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Allocate, Halide::Serdes::CreateAllocate(builder, name_serialized, type_serialized, memory_type, builder.CreateVector(extents_types), builder.CreateVector(extents_serialized), condition_serialized.first, condition_serialized.second, new_expr_serialized.first, new_expr_serialized.second, free_function_serialized, padding, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Free: {
        auto free_stmt = stmt.as<Halide::Internal::Free>();
        std::string name = free_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Free, Halide::Serdes::CreateFree(builder, name_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Realize: {
        auto realize_stmt = stmt.as<Halide::Internal::Realize>();
        std::string name = realize_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto types = realize_stmt->types;
        std::vector<flatbuffers::Offset<Halide::Serdes::Type>> types_serialized;
        for (const auto &type : types) {
            types_serialized.push_back(serialize_type(builder, type));
        }
        // TODO: make this a func
        Halide::Serdes::MemoryType memory_type = Halide::Serdes::MemoryType::MemoryType_Auto;
        switch (realize_stmt->memory_type) {
        case Halide::MemoryType::Auto: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Auto;
            break;
        }
        case Halide::MemoryType::Heap: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Heap;
            break;
        }
        case Halide::MemoryType::Stack: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Stack;
            break;
        }
        case Halide::MemoryType::Register: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_Register;
            break;
        }
        case Halide::MemoryType::GPUShared: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_GPUShared;
            break;
        }
        case Halide::MemoryType::GPUTexture: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_GPUTexture;
            break;
        }
        case Halide::MemoryType::LockedCache: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_LockedCache;
            break;
        }
        case Halide::MemoryType::VTCM: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_VTCM;
            break;
        }
        case Halide::MemoryType::AMXTile: {
            memory_type = Halide::Serdes::MemoryType::MemoryType_AMXTile;
            break;
        }
        }
        auto types_vector = builder.CreateVector(types_serialized);
        auto condition = realize_stmt->condition;
        auto condition_serialized = serialize_expr(builder, condition);
        auto body = realize_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Realize, Halide::Serdes::CreateRealize(builder, name_serialized, types_vector, memory_type, condition_serialized.first, condition_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Block: {
        auto block_stmt = stmt.as<Halide::Internal::Block>();
        auto first = block_stmt->first;
        auto first_serialized = serialize_stmt(builder, first);
        auto rest = block_stmt->rest;
        auto rest_serialized = serialize_stmt(builder, rest);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Block, Halide::Serdes::CreateBlock(builder, first_serialized.first, first_serialized.second, rest_serialized.first, rest_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::IfThenElse: {
        auto if_then_else_stmt = stmt.as<Halide::Internal::IfThenElse>();
        auto condition = if_then_else_stmt->condition;
        auto condition_serialized = serialize_expr(builder, condition);
        auto then_case = if_then_else_stmt->then_case;
        auto then_case_serialized = serialize_stmt(builder, then_case);
        auto else_case = if_then_else_stmt->else_case;
        auto else_case_serialized = serialize_stmt(builder, else_case);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_IfThenElse, Halide::Serdes::CreateIfThenElse(builder, condition_serialized.first, condition_serialized.second, then_case_serialized.first, then_case_serialized.second, else_case_serialized.first, else_case_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Evaluate: {
        auto evaluate_stmt = stmt.as<Halide::Internal::Evaluate>();
        auto value = evaluate_stmt->value;
        auto value_serialized = serialize_expr(builder, value);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Evaluate, Halide::Serdes::CreateEvaluate(builder, value_serialized.first, value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Prefetch: {
        auto prefetch_stmt = stmt.as<Halide::Internal::Prefetch>();
        std::string name = prefetch_stmt->name;
        auto name_serialized = serialize_string(builder, name);
        auto types = prefetch_stmt->types;
        std::vector<flatbuffers::Offset<Halide::Serdes::Type>> types_serialized;
        for (const auto &type : types) {
            types_serialized.push_back(serialize_type(builder, type));
        }
        auto types_vector = builder.CreateVector(types_serialized);
        auto condition = prefetch_stmt->condition;
        auto condition_serialized = serialize_expr(builder, condition);
        auto body = prefetch_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Prefetch, Halide::Serdes::CreatePrefetch(builder, name_serialized, types_vector, condition_serialized.first, condition_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Acquire: {
        auto acquire_stmt = stmt.as<Halide::Internal::Acquire>();
        auto semaphore = acquire_stmt->semaphore;
        auto semaphore_serialized = serialize_expr(builder, semaphore);
        auto count = acquire_stmt->count;
        auto count_serialized = serialize_expr(builder, count);
        auto body = acquire_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Acquire, Halide::Serdes::CreateAcquire(builder, semaphore_serialized.first, semaphore_serialized.second, count_serialized.first, count_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Fork: {
        auto fork_stmt = stmt.as<Halide::Internal::Fork>();
        auto first = fork_stmt->first;
        auto first_serialized = serialize_stmt(builder, first);
        auto rest = fork_stmt->rest;
        auto rest_serialized = serialize_stmt(builder, rest);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Fork, Halide::Serdes::CreateFork(builder, first_serialized.first, first_serialized.second, rest_serialized.first, rest_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Atomic: {
        auto atomic_stmt = stmt.as<Halide::Internal::Atomic>();
        auto producer_name = atomic_stmt->producer_name;
        auto producer_name_serialized = serialize_string(builder, producer_name);
        auto mutex_name = atomic_stmt->mutex_name;
        auto mutex_name_serialized = serialize_string(builder, mutex_name);
        auto body = atomic_stmt->body;
        auto body_serialized = serialize_stmt(builder, body);
        return std::make_pair(Halide::Serdes::Stmt::Stmt_Atomic, Halide::Serdes::CreateAtomic(builder, producer_name_serialized, mutex_name_serialized, body_serialized.first, body_serialized.second).Union());
    }
    default:
        std::cerr << "Unsupported stmt type\n";
        exit(1);
    }
}

std::pair<Halide::Serdes::Expr, flatbuffers::Offset<void>> Serializer::serialize_expr(flatbuffers::FlatBufferBuilder &builder, const Halide::Expr &expr) {
    switch (expr->node_type) {
    case Halide::Internal::IRNodeType::IntImm: {
        auto int_imm = expr.as<Halide::Internal::IntImm>();
        int64_t value = int_imm->value;
        return std::make_pair(Halide::Serdes::Expr::Expr_IntImm, Halide::Serdes::CreateIntImm(builder, value).Union());
    }
    case Halide::Internal::IRNodeType::UIntImm: {
        auto uint_imm = expr.as<Halide::Internal::UIntImm>();
        uint64_t value = uint_imm->value;
        return std::make_pair(Halide::Serdes::Expr::Expr_UIntImm, Halide::Serdes::CreateUIntImm(builder, value).Union());
    }
    case Halide::Internal::IRNodeType::FloatImm: {
        auto float_imm = expr.as<Halide::Internal::FloatImm>();
        double value = float_imm->value;
        return std::make_pair(Halide::Serdes::Expr::Expr_FloatImm, Halide::Serdes::CreateFloatImm(builder, value).Union());
    }
    case Halide::Internal::IRNodeType::StringImm: {
        auto string_imm = expr.as<Halide::Internal::StringImm>();
        std::string value = string_imm->value;
        auto value_serialized = serialize_string(builder, value);
        return std::make_pair(Halide::Serdes::Expr::Expr_StringImm, Halide::Serdes::CreateStringImm(builder, value_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Cast: {
        auto cast_expr = expr.as<Halide::Internal::Cast>();
        auto value = cast_expr->value;
        auto value_serialized = serialize_expr(builder, value);
        return std::make_pair(Halide::Serdes::Expr::Expr_Cast, Halide::Serdes::CreateCast(builder, value_serialized.first, value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Reinterpret: {
        auto reinterpret_expr = expr.as<Halide::Internal::Reinterpret>();
        auto value = reinterpret_expr->value;
        auto value_serialized = serialize_expr(builder, value);
        return std::make_pair(Halide::Serdes::Expr::Expr_Reinterpret, Halide::Serdes::CreateReinterpret(builder, value_serialized.first, value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Add: {
        auto add_expr = expr.as<Halide::Internal::Add>();
        auto a = add_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = add_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Add, Halide::Serdes::CreateAdd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Sub: {
        auto sub_expr = expr.as<Halide::Internal::Sub>();
        auto a = sub_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = sub_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Sub, Halide::Serdes::CreateSub(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Mul: {
        auto mul_expr = expr.as<Halide::Internal::Mul>();
        auto a = mul_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = mul_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Mul, Halide::Serdes::CreateMul(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Div: {
        auto div_expr = expr.as<Halide::Internal::Div>();
        auto a = div_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = div_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Div, Halide::Serdes::CreateDiv(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Mod: {
        auto mod_expr = expr.as<Halide::Internal::Mod>();
        auto a = mod_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = mod_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Mod, Halide::Serdes::CreateMod(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Min: {
        auto min_expr = expr.as<Halide::Internal::Min>();
        auto a = min_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = min_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Min, Halide::Serdes::CreateMin(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Max: {
        auto max_expr = expr.as<Halide::Internal::Max>();
        auto a = max_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = max_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Max, Halide::Serdes::CreateMax(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::EQ: {
        auto eq_expr = expr.as<Halide::Internal::EQ>();
        auto a = eq_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = eq_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_EQ, Halide::Serdes::CreateEQ(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::NE: {
        auto ne_expr = expr.as<Halide::Internal::NE>();
        auto a = ne_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = ne_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_NE, Halide::Serdes::CreateNE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::LT: {
        auto lt_expr = expr.as<Halide::Internal::LT>();
        auto a = lt_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = lt_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_LT, Halide::Serdes::CreateLT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::LE: {
        auto le_expr = expr.as<Halide::Internal::LE>();
        auto a = le_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = le_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_LE, Halide::Serdes::CreateLE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::GT: {
        auto gt_expr = expr.as<Halide::Internal::GT>();
        auto a = gt_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = gt_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_GT, Halide::Serdes::CreateGT(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::GE: {
        auto ge_expr = expr.as<Halide::Internal::GE>();
        auto a = ge_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = ge_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_GE, Halide::Serdes::CreateGE(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::And: {
        auto and_expr = expr.as<Halide::Internal::And>();
        auto a = and_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = and_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_And, Halide::Serdes::CreateAnd(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Or: {
        auto or_expr = expr.as<Halide::Internal::Or>();
        auto a = or_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        auto b = or_expr->b;
        auto b_serialized = serialize_expr(builder, b);
        return std::make_pair(Halide::Serdes::Expr::Expr_Or, Halide::Serdes::CreateOr(builder, a_serialized.first, a_serialized.second, b_serialized.first, b_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Not: {
        auto not_expr = expr.as<Halide::Internal::Not>();
        auto a = not_expr->a;
        auto a_serialized = serialize_expr(builder, a);
        return std::make_pair(Halide::Serdes::Expr::Expr_Not, Halide::Serdes::CreateNot(builder, a_serialized.first, a_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Select: {
        auto select_expr = expr.as<Halide::Internal::Select>();
        auto condition = select_expr->condition;
        auto condition_serialized = serialize_expr(builder, condition);
        auto true_value = select_expr->true_value;
        auto true_value_serialized = serialize_expr(builder, true_value);
        auto false_value = select_expr->false_value;
        auto false_value_serialized = serialize_expr(builder, false_value);
        return std::make_pair(Halide::Serdes::Expr::Expr_Select, Halide::Serdes::CreateSelect(builder, condition_serialized.first, condition_serialized.second, true_value_serialized.first, true_value_serialized.second, false_value_serialized.first, false_value_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Load: {
        auto load_expr = expr.as<Halide::Internal::Load>();
        std::string name = load_expr->name;
        auto name_serialized = serialize_string(builder, name);
        auto predicate = load_expr->predicate;
        auto predicate_serialized = serialize_expr(builder, predicate);
        auto index = load_expr->index;
        auto index_serialized = serialize_expr(builder, index);
        return std::make_pair(Halide::Serdes::Expr::Expr_Load, Halide::Serdes::CreateLoad(builder, name_serialized, predicate_serialized.first, predicate_serialized.second, index_serialized.first, index_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Ramp: {
        auto ramp_expr = expr.as<Halide::Internal::Ramp>();
        auto base = ramp_expr->base;
        auto base_serialized = serialize_expr(builder, base);
        auto stride = ramp_expr->stride;
        auto stride_serialized = serialize_expr(builder, stride);
        auto lanes = ramp_expr->lanes;
        return std::make_pair(Halide::Serdes::Expr::Expr_Ramp, Halide::Serdes::CreateRamp(builder, base_serialized.first, base_serialized.second, stride_serialized.first, stride_serialized.second, lanes).Union());
    }
    case Halide::Internal::IRNodeType::Broadcast: {
        auto broadcast_expr = expr.as<Halide::Internal::Broadcast>();
        auto value = broadcast_expr->value;
        auto value_serialized = serialize_expr(builder, value);
        auto lanes = broadcast_expr->lanes;
        return std::make_pair(Halide::Serdes::Expr::Expr_Broadcast, Halide::Serdes::CreateBroadcast(builder, value_serialized.first, value_serialized.second, lanes).Union());
    }
    case Halide::Internal::IRNodeType::Let: {
        auto let_expr = expr.as<Halide::Internal::Let>();
        std::string name = let_expr->name;
        auto name_serialized = serialize_string(builder, name);
        auto value = let_expr->value;
        auto value_serialized = serialize_expr(builder, value);
        auto body = let_expr->body;
        auto body_serialized = serialize_expr(builder, body);
        return std::make_pair(Halide::Serdes::Expr::Expr_Let, Halide::Serdes::CreateLet(builder, name_serialized, value_serialized.first, value_serialized.second, body_serialized.first, body_serialized.second).Union());
    }
    case Halide::Internal::IRNodeType::Call: {
        auto call_expr = expr.as<Halide::Internal::Call>();
        std::string name = call_expr->name;
        auto name_serialized = serialize_string(builder, name);
        auto args = call_expr->args;
        std::vector<uint8_t> args_types;
        std::vector<flatbuffers::Offset<void>> args_serialized;
        for (const auto &arg : args) {
            auto arg_serialized = serialize_expr(builder, arg);
            args_types.push_back(arg_serialized.first);
            args_serialized.push_back(arg_serialized.second);
        }
        auto value_index = call_expr->value_index;
        return std::make_pair(Halide::Serdes::Expr::Expr_Call, Halide::Serdes::CreateCall(builder, name_serialized, builder.CreateVector(args_types), builder.CreateVector(args_serialized), value_index).Union());
    }
    case Halide::Internal::IRNodeType::Variable: {
        auto variable_expr = expr.as<Halide::Internal::Variable>();
        std::string name = variable_expr->name;
        auto name_serialized = serialize_string(builder, name);
        return std::make_pair(Halide::Serdes::Expr::Expr_Variable, Halide::Serdes::CreateVariable(builder, name_serialized).Union());
    }
    case Halide::Internal::IRNodeType::Shuffle: {
        auto shuffle_expr = expr.as<Halide::Internal::Shuffle>();
        auto vectors = shuffle_expr->vectors;
        std::vector<uint8_t> vectors_types;
        std::vector<flatbuffers::Offset<void>> vectors_serialized;
        for (const auto &vector : vectors) {
            auto vector_serialized = serialize_expr(builder, vector);
            vectors_types.push_back(vector_serialized.first);
            vectors_serialized.push_back(vector_serialized.second);
        }
        auto indices = shuffle_expr->indices;
        return std::make_pair(Halide::Serdes::Expr::Expr_Shuffle, Halide::Serdes::CreateShuffle(builder, builder.CreateVector(vectors_types), builder.CreateVector(vectors_serialized), builder.CreateVector(indices)).Union());
    }
    case Halide::Internal::IRNodeType::VectorReduce: {
        auto vector_reduce_expr = expr.as<Halide::Internal::VectorReduce>();
        auto value = vector_reduce_expr->value;
        auto value_serialized = serialize_expr(builder, value);
        return std::make_pair(Halide::Serdes::Expr::Expr_VectorReduce, Halide::Serdes::CreateVectorReduce(builder, value_serialized.first, value_serialized.second).Union());
    }
    default:
        std::cerr << "Unsupported Expr type\n";
        exit(1);
    }
}

flatbuffers::Offset<Halide::Serdes::Func> Serializer::serialize_func(flatbuffers::FlatBufferBuilder &builder, const Halide::Internal::Function &function) {
    // name
    auto name_serialized = serialize_string(builder, function.name());

    // origin_name
    auto origin_name_serialized = serialize_string(builder, function.origin_name());

    // output_types
    std::vector<Halide::Type> output_types = function.output_types();
    std::vector<flatbuffers::Offset<Halide::Serdes::Type>> output_types_serialized;
    for (const auto &type : output_types) {
        output_types_serialized.push_back(serialize_type(builder, type));
    }
    auto output_types_vector = builder.CreateVector(output_types_serialized);

    // required_types
    std::vector<Halide::Type> required_types = function.required_types();
    std::vector<flatbuffers::Offset<Halide::Serdes::Type>> required_types_serialized;
    for (const auto &type : required_types) {
        required_types_serialized.push_back(serialize_type(builder, type));
    }
    auto required_types_vector = builder.CreateVector(required_types_serialized);

    // required_dimensions
    int required_dim = function.required_dimensions();

    // args
    std::vector<std::string> args = function.args();
    std::vector<flatbuffers::Offset<flatbuffers::String>> args_serialized;
    for (const auto &arg : args) {
        args_serialized.push_back(serialize_string(builder, arg));
    }
    auto args_vector = builder.CreateVector(args_serialized);

    auto func = Halide::Serdes::CreateFunc(builder, name_serialized, origin_name_serialized, output_types_vector, required_types_vector, required_dim, args_vector);
    return func;
}

void Serializer::serialize(const Pipeline &pipeline, const std::string &filename) {
    std::cout << "Serializing a pipeline into " << filename << "\n";
    flatbuffers::FlatBufferBuilder builder(1024);
    std::map<std::string, Function> env;

    // extract the DAG, unwarp function from Funcs
    for (const Func &func : pipeline.outputs()) {
        const Function &f = func.function();
        std::map<std::string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    // serialize each func
    // TODO: this should be the correct way to serialize the whole DAG
    //       a vector of all funcs + an extra map to map from name to index
    //       but for now let me skip this
    // std::vector<flatbuffers::Offset<Halide::Serdes::Func>> func_vector;
    // for (auto it = env.begin(); it != env.end(); ++it) {
    //     func_vector.push_back(this->serialize_func(builder, it->second));
    // }
    // auto funcs = builder.CreateVector(func_vector);

    auto outpus = pipeline.outputs();
    std::vector<flatbuffers::Offset<Halide::Serdes::Func>> funcs_serialized;
    for (const auto &func : outpus) {
        funcs_serialized.push_back(this->serialize_func(builder, func.function()));
    }
    auto funcs = builder.CreateVector(funcs_serialized);

    // requirements
    auto requirements = pipeline.requirements();
    std::vector<flatbuffers::Offset<void>> requirements_serialized;
    std::vector<uint8_t> requirements_types;
    for (const auto &stmt : requirements) {
        auto stmt_serialized = serialize_stmt(builder, stmt);
        requirements_serialized.push_back(stmt_serialized.second);
        requirements_types.push_back(stmt_serialized.first);
    }
    auto requirements_vector = builder.CreateVector(requirements_serialized);
    auto requirements_types_vector = builder.CreateVector(requirements_types);

    auto pipeline_obj = Halide::Serdes::CreatePipeline(builder, funcs, requirements_types_vector, requirements_vector);
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
