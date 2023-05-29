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
    return Pipeline(funcs);
}

