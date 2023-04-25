#include "Serializer.h"
#include <iostream>
#include <fstream>
#include <map>

using Halide::Internal::Function;

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
    // TODO: test: only one func
    assert(env.size() == 1);
    for (auto it = env.begin(); it != env.end(); ++it) {
        std::cout << it->second.name() << " " << it->second.origin_name() << "\n";
    }

    // std::vector<flatbuffers::
    // serialize each func
    std::vector<flatbuffers::Offset<Halide::Serdes::Func>> func_vector;
    for (auto it = env.begin(); it != env.end(); ++it) {
        auto name = builder.CreateString(it->second.name());
        auto origin_name =  builder.CreateString(it->second.origin_name());
        auto func = Halide::Serdes::CreateFunc(builder, name, origin_name);
        func_vector.push_back(func);
    }
    auto funcs = builder.CreateVector(func_vector);
    
    auto pipeline_obj = Halide::Serdes::CreatePipeline(builder, funcs);
    builder.Finish(pipeline_obj);
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

