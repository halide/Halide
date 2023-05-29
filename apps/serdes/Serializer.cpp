#include "Serializer.h"
#include <iostream>
#include <fstream>
#include <map>

using Halide::Internal::Function;

flatbuffers::Offset<flatbuffers::String> Serializer::serialize_string(flatbuffers::FlatBufferBuilder& builder, const std::string& str) {
    return builder.CreateString(str);
}

flatbuffers::Offset<Halide::Serdes::Type> Serializer::serialize_type(flatbuffers::FlatBufferBuilder& builder, const Halide::Type& type) {
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

std::pair<Halide::Serdes::Stmt, flatbuffers::Offset<void>> Serializer::serialize_stmt(flatbuffers::FlatBufferBuilder& builder, const Halide::Internal::Stmt& stmt) {
    switch  (stmt->node_type) {
        case Halide::Internal::IRNodeType::LetStmt: {
            auto let_stmt = stmt.as<Halide::Internal::LetStmt>();
            std::string name = let_stmt->name;
            auto name_serialized = serialize_string(builder, name);

            auto body = let_stmt->body;

            auto body_serialized = serialize_stmt(builder, body);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_LetStmt, Halide::Serdes::CreateLetStmt(builder, name_serialized, body_serialized.first, body_serialized.second).Union());
        }
        case Halide::Internal::IRNodeType::AssertStmt: {
            return std::make_pair(Halide::Serdes::Stmt::Stmt_AssertStmt, Halide::Serdes::CreateAssertStmt(builder).Union());
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
            auto body = for_stmt->body;
            auto body_serialized = serialize_stmt(builder, body);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_For, Halide::Serdes::CreateFor(builder, name_serialized, body_serialized.first, body_serialized.second).Union());
        }
        case Halide::Internal::IRNodeType::Store: {
            auto store_stmt = stmt.as<Halide::Internal::Store>();
            std::string name = store_stmt->name;
            auto name_serialized = serialize_string(builder, name);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Store, Halide::Serdes::CreateStore(builder, name_serialized).Union());
        }
        case Halide::Internal::IRNodeType::Provide: {
            auto provide_stmt = stmt.as<Halide::Internal::Provide>();
            std::string name = provide_stmt->name;
            auto name_serialized = serialize_string(builder, name);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Provide, Halide::Serdes::CreateProvide(builder, name_serialized).Union());
        }
        case Halide::Internal::IRNodeType::Allocate: {
            auto allocate_stmt = stmt.as<Halide::Internal::Allocate>();
            std::string name = allocate_stmt->name;
            auto name_serialized = serialize_string(builder, name);
            auto type = allocate_stmt->type;
            auto type_serialized = serialize_type(builder, type);
            auto free_function = allocate_stmt->free_function;
            auto free_function_serialized = serialize_string(builder, free_function);
            auto padding = allocate_stmt->padding;
            auto body = allocate_stmt->body;
            auto body_serialized = serialize_stmt(builder, body);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Allocate, Halide::Serdes::CreateAllocate(builder, name_serialized, type_serialized, free_function_serialized, padding, body_serialized.first, body_serialized.second).Union());
        }
        case Halide::Internal::IRNodeType::Free : {
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
            for (const auto& type : types) {
                types_serialized.push_back(serialize_type(builder, type));
            }
            auto types_vector = builder.CreateVector(types_serialized);
            auto body = realize_stmt->body;
            auto body_serialized = serialize_stmt(builder, body);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Realize, Halide::Serdes::CreateRealize(builder, name_serialized, types_vector, body_serialized.first, body_serialized.second).Union());
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
            auto then_case = if_then_else_stmt->then_case;
            auto then_case_serialized = serialize_stmt(builder, then_case);
            auto else_case = if_then_else_stmt->else_case;
            auto else_case_serialized = serialize_stmt(builder, else_case);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_IfThenElse, Halide::Serdes::CreateIfThenElse(builder, then_case_serialized.first, then_case_serialized.second, else_case_serialized.first, else_case_serialized.second).Union());
        }
        case Halide::Internal::IRNodeType::Evaluate: {
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Evaluate, Halide::Serdes::CreateEvaluate(builder).Union());
        }
        case Halide::Internal::IRNodeType::Prefetch: {
            auto prefetch_stmt = stmt.as<Halide::Internal::Prefetch>();
            std::string name = prefetch_stmt->name;
            auto name_serialized = serialize_string(builder, name);
            auto types = prefetch_stmt->types;
            std::vector<flatbuffers::Offset<Halide::Serdes::Type>> types_serialized;
            for (const auto& type : types) {
                types_serialized.push_back(serialize_type(builder, type));
            }
            auto types_vector = builder.CreateVector(types_serialized);
            auto body = prefetch_stmt->body;
            auto body_serialized = serialize_stmt(builder, body);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Prefetch, Halide::Serdes::CreatePrefetch(builder, name_serialized, types_vector, body_serialized.first, body_serialized.second).Union());
        }
        case Halide::Internal::IRNodeType::Acquire: {
            auto acquire_stmt = stmt.as<Halide::Internal::Acquire>();
            auto body = acquire_stmt->body;
            auto body_serialized = serialize_stmt(builder, body);
            return std::make_pair(Halide::Serdes::Stmt::Stmt_Acquire, Halide::Serdes::CreateAcquire(builder, body_serialized.first, body_serialized.second).Union());
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

flatbuffers::Offset<Halide::Serdes::Func> Serializer::serialize_func(flatbuffers::FlatBufferBuilder& builder, const Halide::Internal::Function& function) {
    // name
    auto name_serialized = serialize_string(builder, function.name());

    // origin_name
    auto origin_name_serialized = serialize_string(builder, function.origin_name());

    // output_types
    std::vector<Halide::Type> output_types = function.output_types();
    std::vector<flatbuffers::Offset<Halide::Serdes::Type>> output_types_serialized;
    for (const auto& type : output_types) {
        output_types_serialized.push_back(serialize_type(builder, type));
    }
    auto output_types_vector = builder.CreateVector(output_types_serialized);

    // required_types
    std::vector<Halide::Type> required_types = function.required_types();
    std::vector<flatbuffers::Offset<Halide::Serdes::Type>> required_types_serialized;
    for (const auto& type : required_types) {
        required_types_serialized.push_back(serialize_type(builder, type));
    }
    auto required_types_vector = builder.CreateVector(required_types_serialized);


    // required_dimensions
    int required_dim = function.required_dimensions();

    // args
    std::vector<std::string> args = function.args();
    std::vector<flatbuffers::Offset<flatbuffers::String>> args_serialized;
    for (const auto& arg : args) {
        args_serialized.push_back(serialize_string(builder, arg));
    }
    auto args_vector = builder.CreateVector(args_serialized);

    auto func = Halide::Serdes::CreateFunc(builder, name_serialized, origin_name_serialized, output_types_vector, required_types_vector, required_dim, args_vector);
    return func;
}


void Serializer::serialize(const Pipeline& pipeline, const std::string& filename) {
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
    for (const auto& func : outpus) {
        funcs_serialized.push_back(this->serialize_func(builder, func.function()));
    }
    auto funcs = builder.CreateVector(funcs_serialized);


    // requirements
    auto requirements = pipeline.requirements();
    std::vector<flatbuffers::Offset<void>> requirements_serialized;
    std::vector<uint8_t> requirements_types;
    for (const auto& stmt: requirements) {
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

