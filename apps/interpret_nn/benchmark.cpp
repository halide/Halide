#include <chrono>
#include <fstream>
#include <iostream>

#include "halide_app_assert.h"
#include "interpret_nn.h"
#include "tflite_parser.h"
#include "tflite_schema_generated.h"

namespace interpret_nn {

namespace {

std::vector<char> ReadEntireFile(const std::string &filename) {
    std::vector<char> result;

    std::ifstream f(filename, std::ios::in | std::ios::binary);
    halide_app_assert(f.is_open()) << "Unable to open file: " << filename;
    f.seekg(0, std::ifstream::end);
    size_t size = f.tellg();
    result.resize(size);
    f.seekg(0, std::ifstream::beg);
    f.read(result.data(), result.size());
    halide_app_assert(f.good()) << "Unable to read file: " << filename;
    f.close();

    return result;
}

}  // namespace

void RunBenchmark(const std::string &filename) {
    std::cout << "Benchmarking " << filename << std::endl;

    std::vector<char> buffer = ReadEntireFile(filename);
    Model model = ParseTfLiteModel(tflite::GetModel(buffer.data()));

    model.Dump(std::cout);
    for (auto &i : model.tensors) {
        i->Allocate();
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

    std::cout << "Done!\n";
    return 0;
}
