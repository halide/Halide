#include "Deserializer.h"
#include <fstream>
#include <iostream>

std::string Deserializer::deserialize_string(const flatbuffers::String *str) {
    return str->str();
}

Halide::Type Deserializer::deserialize_type(const Halide::Serdes::Type *type) {
    // bits
    int bits = type->bits();

    // lanes
    int lanes = type->lanes();

    // code
    Halide::Serdes::TypeCode code_deserialized = type->code();

    halide_type_code_t code = halide_type_uint;
    switch (code_deserialized) {
    case Halide::Serdes::TypeCode::TypeCode_Int:
        code = halide_type_int;
        break;
    case Halide::Serdes::TypeCode::TypeCode_UInt:
        code = halide_type_uint;
        break;
    case Halide::Serdes::TypeCode::TypeCode_Float:
        code = halide_type_float;
        break;
    case Halide::Serdes::TypeCode::TypeCode_Handle:
        code = halide_type_handle;
        break;
    case Halide::Serdes::TypeCode::TypeCode_BFloat:
        code = halide_type_bfloat;
        break;
    }

    return Halide::Type(code, bits, lanes);
}

Halide::Internal::Function Deserializer::deserialize_function(const Halide::Serdes::Func *function) {
    // name
    std::string name = deserialize_string(function->name());

    // origin_name
    std::string origin_name = deserialize_string(function->origin_name());

    // output_types
    std::vector<Halide::Type> output_types;
    auto output_types_serialized = function->output_types();
    for (const auto &type : *output_types_serialized) {
        output_types.push_back(deserialize_type(type));
    }

    // required_types
    std::vector<Halide::Type> required_types;
    auto required_types_serialized = function->required_types();
    for (const auto &type : *required_types_serialized) {
        required_types.push_back(deserialize_type(type));
    }

    // required_dimensions
    int required_dim = function->required_dims();

    // args
    std::vector<std::string> args;
    auto args_serialized = function->args();
    for (const auto &arg : *args_serialized) {
        args.push_back(deserialize_string(arg));
    }

    // assemble the function
    return Halide::Internal::Function(name, origin_name, output_types, required_types, required_dim, args);
}

Halide::Internal::Stmt Deserializer::deserialize_stmt(uint8_t type_code, const void *stmt) {
    switch (type_code) {
    case Halide::Serdes::Stmt_LetStmt: {
        const Halide::Serdes::LetStmt *let_stmt = (const Halide::Serdes::LetStmt *)stmt;
        auto name = deserialize_string(let_stmt->name());
        auto value = deserialize_expr(let_stmt->value_type(), let_stmt->value());
        auto body = deserialize_stmt(let_stmt->body_type(), let_stmt->body());
        return Halide::Internal::LetStmt::make(name, value, body);
    }
    case Halide::Serdes::Stmt_AssertStmt: {
        const Halide::Serdes::AssertStmt *assert_stmt = (const Halide::Serdes::AssertStmt *)stmt;
        auto condition = deserialize_expr(assert_stmt->condition_type(), assert_stmt->condition());
        auto message = deserialize_expr(assert_stmt->message_type(), assert_stmt->message());
        return Halide::Internal::AssertStmt::make(condition, message);
    }
    case Halide::Serdes::Stmt_ProducerConsumer: {
        const Halide::Serdes::ProducerConsumer *producer_consumer = (const Halide::Serdes::ProducerConsumer *)stmt;
        auto name = deserialize_string(producer_consumer->name());
        auto is_producer = producer_consumer->is_producer();
        auto body = deserialize_stmt(producer_consumer->body_type(), producer_consumer->body());
        return Halide::Internal::ProducerConsumer::make(name, is_producer, body);
    }
    case Halide::Serdes::Stmt_For: {
        const Halide::Serdes::For *for_stmt = (const Halide::Serdes::For *)stmt;
        auto name = deserialize_string(for_stmt->name());
        auto min = deserialize_expr(for_stmt->min_type(), for_stmt->min());
        auto extent = deserialize_expr(for_stmt->extent_type(), for_stmt->extent());
        Halide::Internal::ForType for_type = Halide::Internal::ForType::Serial;
        switch (for_stmt->for_type()) {
        case Halide::Serdes::ForType::ForType_Serial:
            for_type = Halide::Internal::ForType::Serial;
            break;
        case Halide::Serdes::ForType::ForType_Parallel:
            for_type = Halide::Internal::ForType::Parallel;
            break;
        case Halide::Serdes::ForType::ForType_Vectorized:
            for_type = Halide::Internal::ForType::Vectorized;
            break;
        case Halide::Serdes::ForType::ForType_Unrolled:
            for_type = Halide::Internal::ForType::Unrolled;
            break;
        case Halide::Serdes::ForType::ForType_Extern:
            for_type = Halide::Internal::ForType::Extern;
            break;
        case Halide::Serdes::ForType::ForType_GPUBlock:
            for_type = Halide::Internal::ForType::GPUBlock;
            break;
        case Halide::Serdes::ForType::ForType_GPUThread:
            for_type = Halide::Internal::ForType::GPUThread;
            break;
        case Halide::Serdes::ForType::ForType_GPULane:
            for_type = Halide::Internal::ForType::GPULane;
            break;
        }
        Halide::DeviceAPI device_api = Halide::DeviceAPI::None;
        switch (for_stmt->device_api()) {
        case Halide::Serdes::DeviceAPI::DeviceAPI_None:
            device_api = Halide::DeviceAPI::None;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_Host:
            device_api = Halide::DeviceAPI::Host;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_Default_GPU:
            device_api = Halide::DeviceAPI::Default_GPU;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_CUDA:
            device_api = Halide::DeviceAPI::CUDA;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_OpenCL:
            device_api = Halide::DeviceAPI::OpenCL;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_OpenGLCompute:
            device_api = Halide::DeviceAPI::OpenGLCompute;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_Metal:
            device_api = Halide::DeviceAPI::Metal;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_Hexagon:
            device_api = Halide::DeviceAPI::Hexagon;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_HexagonDma:
            device_api = Halide::DeviceAPI::HexagonDma;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_D3D12Compute:
            device_api = Halide::DeviceAPI::D3D12Compute;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_Vulkan:
            device_api = Halide::DeviceAPI::Vulkan;
            break;
        case Halide::Serdes::DeviceAPI::DeviceAPI_WebGPU:
            device_api = Halide::DeviceAPI::WebGPU;
            break;
        }
        auto body = deserialize_stmt(for_stmt->body_type(), for_stmt->body());
        return Halide::Internal::For::make(name, min, extent, for_type, device_api, body);
    }
    case Halide::Serdes::Stmt_Store: {
        const Halide::Serdes::Store *store_stmt = (const Halide::Serdes::Store *)stmt;
        auto name = deserialize_string(store_stmt->name());
        auto predicate = deserialize_expr(store_stmt->predicate_type(), store_stmt->predicate());
        auto value = deserialize_expr(store_stmt->value_type(), store_stmt->value());
        auto index = deserialize_expr(store_stmt->index_type(), store_stmt->index());
        return Halide::Internal::Store::make(name, value, index, Halide::Internal::Parameter(), predicate, Halide::Internal::ModulusRemainder());
    }
    case Halide::Serdes::Stmt_Provide: {
        const Halide::Serdes::Provide *provide_stmt = (const Halide::Serdes::Provide *)stmt;
        auto name = deserialize_string(provide_stmt->name());
        std::vector<Expr> values;
        auto values_serialized = provide_stmt->values();
        auto values_types = provide_stmt->values_type();
        for (size_t i = 0; i < values_serialized->size(); ++i) {
            auto value = deserialize_expr(values_types->Get(i), values_serialized->Get(i));
            values.push_back(value);
        }
        std::vector<Expr> args;
        auto args_serialized = provide_stmt->args();
        auto args_types = provide_stmt->args_type();
        for (size_t i = 0; i < args_serialized->size(); ++i) {
            auto arg = deserialize_expr(args_types->Get(i), args_serialized->Get(i));
            args.push_back(arg);
        }
        auto predicate = deserialize_expr(provide_stmt->predicate_type(), provide_stmt->predicate());
        return Halide::Internal::Provide::make(name, values, args, predicate);
    }
    case Halide::Serdes::Stmt_Allocate: {
        const Halide::Serdes::Allocate *allocate_stmt = (const Halide::Serdes::Allocate *)stmt;
        auto name = deserialize_string(allocate_stmt->name());
        auto type = deserialize_type(allocate_stmt->type());
        Halide::MemoryType memory_type = Halide::MemoryType::Auto;
        switch (allocate_stmt->memory_type()) {
        case Halide::Serdes::MemoryType::MemoryType_Auto:
            memory_type = Halide::MemoryType::Auto;
            break;
        case Halide::Serdes::MemoryType::MemoryType_Heap:
            memory_type = Halide::MemoryType::Heap;
            break;
        case Halide::Serdes::MemoryType::MemoryType_Stack:
            memory_type = Halide::MemoryType::Stack;
            break;
        case Halide::Serdes::MemoryType::MemoryType_Register:
            memory_type = Halide::MemoryType::Register;
            break;
        case Halide::Serdes::MemoryType::MemoryType_GPUShared:
            memory_type = Halide::MemoryType::GPUShared;
            break;
        case Halide::Serdes::MemoryType::MemoryType_GPUTexture:
            memory_type = Halide::MemoryType::GPUTexture;
            break;
        case Halide::Serdes::MemoryType::MemoryType_LockedCache:
            memory_type = Halide::MemoryType::LockedCache;
            break;
        case Halide::Serdes::MemoryType::MemoryType_VTCM:
            memory_type = Halide::MemoryType::VTCM;
            break;
        case Halide::Serdes::MemoryType::MemoryType_AMXTile:
            memory_type = Halide::MemoryType::AMXTile;
            break;
        }
        auto extents_serialized = allocate_stmt->extents();
        auto extents_types = allocate_stmt->extents_type();
        std::vector<Expr> extents;
        for (size_t i = 0; i < extents_serialized->size(); ++i) {
            auto extent = deserialize_expr(extents_types->Get(i), extents_serialized->Get(i));
            extents.push_back(extent);
        }
        auto condition = deserialize_expr(allocate_stmt->condition_type(), allocate_stmt->condition());
        auto new_expr = deserialize_expr(allocate_stmt->new_expr_type(), allocate_stmt->new_expr());
        auto free_function = deserialize_string(allocate_stmt->free_function());
        auto padding = allocate_stmt->padding();
        auto body = deserialize_stmt(allocate_stmt->body_type(), allocate_stmt->body());
        return Halide::Internal::Allocate::make(name, type, memory_type, extents, condition, body, new_expr, free_function, padding);
    }
    case Halide::Serdes::Stmt_Free: {
        const Halide::Serdes::Free *free_stmt = (const Halide::Serdes::Free *)stmt;
        auto name = deserialize_string(free_stmt->name());
        return Halide::Internal::Free::make(name);
    }
    case Halide::Serdes::Stmt_Realize: {
        const Halide::Serdes::Realize *realize_stmt = (const Halide::Serdes::Realize *)stmt;
        auto name = deserialize_string(realize_stmt->name());
        std::vector<Type> types;
        for (const auto &type : *realize_stmt->types()) {
            types.push_back(deserialize_type(type));
        }
        Halide::MemoryType memory_type = Halide::MemoryType::Auto;
        switch (realize_stmt->memory_type()) {
        case Halide::Serdes::MemoryType::MemoryType_Auto:
            memory_type = Halide::MemoryType::Auto;
            break;
        case Halide::Serdes::MemoryType::MemoryType_Heap:
            memory_type = Halide::MemoryType::Heap;
            break;
        case Halide::Serdes::MemoryType::MemoryType_Stack:
            memory_type = Halide::MemoryType::Stack;
            break;
        case Halide::Serdes::MemoryType::MemoryType_Register:
            memory_type = Halide::MemoryType::Register;
            break;
        case Halide::Serdes::MemoryType::MemoryType_GPUShared:
            memory_type = Halide::MemoryType::GPUShared;
            break;
        case Halide::Serdes::MemoryType::MemoryType_GPUTexture:
            memory_type = Halide::MemoryType::GPUTexture;
            break;
        case Halide::Serdes::MemoryType::MemoryType_LockedCache:
            memory_type = Halide::MemoryType::LockedCache;
            break;
        case Halide::Serdes::MemoryType::MemoryType_VTCM:
            memory_type = Halide::MemoryType::VTCM;
            break;
        case Halide::Serdes::MemoryType::MemoryType_AMXTile:
            memory_type = Halide::MemoryType::AMXTile;
            break;
        }
        std::vector<Halide::Range> bounds;
        for (const auto &bound : *realize_stmt->bounds()) {
            bounds.push_back(deserialize_range(bound));
        }
        auto condition = deserialize_expr(realize_stmt->condition_type(), realize_stmt->condition());
        auto body = deserialize_stmt(realize_stmt->body_type(), realize_stmt->body());
        return Halide::Internal::Realize::make(name, types, memory_type, bounds, condition, body);
    }
    case Halide::Serdes::Stmt_Block: {
        const Halide::Serdes::Block *block_stmt = (const Halide::Serdes::Block *)stmt;
        auto first = deserialize_stmt(block_stmt->first_type(), block_stmt->first());
        auto rest = deserialize_stmt(block_stmt->rest_type(), block_stmt->rest());
        return Halide::Internal::Block::make(first, rest);
    }
    case Halide::Serdes::Stmt_IfThenElse: {
        const Halide::Serdes::IfThenElse *if_then_else_stmt = (const Halide::Serdes::IfThenElse *)stmt;
        auto condition = deserialize_expr(if_then_else_stmt->condition_type(), if_then_else_stmt->condition());
        auto then_case = deserialize_stmt(if_then_else_stmt->then_case_type(), if_then_else_stmt->then_case());
        auto else_case = deserialize_stmt(if_then_else_stmt->else_case_type(), if_then_else_stmt->else_case());
        return Halide::Internal::IfThenElse::make(condition, then_case, else_case);
    }
    case Halide::Serdes::Stmt_Evaluate: {
        const Halide::Serdes::Evaluate *evaluate_stmt = (const Halide::Serdes::Evaluate *)stmt;
        auto value = deserialize_expr(evaluate_stmt->value_type(), evaluate_stmt->value());
        return Halide::Internal::Evaluate::make(value);
    }
    case Halide::Serdes::Stmt_Prefetch: {
        const Halide::Serdes::Prefetch *prefetch_stmt = (const Halide::Serdes::Prefetch *)stmt;
        auto name = deserialize_string(prefetch_stmt->name());
        std::vector<Type> types;
        for (const auto &type : *prefetch_stmt->types()) {
            types.push_back(deserialize_type(type));
        }
        std::vector<Range> bounds;
        for (const auto &bound : *prefetch_stmt->bounds()) {
            bounds.push_back(deserialize_range(bound));
        }
        auto condition = deserialize_expr(prefetch_stmt->condition_type(), prefetch_stmt->condition());
        auto body = deserialize_stmt(prefetch_stmt->body_type(), prefetch_stmt->body());
        return Halide::Internal::Prefetch::make(name, types, bounds,
                                                Halide::Internal::PrefetchDirective(),
                                                condition, body);
    }
    case Halide::Serdes::Stmt_Acquire: {
        const Halide::Serdes::Acquire *acquire_stmt = (const Halide::Serdes::Acquire *)stmt;
        auto semaphore = deserialize_expr(acquire_stmt->semaphore_type(), acquire_stmt->semaphore());
        auto count = deserialize_expr(acquire_stmt->count_type(), acquire_stmt->count());
        auto body = deserialize_stmt(acquire_stmt->body_type(), acquire_stmt->body());
        return Halide::Internal::Acquire::make(semaphore, count, body);
    }
    case Halide::Serdes::Stmt_Fork: {
        const Halide::Serdes::Fork *fork_stmt = (const Halide::Serdes::Fork *)stmt;
        auto first = deserialize_stmt(fork_stmt->first_type(), fork_stmt->first());
        auto rest = deserialize_stmt(fork_stmt->rest_type(), fork_stmt->rest());
        return Halide::Internal::Fork::make(first, rest);
    }
    case Halide::Serdes::Stmt_Atomic: {
        const Halide::Serdes::Atomic *atomic_stmt = (const Halide::Serdes::Atomic *)stmt;
        auto producer_name = deserialize_string(atomic_stmt->producer_name());
        auto mutex_name = deserialize_string(atomic_stmt->mutex_name());
        auto body = deserialize_stmt(atomic_stmt->body_type(), atomic_stmt->body());
        return Halide::Internal::Atomic::make(producer_name, mutex_name, body);
    }
    default:
        std::cerr << "unknown type code " << type_code << "\n";
        return Halide::Internal::Stmt();
    }
}

Halide::Expr Deserializer::deserialize_expr(uint8_t type_code, const void *expr) {
    switch (type_code) {
    case Halide::Serdes::Expr::Expr_IntImm: {
        const Halide::Serdes::IntImm *int_imm_expr = (const Halide::Serdes::IntImm *)expr;
        auto value = int_imm_expr->value();
        // TODO: fix this hard-coding
        return Halide::Internal::IntImm::make(Halide::Int(64), value);
    }
    case Halide::Serdes::Expr::Expr_UIntImm: {
        const Halide::Serdes::UIntImm *uint_imm_expr = (const Halide::Serdes::UIntImm *)expr;
        auto value = uint_imm_expr->value();
        return Halide::Internal::UIntImm::make(Halide::UInt(64), value);
    }
    case Halide::Serdes::Expr::Expr_FloatImm: {
        const Halide::Serdes::FloatImm *float_imm_expr = (const Halide::Serdes::FloatImm *)expr;
        auto value = float_imm_expr->value();
        return Halide::Internal::FloatImm::make(Halide::Float(64), value);
    }
    case Halide::Serdes::Expr::Expr_StringImm: {
        const Halide::Serdes::StringImm *string_imm_expr = (const Halide::Serdes::StringImm *)expr;
        auto value = deserialize_string(string_imm_expr->value());
        return Halide::Internal::StringImm::make(value);
    }
    case Halide::Serdes::Expr::Expr_Cast: {
        const Halide::Serdes::Cast *cast_expr = (const Halide::Serdes::Cast *)expr;
        auto value = deserialize_expr(cast_expr->value_type(), cast_expr->value());
        // TODO: this is clearly wrong as well
        return Halide::Internal::Cast::make(Halide::Int(64), value);
    }
    case Halide::Serdes::Expr::Expr_Reinterpret: {
        const Halide::Serdes::Reinterpret *reinterpret_expr = (const Halide::Serdes::Reinterpret *)expr;
        auto value = deserialize_expr(reinterpret_expr->value_type(), reinterpret_expr->value());
        return Halide::Internal::Reinterpret::make(Halide::Int(64), value);
    }
    case Halide::Serdes::Expr::Expr_Add: {
        const Halide::Serdes::Add *add_expr = (const Halide::Serdes::Add *)expr;
        auto a = deserialize_expr(add_expr->a_type(), add_expr->a());
        auto b = deserialize_expr(add_expr->b_type(), add_expr->b());
        return Halide::Internal::Add::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Sub: {
        const Halide::Serdes::Sub *sub_expr = (const Halide::Serdes::Sub *)expr;
        auto a = deserialize_expr(sub_expr->a_type(), sub_expr->a());
        auto b = deserialize_expr(sub_expr->b_type(), sub_expr->b());
        return Halide::Internal::Sub::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Mul: {
        const Halide::Serdes::Mul *mul_expr = (const Halide::Serdes::Mul *)expr;
        auto a = deserialize_expr(mul_expr->a_type(), mul_expr->a());
        auto b = deserialize_expr(mul_expr->b_type(), mul_expr->b());
        return Halide::Internal::Mul::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Div: {
        const Halide::Serdes::Div *div_expr = (const Halide::Serdes::Div *)expr;
        auto a = deserialize_expr(div_expr->a_type(), div_expr->a());
        auto b = deserialize_expr(div_expr->b_type(), div_expr->b());
        return Halide::Internal::Div::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Mod: {
        const Halide::Serdes::Mod *mod_expr = (const Halide::Serdes::Mod *)expr;
        auto a = deserialize_expr(mod_expr->a_type(), mod_expr->a());
        auto b = deserialize_expr(mod_expr->b_type(), mod_expr->b());
        return Halide::Internal::Mod::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Min: {
        const Halide::Serdes::Min *min_expr = (const Halide::Serdes::Min *)expr;
        auto a = deserialize_expr(min_expr->a_type(), min_expr->a());
        auto b = deserialize_expr(min_expr->b_type(), min_expr->b());
        return Halide::Internal::Min::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Max: {
        const Halide::Serdes::Max *max_expr = (const Halide::Serdes::Max *)expr;
        auto a = deserialize_expr(max_expr->a_type(), max_expr->a());
        auto b = deserialize_expr(max_expr->b_type(), max_expr->b());
        return Halide::Internal::Max::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_EQ: {
        const Halide::Serdes::EQ *eq_expr = (const Halide::Serdes::EQ *)expr;
        auto a = deserialize_expr(eq_expr->a_type(), eq_expr->a());
        auto b = deserialize_expr(eq_expr->b_type(), eq_expr->b());
        return Halide::Internal::EQ::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_NE: {
        const Halide::Serdes::NE *ne_expr = (const Halide::Serdes::NE *)expr;
        auto a = deserialize_expr(ne_expr->a_type(), ne_expr->a());
        auto b = deserialize_expr(ne_expr->b_type(), ne_expr->b());
        return Halide::Internal::NE::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_LT: {
        const Halide::Serdes::LT *lt_expr = (const Halide::Serdes::LT *)expr;
        auto a = deserialize_expr(lt_expr->a_type(), lt_expr->a());
        auto b = deserialize_expr(lt_expr->b_type(), lt_expr->b());
        return Halide::Internal::LT::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_LE: {
        const Halide::Serdes::LE *le_expr = (const Halide::Serdes::LE *)expr;
        auto a = deserialize_expr(le_expr->a_type(), le_expr->a());
        auto b = deserialize_expr(le_expr->b_type(), le_expr->b());
        return Halide::Internal::LE::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_GT: {
        const Halide::Serdes::GT *gt_expr = (const Halide::Serdes::GT *)expr;
        auto a = deserialize_expr(gt_expr->a_type(), gt_expr->a());
        auto b = deserialize_expr(gt_expr->b_type(), gt_expr->b());
        return Halide::Internal::GT::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_GE: {
        const Halide::Serdes::GE *ge_expr = (const Halide::Serdes::GE *)expr;
        auto a = deserialize_expr(ge_expr->a_type(), ge_expr->a());
        auto b = deserialize_expr(ge_expr->b_type(), ge_expr->b());
        return Halide::Internal::GE::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_And: {
        const Halide::Serdes::And *and_expr = (const Halide::Serdes::And *)expr;
        auto a = deserialize_expr(and_expr->a_type(), and_expr->a());
        auto b = deserialize_expr(and_expr->b_type(), and_expr->b());
        return Halide::Internal::And::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Or: {
        const Halide::Serdes::Or *or_expr = (const Halide::Serdes::Or *)expr;
        auto a = deserialize_expr(or_expr->a_type(), or_expr->a());
        auto b = deserialize_expr(or_expr->b_type(), or_expr->b());
        return Halide::Internal::Or::make(a, b);
    }
    case Halide::Serdes::Expr::Expr_Not: {
        const Halide::Serdes::Not *not_expr = (const Halide::Serdes::Not *)expr;
        auto a = deserialize_expr(not_expr->a_type(), not_expr->a());
        return Halide::Internal::Not::make(a);
    }
    case Halide::Serdes::Expr::Expr_Select: {
        const Halide::Serdes::Select *select_expr = (const Halide::Serdes::Select *)expr;
        auto condition = deserialize_expr(select_expr->condition_type(), select_expr->condition());
        auto true_value = deserialize_expr(select_expr->true_value_type(), select_expr->true_value());
        auto false_value = deserialize_expr(select_expr->false_value_type(), select_expr->false_value());
        return Halide::Internal::Select::make(condition, true_value, false_value);
    }
    case Halide::Serdes::Expr::Expr_Load: {
        const Halide::Serdes::Load *load_expr = (const Halide::Serdes::Load *)expr;
        auto name = deserialize_string(load_expr->name());
        auto predicate = deserialize_expr(load_expr->predicate_type(), load_expr->predicate());
        auto index = deserialize_expr(load_expr->index_type(), load_expr->index());
        return Halide::Internal::Load::make(Halide::Int(64), name, index, Halide::Buffer<float, 3>(), Halide::Internal::Parameter(), predicate, Halide::Internal::ModulusRemainder());
    }
    case Halide::Serdes::Expr::Expr_Ramp: {
        const Halide::Serdes::Ramp *ramp_expr = (const Halide::Serdes::Ramp *)expr;
        auto base = deserialize_expr(ramp_expr->base_type(), ramp_expr->base());
        auto stride = deserialize_expr(ramp_expr->stride_type(), ramp_expr->stride());
        auto lanes = ramp_expr->lanes();
        return Halide::Internal::Ramp::make(base, stride, lanes);
    }
    case Halide::Serdes::Expr::Expr_Broadcast: {
        const Halide::Serdes::Broadcast *broadcast_expr = (const Halide::Serdes::Broadcast *)expr;
        auto value = deserialize_expr(broadcast_expr->value_type(), broadcast_expr->value());
        auto lanes = broadcast_expr->lanes();
        return Halide::Internal::Broadcast::make(value, lanes);
    }
    case Halide::Serdes::Expr::Expr_Let: {
        const Halide::Serdes::Let *let_expr = (const Halide::Serdes::Let *)expr;
        auto name = deserialize_string(let_expr->name());
        auto value = deserialize_expr(let_expr->value_type(), let_expr->value());
        auto body = deserialize_expr(let_expr->body_type(), let_expr->body());
        return Halide::Internal::Let::make(name, value, body);
    }
    case Halide::Serdes::Expr::Expr_Call: {
        const Halide::Serdes::Call *call_expr = (const Halide::Serdes::Call *)expr;
        auto name = deserialize_string(call_expr->name());
        auto args_type = call_expr->args_type();
        auto args_serialized = call_expr->args();
        std::vector<Halide::Expr> args;
        for (size_t i = 0; i < args_serialized->size(); ++i) {
            auto arg = deserialize_expr(args_type->Get(i), args_serialized->Get(i));
            args.push_back(arg);
        }
        auto value_index = call_expr->value_index();
        // TODO: fix type and function ptr here once function DAG is fixed
        return Halide::Internal::Call::make(Halide::Int(64), name, args, Halide::Internal::Call::CallType::Extern, Halide::Internal::FunctionPtr(), value_index);
    }
    case Halide::Serdes::Expr::Expr_Variable: {
        const Halide::Serdes::Variable *variable_expr = (const Halide::Serdes::Variable *)expr;
        auto name = deserialize_string(variable_expr->name());
        return Halide::Internal::Variable::make(Halide::Int(64), name);
    }
    case Halide::Serdes::Expr::Expr_Shuffle: {
        const Halide::Serdes::Shuffle *shuffle_expr = (const Halide::Serdes::Shuffle *)expr;
        auto vectors_type = shuffle_expr->vectors_type();
        auto vectors_serialized = shuffle_expr->vectors();
        std::vector<Halide::Expr> vectors;
        for (size_t i = 0; i < vectors_serialized->size(); ++i) {
            auto vector = deserialize_expr(vectors_type->Get(i), vectors_serialized->Get(i));
            vectors.push_back(vector);
        }
        auto indices_serialized = shuffle_expr->indices();
        std::vector<int32_t> indices;
        for (size_t i = 0; i < indices_serialized->size(); ++i) {
            indices.push_back(indices_serialized->Get(i));
        }
        return Halide::Internal::Shuffle::make(vectors, indices);
    }
    case Halide::Serdes::Expr::Expr_VectorReduce: {
        const Halide::Serdes::VectorReduce *vector_reduce_expr = (const Halide::Serdes::VectorReduce *)expr;
        auto value = deserialize_expr(vector_reduce_expr->value_type(), vector_reduce_expr->value());
        // TODO: fix op here and store lanes during serialization
        return Halide::Internal::VectorReduce::make(Halide::Internal::VectorReduce::Operator::Add, value, 16);
    }
    default: {
        std::cerr << "unknown type code " << type_code << "\n";
        return Halide::Expr();
    }
    }
}

Halide::Range Deserializer::deserialize_range(const Halide::Serdes::Range *range) {
    auto min = deserialize_expr(range->min_type(), range->min());
    auto extent = deserialize_expr(range->extent_type(), range->extent());
    return Halide::Range(min, extent);
}

Pipeline Deserializer::deserialize(const std::string &filename) {
    // unpack binary file
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    if (!in) {
        std::cerr << "failed to open file " << filename << "\n";
        return Pipeline();
    }
    in.seekg(0, std::ios::end);
    int size = in.tellg();
    in.seekg(0, std::ios::beg);
    char *data = new char[size];
    in.read(data, size);
    in.close();

    const auto *pipeline_obj = Halide::Serdes::GetPipeline(data);
    const auto *func_objs = pipeline_obj->outputs();
    std::vector<Halide::Func> funcs;
    for (const auto &fo : *func_objs) {
        auto function_deserialized = deserialize_function(fo);
        Halide::Func func(function_deserialized);
        funcs.push_back(func);
    }

    const auto *requirements_objs = pipeline_obj->requirements();
    const auto *requirement_type_objs = pipeline_obj->requirements_type();

    std::vector<Halide::Internal::Stmt> requirements;
    for (size_t i = 0; i < requirements_objs->size(); ++i) {
        auto requirement_deserialized = deserialize_stmt(requirement_type_objs->Get(i), requirements_objs->Get(i));
        requirements.push_back(requirement_deserialized);
    }
    return Pipeline(funcs);
}
