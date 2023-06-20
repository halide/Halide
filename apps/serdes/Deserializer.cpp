#include "Deserializer.h"
#include <fstream>
#include <iostream>

std::string Deserializer::deserialize_string(const flatbuffers::String *str) {
    return str->str();
}

Halide::MemoryType Deserializer::deserialize_memory_type(const Halide::Serialize::MemoryType memory_type) {
    switch (memory_type) {
    case Halide::Serialize::MemoryType::MemoryType_Auto:
        return Halide::MemoryType::Auto;
    case Halide::Serialize::MemoryType::MemoryType_Heap:
        return Halide::MemoryType::Heap;
    case Halide::Serialize::MemoryType::MemoryType_Stack:
        return Halide::MemoryType::Stack;
    case Halide::Serialize::MemoryType::MemoryType_Register:
        return Halide::MemoryType::Register;
    case Halide::Serialize::MemoryType::MemoryType_GPUShared:
        return Halide::MemoryType::GPUShared;
    case Halide::Serialize::MemoryType::MemoryType_GPUTexture:
        return Halide::MemoryType::GPUTexture;
    case Halide::Serialize::MemoryType::MemoryType_LockedCache:
        return Halide::MemoryType::LockedCache;
    case Halide::Serialize::MemoryType::MemoryType_VTCM:
        return Halide::MemoryType::VTCM;
    case Halide::Serialize::MemoryType::MemoryType_AMXTile:
        return Halide::MemoryType::AMXTile;
    default:
        std::cerr << "unknown memory type " << memory_type << "\n";
        exit(1);
    }
}

Halide::Internal::ForType Deserializer::deserialize_for_type(const Halide::Serialize::ForType for_type) {
    switch (for_type) {
    case Halide::Serialize::ForType::ForType_Serial:
        return Halide::Internal::ForType::Serial;
    case Halide::Serialize::ForType::ForType_Parallel:
        return Halide::Internal::ForType::Parallel;
    case Halide::Serialize::ForType::ForType_Vectorized:
        return Halide::Internal::ForType::Vectorized;
    case Halide::Serialize::ForType::ForType_Unrolled:
        return Halide::Internal::ForType::Unrolled;
    case Halide::Serialize::ForType::ForType_Extern:
        return Halide::Internal::ForType::Extern;
    case Halide::Serialize::ForType::ForType_GPUBlock:
        return Halide::Internal::ForType::GPUBlock;
    case Halide::Serialize::ForType::ForType_GPUThread:
        return Halide::Internal::ForType::GPUThread;
    case Halide::Serialize::ForType::ForType_GPULane:
        return Halide::Internal::ForType::GPULane;
    default:
        std::cerr << "unknown for type " << for_type << "\n";
        exit(1);
    }
}

Halide::DeviceAPI Deserializer::deserialize_device_api(const Halide::Serialize::DeviceAPI device_api) {
    switch (device_api) {
    case Halide::Serialize::DeviceAPI::DeviceAPI_None:
        return Halide::DeviceAPI::None;
    case Halide::Serialize::DeviceAPI::DeviceAPI_Host:
        return Halide::DeviceAPI::Host;
    case Halide::Serialize::DeviceAPI::DeviceAPI_Default_GPU:
        return Halide::DeviceAPI::Default_GPU;
    case Halide::Serialize::DeviceAPI::DeviceAPI_CUDA:
        return Halide::DeviceAPI::CUDA;
    case Halide::Serialize::DeviceAPI::DeviceAPI_OpenCL:
        return Halide::DeviceAPI::OpenCL;
    case Halide::Serialize::DeviceAPI::DeviceAPI_OpenGLCompute:
        return Halide::DeviceAPI::OpenGLCompute;
    case Halide::Serialize::DeviceAPI::DeviceAPI_Metal:
        return Halide::DeviceAPI::Metal;
    case Halide::Serialize::DeviceAPI::DeviceAPI_Hexagon:
        return Halide::DeviceAPI::Hexagon;
    case Halide::Serialize::DeviceAPI::DeviceAPI_HexagonDma:
        return Halide::DeviceAPI::HexagonDma;
    case Halide::Serialize::DeviceAPI::DeviceAPI_D3D12Compute:
        return Halide::DeviceAPI::D3D12Compute;
    case Halide::Serialize::DeviceAPI::DeviceAPI_Vulkan:
        return Halide::DeviceAPI::Vulkan;
    case Halide::Serialize::DeviceAPI::DeviceAPI_WebGPU:
        return Halide::DeviceAPI::WebGPU;
    default:
        std::cerr << "unknown device api " << device_api << "\n";
        exit(1);
    }
}

Halide::Internal::Call::CallType Deserializer::deserialize_call_type(const Halide::Serialize::CallType call_type) {
    switch (call_type) {
    case Halide::Serialize::CallType::CallType_Image:
        return Halide::Internal::Call::CallType::Image;
    case Halide::Serialize::CallType::CallType_Extern:
        return Halide::Internal::Call::CallType::Extern;
    case Halide::Serialize::CallType::CallType_ExternCPlusPlus:
        return Halide::Internal::Call::CallType::ExternCPlusPlus;
    case Halide::Serialize::CallType::CallType_PureExtern:
        return Halide::Internal::Call::CallType::PureExtern;
    case Halide::Serialize::CallType::CallType_Halide:
        return Halide::Internal::Call::CallType::Halide;
    case Halide::Serialize::CallType::CallType_PureIntrinsic:
        return Halide::Internal::Call::CallType::PureIntrinsic;
    default:
        std::cerr << "unknown call type " << call_type << "\n";
        exit(1);
    }
}

Halide::Internal::VectorReduce::Operator Deserializer::deserialize_vector_reduce_op(const Halide::Serialize::VectorReduceOp vector_reduce_op) {
    switch (vector_reduce_op) {
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_Add:
        return Halide::Internal::VectorReduce::Operator::Add;
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_SaturatingAdd:
        return Halide::Internal::VectorReduce::Operator::SaturatingAdd;
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_Mul:
        return Halide::Internal::VectorReduce::Operator::Mul;
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_Min:
        return Halide::Internal::VectorReduce::Operator::Min;
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_Max:
        return Halide::Internal::VectorReduce::Operator::Max;
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_And:
        return Halide::Internal::VectorReduce::Operator::And;
    case Halide::Serialize::VectorReduceOp::VectorReduceOp_Or:
        return Halide::Internal::VectorReduce::Operator::Or;
    default:
        std::cerr << "unknown vector reduce op " << vector_reduce_op << "\n";
        exit(1);
    }
}

Halide::PrefetchBoundStrategy Deserializer::deserialize_prefetch_bound_strategy(const Halide::Serialize::PrefetchBoundStrategy prefetch_bound_strategy) {
    switch (prefetch_bound_strategy) {
    case Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_Clamp:
        return Halide::PrefetchBoundStrategy::Clamp;
    case Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_GuardWithIf:
        return Halide::PrefetchBoundStrategy::GuardWithIf;
    case Halide::Serialize::PrefetchBoundStrategy::PrefetchBoundStrategy_NonFaulting:
        return Halide::PrefetchBoundStrategy::NonFaulting;
    default:
        std::cerr << "unknown prefetch bound strategy " << prefetch_bound_strategy << "\n";
        exit(1);
    }
}

Halide::NameMangling Deserializer::deserialize_name_mangling(const Halide::Serialize::NameMangling name_mangling) {
    switch (name_mangling) {
    case Halide::Serialize::NameMangling::NameMangling_Default:
        return Halide::NameMangling::Default;
    case Halide::Serialize::NameMangling::NameMangling_C:
        return Halide::NameMangling::C;
    case Halide::Serialize::NameMangling::NameMangling_CPlusPlus:
        return Halide::NameMangling::CPlusPlus;
    default:
        std::cerr << "unknown name mangling " << name_mangling << "\n";
        exit(1);
    }
}

Halide::Type Deserializer::deserialize_type(const Halide::Serialize::Type *type) {
    using Halide::Serialize::TypeCode;
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

    return Halide::Type(code, bits, lanes);
}

Halide::Internal::Function Deserializer::deserialize_function(const Halide::Serialize::Func *function) {
    std::string name = deserialize_string(function->name());
    std::string origin_name = deserialize_string(function->origin_name());
    std::vector<Halide::Type> output_types;
    output_types.reserve(function->output_types()->size());
    for (const auto &type : *function->output_types()) {
        output_types.push_back(deserialize_type(type));
    }
    std::vector<Halide::Type> required_types;
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

    std::vector<Halide::Internal::Definition> updates;
    for (const auto &update : *function->updates()) {
        updates.push_back(deserialize_definition(update));
    }

    std::string debug_file = deserialize_string(function->debug_file());
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

    return Halide::Internal::Function(name, origin_name, output_types, required_types,
                                      required_dim, args, func_schedule, init_def, updates,
                                      debug_file, extern_function_name, name_mangling,
                                      extern_function_device_api, extern_proxy_expr,
                                      trace_loads, trace_stores, trace_realizations, trace_tags, frozen);
}

Halide::Internal::Stmt Deserializer::deserialize_stmt(uint8_t type_code, const void *stmt) {
    switch (type_code) {
    case Halide::Serialize::Stmt_LetStmt: {
        const Halide::Serialize::LetStmt *let_stmt = (const Halide::Serialize::LetStmt *)stmt;
        auto name = deserialize_string(let_stmt->name());
        auto value = deserialize_expr(let_stmt->value_type(), let_stmt->value());
        auto body = deserialize_stmt(let_stmt->body_type(), let_stmt->body());
        return Halide::Internal::LetStmt::make(name, value, body);
    }
    case Halide::Serialize::Stmt_AssertStmt: {
        const Halide::Serialize::AssertStmt *assert_stmt = (const Halide::Serialize::AssertStmt *)stmt;
        auto condition = deserialize_expr(assert_stmt->condition_type(), assert_stmt->condition());
        auto message = deserialize_expr(assert_stmt->message_type(), assert_stmt->message());
        return Halide::Internal::AssertStmt::make(condition, message);
    }
    case Halide::Serialize::Stmt_ProducerConsumer: {
        const Halide::Serialize::ProducerConsumer *producer_consumer = (const Halide::Serialize::ProducerConsumer *)stmt;
        auto name = deserialize_string(producer_consumer->name());
        auto is_producer = producer_consumer->is_producer();
        auto body = deserialize_stmt(producer_consumer->body_type(), producer_consumer->body());
        return Halide::Internal::ProducerConsumer::make(name, is_producer, body);
    }
    case Halide::Serialize::Stmt_For: {
        const Halide::Serialize::For *for_stmt = (const Halide::Serialize::For *)stmt;
        auto name = deserialize_string(for_stmt->name());
        auto min = deserialize_expr(for_stmt->min_type(), for_stmt->min());
        auto extent = deserialize_expr(for_stmt->extent_type(), for_stmt->extent());
        Halide::Internal::ForType for_type = deserialize_for_type(for_stmt->for_type());

        Halide::DeviceAPI device_api = deserialize_device_api(for_stmt->device_api());
        auto body = deserialize_stmt(for_stmt->body_type(), for_stmt->body());
        return Halide::Internal::For::make(name, min, extent, for_type, device_api, body);
    }
    case Halide::Serialize::Stmt_Store: {
        const Halide::Serialize::Store *store_stmt = (const Halide::Serialize::Store *)stmt;
        auto name = deserialize_string(store_stmt->name());
        auto predicate = deserialize_expr(store_stmt->predicate_type(), store_stmt->predicate());
        auto value = deserialize_expr(store_stmt->value_type(), store_stmt->value());
        auto index = deserialize_expr(store_stmt->index_type(), store_stmt->index());
        auto alignment = deserialize_modulus_remainder(store_stmt->alignment());
        return Halide::Internal::Store::make(name, value, index, Halide::Internal::Parameter(), predicate, alignment);
    }
    case Halide::Serialize::Stmt_Provide: {
        const Halide::Serialize::Provide *provide_stmt = (const Halide::Serialize::Provide *)stmt;
        auto name = deserialize_string(provide_stmt->name());
        std::vector<Halide::Expr> values = deserialize_expr_vector(provide_stmt->values_type(), provide_stmt->values());
        std::vector<Halide::Expr> args = deserialize_expr_vector(provide_stmt->args_type(), provide_stmt->args());
        auto predicate = deserialize_expr(provide_stmt->predicate_type(), provide_stmt->predicate());
        return Halide::Internal::Provide::make(name, values, args, predicate);
    }
    case Halide::Serialize::Stmt_Allocate: {
        const Halide::Serialize::Allocate *allocate_stmt = (const Halide::Serialize::Allocate *)stmt;
        auto name = deserialize_string(allocate_stmt->name());
        auto type = deserialize_type(allocate_stmt->type());
        Halide::MemoryType memory_type = deserialize_memory_type(allocate_stmt->memory_type());
        std::vector<Halide::Expr> extents = deserialize_expr_vector(allocate_stmt->extents_type(), allocate_stmt->extents());
        auto condition = deserialize_expr(allocate_stmt->condition_type(), allocate_stmt->condition());
        auto new_expr = deserialize_expr(allocate_stmt->new_expr_type(), allocate_stmt->new_expr());
        auto free_function = deserialize_string(allocate_stmt->free_function());
        auto padding = allocate_stmt->padding();
        auto body = deserialize_stmt(allocate_stmt->body_type(), allocate_stmt->body());
        return Halide::Internal::Allocate::make(name, type, memory_type, extents, condition, body, new_expr, free_function, padding);
    }
    case Halide::Serialize::Stmt_Free: {
        const Halide::Serialize::Free *free_stmt = (const Halide::Serialize::Free *)stmt;
        auto name = deserialize_string(free_stmt->name());
        return Halide::Internal::Free::make(name);
    }
    case Halide::Serialize::Stmt_Realize: {
        const Halide::Serialize::Realize *realize_stmt = (const Halide::Serialize::Realize *)stmt;
        auto name = deserialize_string(realize_stmt->name());
        std::vector<Halide::Type> types;
        types.reserve(realize_stmt->types()->size());
        for (const auto &type : *realize_stmt->types()) {
            types.push_back(deserialize_type(type));
        }
        Halide::MemoryType memory_type = deserialize_memory_type(realize_stmt->memory_type());
        std::vector<Halide::Range> bounds;
        bounds.reserve(realize_stmt->bounds()->size());
        for (const auto &bound : *realize_stmt->bounds()) {
            bounds.push_back(deserialize_range(bound));
        }
        auto condition = deserialize_expr(realize_stmt->condition_type(), realize_stmt->condition());
        auto body = deserialize_stmt(realize_stmt->body_type(), realize_stmt->body());
        return Halide::Internal::Realize::make(name, types, memory_type, bounds, condition, body);
    }
    case Halide::Serialize::Stmt_Block: {
        const Halide::Serialize::Block *block_stmt = (const Halide::Serialize::Block *)stmt;
        auto first = deserialize_stmt(block_stmt->first_type(), block_stmt->first());
        auto rest = deserialize_stmt(block_stmt->rest_type(), block_stmt->rest());
        return Halide::Internal::Block::make(first, rest);
    }
    case Halide::Serialize::Stmt_IfThenElse: {
        const Halide::Serialize::IfThenElse *if_then_else_stmt = (const Halide::Serialize::IfThenElse *)stmt;
        auto condition = deserialize_expr(if_then_else_stmt->condition_type(), if_then_else_stmt->condition());
        auto then_case = deserialize_stmt(if_then_else_stmt->then_case_type(), if_then_else_stmt->then_case());
        auto else_case = deserialize_stmt(if_then_else_stmt->else_case_type(), if_then_else_stmt->else_case());
        return Halide::Internal::IfThenElse::make(condition, then_case, else_case);
    }
    case Halide::Serialize::Stmt_Evaluate: {
        const Halide::Serialize::Evaluate *evaluate_stmt = (const Halide::Serialize::Evaluate *)stmt;
        auto value = deserialize_expr(evaluate_stmt->value_type(), evaluate_stmt->value());
        return Halide::Internal::Evaluate::make(value);
    }
    case Halide::Serialize::Stmt_Prefetch: {
        const Halide::Serialize::Prefetch *prefetch_stmt = (const Halide::Serialize::Prefetch *)stmt;
        auto name = deserialize_string(prefetch_stmt->name());
        std::vector<Halide::Type> types;
        types.reserve(prefetch_stmt->types()->size());
        for (const auto &type : *prefetch_stmt->types()) {
            types.push_back(deserialize_type(type));
        }
        std::vector<Halide::Range> bounds;
        bounds.reserve(prefetch_stmt->bounds()->size());
        for (const auto &bound : *prefetch_stmt->bounds()) {
            bounds.push_back(deserialize_range(bound));
        }
        auto prefetch = deserialize_prefetch_directive(prefetch_stmt->prefetch());
        auto condition = deserialize_expr(prefetch_stmt->condition_type(), prefetch_stmt->condition());
        auto body = deserialize_stmt(prefetch_stmt->body_type(), prefetch_stmt->body());
        return Halide::Internal::Prefetch::make(name, types, bounds, prefetch, condition, body);
    }
    case Halide::Serialize::Stmt_Acquire: {
        const Halide::Serialize::Acquire *acquire_stmt = (const Halide::Serialize::Acquire *)stmt;
        auto semaphore = deserialize_expr(acquire_stmt->semaphore_type(), acquire_stmt->semaphore());
        auto count = deserialize_expr(acquire_stmt->count_type(), acquire_stmt->count());
        auto body = deserialize_stmt(acquire_stmt->body_type(), acquire_stmt->body());
        return Halide::Internal::Acquire::make(semaphore, count, body);
    }
    case Halide::Serialize::Stmt_Fork: {
        const Halide::Serialize::Fork *fork_stmt = (const Halide::Serialize::Fork *)stmt;
        auto first = deserialize_stmt(fork_stmt->first_type(), fork_stmt->first());
        auto rest = deserialize_stmt(fork_stmt->rest_type(), fork_stmt->rest());
        return Halide::Internal::Fork::make(first, rest);
    }
    case Halide::Serialize::Stmt_Atomic: {
        const Halide::Serialize::Atomic *atomic_stmt = (const Halide::Serialize::Atomic *)stmt;
        auto producer_name = deserialize_string(atomic_stmt->producer_name());
        auto mutex_name = deserialize_string(atomic_stmt->mutex_name());
        auto body = deserialize_stmt(atomic_stmt->body_type(), atomic_stmt->body());
        return Halide::Internal::Atomic::make(producer_name, mutex_name, body);
    }
    case Halide::Serialize::Stmt_UndefinedStmt: {
        return Halide::Internal::Stmt();
    }
    default:
        std::cerr << "unknown type code " << type_code << "\n";
        return Halide::Internal::Stmt();
    }
}

Halide::Expr Deserializer::deserialize_expr(uint8_t type_code, const void *expr) {
    switch (type_code) {
    case Halide::Serialize::Expr::Expr_IntImm: {
        const Halide::Serialize::IntImm *int_imm_expr = (const Halide::Serialize::IntImm *)expr;
        auto value = int_imm_expr->value();
        // TODO: fix this hard-coding
        return Halide::Internal::IntImm::make(Halide::Int(64), value);
    }
    case Halide::Serialize::Expr::Expr_UIntImm: {
        const Halide::Serialize::UIntImm *uint_imm_expr = (const Halide::Serialize::UIntImm *)expr;
        auto value = uint_imm_expr->value();
        return Halide::Internal::UIntImm::make(Halide::UInt(64), value);
    }
    case Halide::Serialize::Expr::Expr_FloatImm: {
        const Halide::Serialize::FloatImm *float_imm_expr = (const Halide::Serialize::FloatImm *)expr;
        auto value = float_imm_expr->value();
        return Halide::Internal::FloatImm::make(Halide::Float(64), value);
    }
    case Halide::Serialize::Expr::Expr_StringImm: {
        const Halide::Serialize::StringImm *string_imm_expr = (const Halide::Serialize::StringImm *)expr;
        auto value = deserialize_string(string_imm_expr->value());
        return Halide::Internal::StringImm::make(value);
    }
    case Halide::Serialize::Expr::Expr_Cast: {
        const Halide::Serialize::Cast *cast_expr = (const Halide::Serialize::Cast *)expr;
        auto value = deserialize_expr(cast_expr->value_type(), cast_expr->value());
        // TODO: this is clearly wrong as well
        return Halide::Internal::Cast::make(Halide::Int(64), value);
    }
    case Halide::Serialize::Expr::Expr_Reinterpret: {
        const Halide::Serialize::Reinterpret *reinterpret_expr = (const Halide::Serialize::Reinterpret *)expr;
        auto value = deserialize_expr(reinterpret_expr->value_type(), reinterpret_expr->value());
        return Halide::Internal::Reinterpret::make(Halide::Int(64), value);
    }
    case Halide::Serialize::Expr::Expr_Add: {
        const Halide::Serialize::Add *add_expr = (const Halide::Serialize::Add *)expr;
        auto a = deserialize_expr(add_expr->a_type(), add_expr->a());
        auto b = deserialize_expr(add_expr->b_type(), add_expr->b());
        return Halide::Internal::Add::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Sub: {
        const Halide::Serialize::Sub *sub_expr = (const Halide::Serialize::Sub *)expr;
        auto a = deserialize_expr(sub_expr->a_type(), sub_expr->a());
        auto b = deserialize_expr(sub_expr->b_type(), sub_expr->b());
        return Halide::Internal::Sub::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Mul: {
        const Halide::Serialize::Mul *mul_expr = (const Halide::Serialize::Mul *)expr;
        auto a = deserialize_expr(mul_expr->a_type(), mul_expr->a());
        auto b = deserialize_expr(mul_expr->b_type(), mul_expr->b());
        return Halide::Internal::Mul::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Div: {
        const Halide::Serialize::Div *div_expr = (const Halide::Serialize::Div *)expr;
        auto a = deserialize_expr(div_expr->a_type(), div_expr->a());
        auto b = deserialize_expr(div_expr->b_type(), div_expr->b());
        return Halide::Internal::Div::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Mod: {
        const Halide::Serialize::Mod *mod_expr = (const Halide::Serialize::Mod *)expr;
        auto a = deserialize_expr(mod_expr->a_type(), mod_expr->a());
        auto b = deserialize_expr(mod_expr->b_type(), mod_expr->b());
        return Halide::Internal::Mod::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Min: {
        const Halide::Serialize::Min *min_expr = (const Halide::Serialize::Min *)expr;
        auto a = deserialize_expr(min_expr->a_type(), min_expr->a());
        auto b = deserialize_expr(min_expr->b_type(), min_expr->b());
        return Halide::Internal::Min::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Max: {
        const Halide::Serialize::Max *max_expr = (const Halide::Serialize::Max *)expr;
        auto a = deserialize_expr(max_expr->a_type(), max_expr->a());
        auto b = deserialize_expr(max_expr->b_type(), max_expr->b());
        return Halide::Internal::Max::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_EQ: {
        const Halide::Serialize::EQ *eq_expr = (const Halide::Serialize::EQ *)expr;
        auto a = deserialize_expr(eq_expr->a_type(), eq_expr->a());
        auto b = deserialize_expr(eq_expr->b_type(), eq_expr->b());
        return Halide::Internal::EQ::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_NE: {
        const Halide::Serialize::NE *ne_expr = (const Halide::Serialize::NE *)expr;
        auto a = deserialize_expr(ne_expr->a_type(), ne_expr->a());
        auto b = deserialize_expr(ne_expr->b_type(), ne_expr->b());
        return Halide::Internal::NE::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_LT: {
        const Halide::Serialize::LT *lt_expr = (const Halide::Serialize::LT *)expr;
        auto a = deserialize_expr(lt_expr->a_type(), lt_expr->a());
        auto b = deserialize_expr(lt_expr->b_type(), lt_expr->b());
        return Halide::Internal::LT::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_LE: {
        const Halide::Serialize::LE *le_expr = (const Halide::Serialize::LE *)expr;
        auto a = deserialize_expr(le_expr->a_type(), le_expr->a());
        auto b = deserialize_expr(le_expr->b_type(), le_expr->b());
        return Halide::Internal::LE::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_GT: {
        const Halide::Serialize::GT *gt_expr = (const Halide::Serialize::GT *)expr;
        auto a = deserialize_expr(gt_expr->a_type(), gt_expr->a());
        auto b = deserialize_expr(gt_expr->b_type(), gt_expr->b());
        return Halide::Internal::GT::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_GE: {
        const Halide::Serialize::GE *ge_expr = (const Halide::Serialize::GE *)expr;
        auto a = deserialize_expr(ge_expr->a_type(), ge_expr->a());
        auto b = deserialize_expr(ge_expr->b_type(), ge_expr->b());
        return Halide::Internal::GE::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_And: {
        const Halide::Serialize::And *and_expr = (const Halide::Serialize::And *)expr;
        auto a = deserialize_expr(and_expr->a_type(), and_expr->a());
        auto b = deserialize_expr(and_expr->b_type(), and_expr->b());
        return Halide::Internal::And::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Or: {
        const Halide::Serialize::Or *or_expr = (const Halide::Serialize::Or *)expr;
        auto a = deserialize_expr(or_expr->a_type(), or_expr->a());
        auto b = deserialize_expr(or_expr->b_type(), or_expr->b());
        return Halide::Internal::Or::make(a, b);
    }
    case Halide::Serialize::Expr::Expr_Not: {
        const Halide::Serialize::Not *not_expr = (const Halide::Serialize::Not *)expr;
        auto a = deserialize_expr(not_expr->a_type(), not_expr->a());
        return Halide::Internal::Not::make(a);
    }
    case Halide::Serialize::Expr::Expr_Select: {
        const Halide::Serialize::Select *select_expr = (const Halide::Serialize::Select *)expr;
        auto condition = deserialize_expr(select_expr->condition_type(), select_expr->condition());
        auto true_value = deserialize_expr(select_expr->true_value_type(), select_expr->true_value());
        auto false_value = deserialize_expr(select_expr->false_value_type(), select_expr->false_value());
        return Halide::Internal::Select::make(condition, true_value, false_value);
    }
    case Halide::Serialize::Expr::Expr_Load: {
        const Halide::Serialize::Load *load_expr = (const Halide::Serialize::Load *)expr;
        auto name = deserialize_string(load_expr->name());
        auto predicate = deserialize_expr(load_expr->predicate_type(), load_expr->predicate());
        auto index = deserialize_expr(load_expr->index_type(), load_expr->index());
        auto alignment = deserialize_modulus_remainder(load_expr->alignment());
        return Halide::Internal::Load::make(Halide::Int(64), name, index, Halide::Buffer<float, 3>(), Halide::Internal::Parameter(), predicate, alignment);
    }
    case Halide::Serialize::Expr::Expr_Ramp: {
        const Halide::Serialize::Ramp *ramp_expr = (const Halide::Serialize::Ramp *)expr;
        auto base = deserialize_expr(ramp_expr->base_type(), ramp_expr->base());
        auto stride = deserialize_expr(ramp_expr->stride_type(), ramp_expr->stride());
        auto lanes = ramp_expr->lanes();
        return Halide::Internal::Ramp::make(base, stride, lanes);
    }
    case Halide::Serialize::Expr::Expr_Broadcast: {
        const Halide::Serialize::Broadcast *broadcast_expr = (const Halide::Serialize::Broadcast *)expr;
        auto value = deserialize_expr(broadcast_expr->value_type(), broadcast_expr->value());
        auto lanes = broadcast_expr->lanes();
        return Halide::Internal::Broadcast::make(value, lanes);
    }
    case Halide::Serialize::Expr::Expr_Let: {
        const Halide::Serialize::Let *let_expr = (const Halide::Serialize::Let *)expr;
        auto name = deserialize_string(let_expr->name());
        auto value = deserialize_expr(let_expr->value_type(), let_expr->value());
        auto body = deserialize_expr(let_expr->body_type(), let_expr->body());
        return Halide::Internal::Let::make(name, value, body);
    }
    case Halide::Serialize::Expr::Expr_Call: {
        const Halide::Serialize::Call *call_expr = (const Halide::Serialize::Call *)expr;
        auto name = deserialize_string(call_expr->name());
        std::vector<Halide::Expr> args = deserialize_expr_vector(call_expr->args_type(), call_expr->args());
        auto value_index = call_expr->value_index();
        auto call_type = deserialize_call_type(call_expr->call_type());
        // TODO: fix type and function ptr here once function DAG is fixed
        return Halide::Internal::Call::make(Halide::Int(64), name, args, call_type, Halide::Internal::FunctionPtr(), value_index);
    }
    case Halide::Serialize::Expr::Expr_Variable: {
        const Halide::Serialize::Variable *variable_expr = (const Halide::Serialize::Variable *)expr;
        auto name = deserialize_string(variable_expr->name());
        auto reduction_domain = deserialize_reduction_domain(variable_expr->reduction_domain());
        return Halide::Internal::Variable::make(Halide::Int(64), name, reduction_domain);
    }
    case Halide::Serialize::Expr::Expr_Shuffle: {
        const Halide::Serialize::Shuffle *shuffle_expr = (const Halide::Serialize::Shuffle *)expr;
        std::vector<Halide::Expr> vectors = deserialize_expr_vector(shuffle_expr->vectors_type(), shuffle_expr->vectors());
        auto indices_serialized = shuffle_expr->indices();
        std::vector<int32_t> indices;
        indices.reserve(indices_serialized->size());
        for (size_t i = 0; i < indices_serialized->size(); ++i) {
            indices.push_back(indices_serialized->Get(i));
        }
        return Halide::Internal::Shuffle::make(vectors, indices);
    }
    case Halide::Serialize::Expr::Expr_VectorReduce: {
        const Halide::Serialize::VectorReduce *vector_reduce_expr = (const Halide::Serialize::VectorReduce *)expr;
        auto value = deserialize_expr(vector_reduce_expr->value_type(), vector_reduce_expr->value());
        auto reduction_op = deserialize_vector_reduce_op(vector_reduce_expr->reduction_op());
        // TODO: store lanes during serialization
        return Halide::Internal::VectorReduce::make(reduction_op, value, 16);
    }
    case Halide::Serialize::Expr::Expr_UndefinedExpr: {
        return Halide::Expr();
    }
    default: {
        std::cerr << "unknown type code " << type_code << "\n";
        return Halide::Expr();
    }
    }
}

std::vector<Halide::Expr> Deserializer::deserialize_expr_vector(const flatbuffers::Vector<uint8_t> *exprs_types, const flatbuffers::Vector<flatbuffers::Offset<void>> *exprs_serialized) {
    std::vector<Halide::Expr> result;
    result.reserve(exprs_serialized->size());
    for (size_t i = 0; i < exprs_serialized->size(); ++i) {
        auto expr = deserialize_expr(exprs_types->Get(i), exprs_serialized->Get(i));
        result.push_back(expr);
    }
    return result;
}

Halide::Range Deserializer::deserialize_range(const Halide::Serialize::Range *range) {
    auto min = deserialize_expr(range->min_type(), range->min());
    auto extent = deserialize_expr(range->extent_type(), range->extent());
    return Halide::Range(min, extent);
}

Halide::Internal::Bound Deserializer::deserialize_bound(const Halide::Serialize::Bound *bound) {
    auto var = deserialize_string(bound->var());
    auto min = deserialize_expr(bound->min_type(), bound->min());
    auto extent = deserialize_expr(bound->extent_type(), bound->extent());
    auto modulus = deserialize_expr(bound->modulus_type(), bound->modulus());
    auto remainder = deserialize_expr(bound->remainder_type(), bound->remainder());
    auto hl_bound = Halide::Internal::Bound();
    hl_bound.var = var;
    hl_bound.min = min;
    hl_bound.extent = extent;
    hl_bound.modulus = modulus;
    hl_bound.remainder = remainder;
    return hl_bound;
}

Halide::Internal::StorageDim Deserializer::deserialize_storage_dim(const Halide::Serialize::StorageDim *storage_dim) {
    auto var = deserialize_string(storage_dim->var());
    auto alignment = deserialize_expr(storage_dim->alignment_type(), storage_dim->alignment());
    auto bound = deserialize_expr(storage_dim->bound_type(), storage_dim->bound());
    auto fold_factor = deserialize_expr(storage_dim->fold_factor_type(), storage_dim->fold_factor());
    auto fold_forward = storage_dim->fold_forward();
    auto hl_storage_dim = Halide::Internal::StorageDim();
    hl_storage_dim.var = var;
    hl_storage_dim.alignment = alignment;
    hl_storage_dim.bound = bound;
    hl_storage_dim.fold_factor = fold_factor;
    hl_storage_dim.fold_forward = fold_forward;
    return hl_storage_dim;
}

Halide::LoopLevel Deserializer::deserialize_loop_level(const Halide::Serialize::LoopLevel *loop_level) {
    auto func_name = deserialize_string(loop_level->func_name());
    auto stage_index = loop_level->stage_index();
    auto var_name = deserialize_string(loop_level->var_name());
    auto is_rvar = loop_level->is_rvar();
    auto locked = loop_level->locked();
    return Halide::LoopLevel(func_name, var_name, is_rvar, stage_index, locked);
}

Halide::Internal::FuncSchedule Deserializer::deserialize_func_schedule(const Halide::Serialize::FuncSchedule *func_schedule) {
    auto store_level = deserialize_loop_level(func_schedule->store_level());
    auto compute_level = deserialize_loop_level(func_schedule->compute_level());
    std::vector<Halide::Internal::StorageDim> storage_dims;
    for (const auto &storage_dim : *func_schedule->storage_dims()) {
        storage_dims.push_back(deserialize_storage_dim(storage_dim));
    }
    std::vector<Halide::Internal::Bound> bounds;
    for (const auto &bound : *func_schedule->bounds()) {
        bounds.push_back(deserialize_bound(bound));
    }
    std::vector<Halide::Internal::Bound> estimates;
    for (const auto &estimate : *func_schedule->estimates()) {
        estimates.push_back(deserialize_bound(estimate));
    }
    auto memory_type = deserialize_memory_type(func_schedule->memory_type());
    auto memoized = func_schedule->memoized();
    auto async = func_schedule->async();
    auto memoize_eviction_key = deserialize_expr(func_schedule->memoize_eviction_key_type(), func_schedule->memoize_eviction_key());
    auto hl_func_schedule = Halide::Internal::FuncSchedule();
    hl_func_schedule.store_level() = store_level;
    hl_func_schedule.compute_level() = compute_level;
    hl_func_schedule.bounds() = bounds;
    hl_func_schedule.estimates() = estimates;
    hl_func_schedule.memory_type() = memory_type;
    hl_func_schedule.memoized() = memoized;
    hl_func_schedule.async() = async;
    hl_func_schedule.memoize_eviction_key() = memoize_eviction_key;
    return hl_func_schedule;
}

Halide::Internal::Specialization Deserializer::deserialize_specialization(const Halide::Serialize::Specialization *specialization) {
    auto condition = deserialize_expr(specialization->condition_type(), specialization->condition());
    auto defintion = deserialize_definition(specialization->definition());
    auto failure_message = deserialize_string(specialization->failure_message());
    Halide::Internal::Specialization hl_specialization;
    hl_specialization.condition = condition;
    hl_specialization.definition = defintion;
    hl_specialization.failure_message = failure_message;
    return hl_specialization;
}

Halide::Internal::Definition Deserializer::deserialize_definition(const Halide::Serialize::Definition *definition) {
    auto is_init = definition->is_init();
    auto predicate = deserialize_expr(definition->predicate_type(), definition->predicate());
    auto args = deserialize_expr_vector(definition->args_type(), definition->args());
    auto values = deserialize_expr_vector(definition->values_type(), definition->values());

    std::vector<Halide::Internal::Specialization> specializations;
    for (const auto &specialization : *definition->specializations()) {
        specializations.push_back(deserialize_specialization(specialization));
    }
    auto source_location = deserialize_string(definition->source_location());
    return Halide::Internal::Definition(is_init, predicate, args, values, Halide::Internal::StageSchedule(), specializations, source_location);
}

Halide::Internal::ReductionVariable Deserializer::deserialize_reduction_variable(const Halide::Serialize::ReductionVariable *reduction_variable) {
    auto var = deserialize_string(reduction_variable->var());
    auto min = deserialize_expr(reduction_variable->min_type(), reduction_variable->min());
    auto extent = deserialize_expr(reduction_variable->extent_type(), reduction_variable->extent());
    auto hl_reduction_variable = Halide::Internal::ReductionVariable();
    hl_reduction_variable.var = var;
    hl_reduction_variable.min = min;
    hl_reduction_variable.extent = extent;
    return hl_reduction_variable;
}

Halide::Internal::ReductionDomain Deserializer::deserialize_reduction_domain(const Halide::Serialize::ReductionDomain *reduction_domain) {
    std::vector<Halide::Internal::ReductionVariable> domain;
    for (const auto &reduction_variable : *reduction_domain->domain()) {
        domain.push_back(deserialize_reduction_variable(reduction_variable));
    }
    auto predicate = deserialize_expr(reduction_domain->predicate_type(), reduction_domain->predicate());
    auto frozen = reduction_domain->frozen();
    return Halide::Internal::ReductionDomain(domain, predicate, frozen);
}

Halide::Internal::ModulusRemainder Deserializer::deserialize_modulus_remainder(const Halide::Serialize::ModulusRemainder *modulus_remainder) {
    return Halide::Internal::ModulusRemainder(modulus_remainder->modulus(), modulus_remainder->remainder());
}

Halide::Internal::PrefetchDirective Deserializer::deserialize_prefetch_directive(const Halide::Serialize::PrefetchDirective *prefetch_directive) {
    auto name = deserialize_string(prefetch_directive->name());
    auto at = deserialize_string(prefetch_directive->at());
    auto from = deserialize_string(prefetch_directive->from());
    auto offset = deserialize_expr(prefetch_directive->offset_type(), prefetch_directive->offset());
    auto strategy = deserialize_prefetch_bound_strategy(prefetch_directive->strategy());
    auto hl_prefetch_directive = Halide::Internal::PrefetchDirective();
    hl_prefetch_directive.name = name;
    hl_prefetch_directive.at = at;
    hl_prefetch_directive.from = from;
    hl_prefetch_directive.offset = offset;
    hl_prefetch_directive.strategy = strategy;
    return hl_prefetch_directive;
}

// TODO: will need to serialize a reverse table of map<address, func_name> to
//       later reconstruct a map of <name, func_ptr> find out which function ptrs to use here
// std::map<std::string, Halide::Internal::FunctionPtr> Deserializer::deserialize_wrapper_refs(const flatbuffers::Vector<flatbuffers::Offset<Halide::Serialize::WrapperRef>> *wrapper_refs) {
//     return std::map<std::string, Halide::Internal::FunctionPtr>();
// }

// std::map<std::string, int32_t> Deserializer::deserialize_func_mappings(const flatbuffers::Vector<flatbuffers::Offset<Halide::Serialize::FuncMapping>> *func_mappings) {
//     std::map<std::string, int32_t> result;
//     for (const auto &func_mapping : *func_mappings) {
//         auto name = deserialize_string(func_mapping->name());
//         auto index = func_mapping->index();
//         result[name] = index;
//     }
//     return result;
// }

// std::map<int32_t, Halide::Internal::FunctionPtr> Deserializer::reconstruct_func_ptr_mappings() {
//     std::map<int32_t, Halide::Internal::FunctionPtr> result;
//     for (const auto &mapping : this->func_mappings_str2idx) {
//         auto name = mapping.first;
//         auto index = mapping.second;
//         auto func_ptr = this->func_mappings_idx2ptr[index];
//         result[index] = func_ptr;
//     }
//     return result;
// }

Halide::Pipeline Deserializer::deserialize(const std::string &filename) {
    // unpack binary file
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    if (!in) {
        std::cerr << "failed to open file " << filename << "\n";
        return Halide::Pipeline();
    }
    std::cout << "Deserializing from file " << filename << "\n";
    in.seekg(0, std::ios::end);
    int size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> data(size);
    in.read(data.data(), size);
    in.close();

    const auto *pipeline_obj = Halide::Serialize::GetPipeline(data.data());
    // this->func_mappings_str2idx = deserialize_func_mappings(pipeline_obj->func_mappings());
    // this->func_mappings_idx2ptr = reconstruct_func_ptr_mappings();
    const auto *func_objs = pipeline_obj->outputs();
    std::vector<Halide::Func> funcs;
    funcs.reserve(func_objs->size());
    for (const auto &fo : *func_objs) {
        auto function_deserialized = deserialize_function(fo);
        Halide::Func func(function_deserialized);
        funcs.push_back(func);
    }

    const auto *requirements_objs = pipeline_obj->requirements();
    const auto *requirement_type_objs = pipeline_obj->requirements_type();

    std::vector<Halide::Internal::Stmt> requirements;
    requirements.reserve(requirements_objs->size());
    for (size_t i = 0; i < requirements_objs->size(); ++i) {
        auto requirement_deserialized = deserialize_stmt(requirement_type_objs->Get(i), requirements_objs->Get(i));
        requirements.push_back(requirement_deserialized);
    }
    return Halide::Pipeline(funcs);
}
