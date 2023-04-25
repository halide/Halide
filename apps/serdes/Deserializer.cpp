#include "Deserializer.h"
#include <iostream>
#include <fstream>

Pipeline Deserializer::deserialize(const std::string& filename) {
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
      auto name = fo->name()->str();
      auto origin_name = fo->origin_name()->str();
      std::cout << "debug deserializer: name " << name << " orig_name: " << origin_name << "\n";
      Halide::Internal::Function function(name, origin_name);
      Halide::Func func(function);
      funcs.push_back(func);
    }
    return Pipeline(funcs);
}

