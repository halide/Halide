#include <chrono>
#include <fstream>
#include <iostream>

#include "halide_app_assert.h"
#include "interpret_nn.h"
#include "tflite_parser.h"
#include "tflite_schema_generated.h"

namespace interpret_nn {

void RunBenchmark(const std::string &filename) {
    std::cout << "Benchmarking " << filename << std::endl;

    std::string output;
    std::ifstream file(filename);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(&buffer[0], size);

    Model model = ParseTfLiteModel(tflite::GetModel(buffer.data()));

    model.Dump(std::cout);
    for (auto &i : model.tensors) {
        i->allocate();
    }

    ModelInterpreter interpreter(&model);

    auto begin = std::chrono::high_resolution_clock::now();
    auto end = begin;
    int loops = 0;
    do {
        interpreter.Execute();
        loops++;
        end = std::chrono::high_resolution_clock::now();
    } while (end - begin < std::chrono::seconds(1));
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::microseconds>((end - begin) / loops).count() << " us" << std::endl;
}

}  // namespace interpret_nn

int main(int argc, char **argv) {

    for (int i = 1; i < argc; i++) {
        interpret_nn::RunBenchmark(argv[i]);
    }

    return 0;
}
