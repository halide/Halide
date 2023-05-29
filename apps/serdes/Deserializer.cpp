#include "Deserializer.h"
#include <iostream>
#include <fstream>

std::string Deserializer::deserialize_string(const flatbuffers::String* str) {
    return str->str();
}

Halide::Type Deserializer::deserialize_type(const Halide::Serdes::Type* type) {
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

Halide::Internal::Function Deserializer::deserialize_function(const Halide::Serdes::Func* function) {
    // name
    std::string name = deserialize_string(function->name());

    // origin_name
    std::string origin_name = deserialize_string(function->origin_name());

    // output_types
    std::vector<Halide::Type> output_types;
    auto output_types_serialized = function->output_types();
    for (const auto& type : *output_types_serialized) {
        output_types.push_back(deserialize_type(type));
    }

    // required_types
    std::vector<Halide::Type> required_types;
    auto required_types_serialized = function->required_types();
    for (const auto& type : *required_types_serialized) {
        required_types.push_back(deserialize_type(type));
    }

    // required_dimensions
    int required_dim = function->required_dims();

    // args
    std::vector<std::string> args;
    auto args_serialized = function->args();
    for (const auto& arg : *args_serialized) {
        args.push_back(deserialize_string(arg));
    }

    // assemble the function
    return Halide::Internal::Function(name, origin_name, output_types, required_types, required_dim, args);
}

Halide::Internal::Stmt Deserializer::deserialize_stmt(uint8_t type_code, const void * stmt) {
    switch (type_code) {
        case Halide::Serdes::Stmt_LetStmt : {
            const Halide::Serdes::LetStmt* let_stmt = (const Halide::Serdes::LetStmt *)stmt;
            auto name = deserialize_string(let_stmt->name());
            auto body = deserialize_stmt(let_stmt->body_type(), let_stmt->body());
            return Halide::Internal::LetStmt::make(name, Expr(), body);
        }
        case Halide::Serdes::Stmt_AssertStmt : {
            // Halide::Serdes::AssertStmt* assert_stmt = (Halide::Serdes::AssertStmt *)stmt;
            return Halide::Internal::AssertStmt::make(Expr(), Expr());
        }
        case Halide::Serdes::Stmt_ProducerConsumer : {
            const Halide::Serdes::ProducerConsumer* producer_consumer = (const Halide::Serdes::ProducerConsumer *)stmt;
            auto name = deserialize_string(producer_consumer->name());
            auto is_producer = producer_consumer->is_producer();
            auto body = deserialize_stmt(producer_consumer->body_type(), producer_consumer->body());
            return Halide::Internal::ProducerConsumer::make(name, is_producer, body);
        }
        case Halide::Serdes::Stmt_For : {
            const Halide::Serdes::For* for_stmt = (const Halide::Serdes::For *)stmt;
            auto name = deserialize_string(for_stmt->name());
            auto body = deserialize_stmt(for_stmt->body_type(), for_stmt->body());
            return Halide::Internal::For::make(name, Expr(), Expr(), Halide::Internal::ForType::Vectorized, Halide::DeviceAPI::None, body);
        }
        case Halide::Serdes::Stmt_Store : {
            const Halide::Serdes::Store* store_stmt = (const Halide::Serdes::Store *)stmt;
            auto name = deserialize_string(store_stmt->name());
            return Halide::Internal::Store::make(name, Expr(), Expr(), Halide::Internal::Parameter(), Expr(), Halide::Internal::ModulusRemainder());
        }
        case Halide::Serdes::Stmt_Provide : {
            const Halide::Serdes::Provide* provide_stmt = (const Halide::Serdes::Provide *)stmt;
            auto name = deserialize_string(provide_stmt->name());
            return Halide::Internal::Provide::make(name, std::vector<Expr>(), std::vector<Expr>(), Expr());
        }
        case Halide::Serdes::Stmt_Allocate : {
            const Halide::Serdes::Allocate* allocate_stmt = (const Halide::Serdes::Allocate *)stmt;
            auto name = deserialize_string(allocate_stmt->name());
            auto type = deserialize_type(allocate_stmt->type());
            auto free_function = deserialize_string(allocate_stmt->free_function());
            auto padding = allocate_stmt->padding();
            auto body = deserialize_stmt(allocate_stmt->body_type(), allocate_stmt->body());
            return Halide::Internal::Allocate::make(name, type, Halide::MemoryType::Auto,  std::vector<Expr>(), Expr(), body, Expr(), free_function, padding);
        }
        case Halide::Serdes::Stmt_Free : {
            const Halide::Serdes::Free* free_stmt = (const Halide::Serdes::Free *)stmt;
            auto name = deserialize_string(free_stmt->name());
            return Halide::Internal::Free::make(name);
        }
        case Halide::Serdes::Stmt_Realize : {
            const Halide::Serdes::Realize* realize_stmt = (const Halide::Serdes::Realize *)stmt;
            auto name = deserialize_string(realize_stmt->name());
            std::vector<Type> types;
            for (const auto& type : *realize_stmt->types()) {
                types.push_back(deserialize_type(type));
            }
            auto body = deserialize_stmt(realize_stmt->body_type(), realize_stmt->body());
            return Halide::Internal::Realize::make(name, types, Halide::MemoryType::Auto, Halide::Region(), Expr(), body);
        }
        case Halide::Serdes::Stmt_Block : {
            const Halide::Serdes::Block* block_stmt = (const Halide::Serdes::Block *)stmt;
            auto first = deserialize_stmt(block_stmt->first_type(), block_stmt->first());
            auto rest = deserialize_stmt(block_stmt->rest_type(), block_stmt->rest());
            return Halide::Internal::Block::make(first, rest);
        }
        case Halide::Serdes::Stmt_IfThenElse : {
            const Halide::Serdes::IfThenElse* if_then_else_stmt = (const Halide::Serdes::IfThenElse *)stmt;
            auto then_case = deserialize_stmt(if_then_else_stmt->then_case_type(), if_then_else_stmt->then_case());
            auto else_case = deserialize_stmt(if_then_else_stmt->else_case_type(), if_then_else_stmt->else_case());
            return Halide::Internal::IfThenElse::make(Expr(), then_case, else_case);
        }
        case Halide::Serdes::Stmt_Evaluate : {
            // Halide::Serdes::Evaluate* evaluate_stmt = (Halide::Serdes::Evaluate *)stmt;
            return Halide::Internal::Evaluate::make(Expr());
        }
        case Halide::Serdes::Stmt_Prefetch : {
            const Halide::Serdes::Prefetch* prefetch_stmt = (const Halide::Serdes::Prefetch *)stmt;
            auto name = deserialize_string(prefetch_stmt->name());
            std::vector<Type> types;
            for (const auto& type : *prefetch_stmt->types()) {
                types.push_back(deserialize_type(type));
            }
            auto body = deserialize_stmt(prefetch_stmt->body_type(), prefetch_stmt->body());
            return Halide::Internal::Prefetch::make(name, types, Region(),
                    Halide::Internal::PrefetchDirective(),
                    Expr(), body);
        }
        case Halide::Serdes::Stmt_Acquire : {
            const Halide::Serdes::Acquire* acquire_stmt = (const Halide::Serdes::Acquire *)stmt;
            auto body = deserialize_stmt(acquire_stmt->body_type(), acquire_stmt->body());
            return Halide::Internal::Acquire::make(Expr(), Expr(), body);
        }
        case Halide::Serdes::Stmt_Fork : {
            const Halide::Serdes::Fork* fork_stmt = (const Halide::Serdes::Fork *)stmt;
            auto first = deserialize_stmt(fork_stmt->first_type(), fork_stmt->first());
            auto rest = deserialize_stmt(fork_stmt->rest_type(), fork_stmt->rest());
            return Halide::Internal::Fork::make(first, rest);
        }
        case Halide::Serdes::Stmt_Atomic : {
            const Halide::Serdes::Atomic* atomic_stmt = (const Halide::Serdes::Atomic *)stmt;
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

Pipeline Deserializer::deserialize(const std::string& filename) {
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
    
    const auto* pipeline_obj = Halide::Serdes::GetPipeline(data);
    const auto* func_objs = pipeline_obj->outputs();
    std::vector<Halide::Func> funcs;
    for (const auto& fo: *func_objs) {
        auto function_deserialized = deserialize_function(fo);
        Halide::Func func(function_deserialized);
        funcs.push_back(func);
    }

    const auto* requirements_objs = pipeline_obj->requirements();
    const auto* requirement_type_objs = pipeline_obj->requirements_type();

    std::vector<Halide::Internal::Stmt> requirements;
    for (size_t i = 0; i < requirements_objs->size(); ++i) {
        auto requirement_deserialized = deserialize_stmt(requirement_type_objs->Get(i), requirements_objs->Get(i));
        requirements.push_back(requirement_deserialized);
    }
    return Pipeline(funcs);
}

