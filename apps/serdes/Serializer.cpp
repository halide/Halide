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

    // required_types
    auto output_types_vector = builder.CreateVector(output_types_serialized);
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

    auto pipeline_obj = Halide::Serdes::CreatePipeline(builder, funcs);
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

