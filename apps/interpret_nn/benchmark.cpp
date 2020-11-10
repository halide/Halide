#include <chrono>
#include <fstream>
#include <iostream>

#include "app_util.h"
#include "interpreter.h"
#include "tflite_parser.h"

using app_util::ReadEntireFile;

namespace interpret_nn {

void RunBenchmark(const std::string &filename) {
    std::cout << "Benchmarking " << filename << std::endl;

    std::vector<char> buffer = ReadEntireFile(filename);
    Model model = ParseTfLiteModelFromBuffer(buffer.data());

    model.Dump(std::cout);
    for (auto &i : model.tensors) {
        i->Allocate();
    }

    ScheduleOptions options;
    options.verbose = true;
    ModelInterpreter interpreter(&model, options);

    auto begin = std::chrono::high_resolution_clock::now();
    auto end = begin;
    int loops = 0;
    do {
        interpreter.Execute();
        loops++;
        end = std::chrono::high_resolution_clock::now();
    } while (end - begin < std::chrono::seconds(1));
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::microseconds>((end - begin) / loops).count() << " us" << std::endl;

    std::cout << "Outputs:\n";
    std::vector<Tensor *> outputs = interpreter.Outputs();
    for (Tensor *t : outputs) {
        APP_CHECK(t);
        std::cout << "  \"" << t->Name() << "\" : " << TensorTypeToString(t->Type()) << " x " << t->Shape() << "\n";
    }
}

}  // namespace interpret_nn

int main(int argc, char **argv) {

    for (int i = 1; i < argc; i++) {
        interpret_nn::RunBenchmark(argv[i]);
    }

    std::cout << "Done!\n";
    return 0;
}
