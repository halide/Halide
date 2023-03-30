#include "Serializer.h"
#include <iostream>

void Serializer::serialize(const Pipeline& pipeline, const std::string& filename) {
    std::cout << "serialize func\n";
    std::cout << "pipeline funcs: " << pipeline.outputs().size() << "\n";
    std::cout << pipeline.outputs().back().name() << "\n";

}

