#include <chrono>
#include <fstream>
#include <iostream>

#include "app_util.h"
#include "interpreter.h"
#include "tflite_parser.h"

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/model.h"
//#include "tensorflow/lite/kernels/register.h"

using app_util::ReadEntireFile;

namespace interpret_nn {

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
