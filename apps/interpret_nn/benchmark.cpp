#include <chrono>
#include <fstream>
#include <iostream>

#include "app_util.h"
#include "interpreter.h"
#include "tflite_parser.h"

using app_util::ReadEntireFile;

namespace interpret_nn {

void RunBenchmark(const std::string &filename, bool verbose) {
    std::cout << "Benchmarking " << filename << std::endl;

    std::vector<char> buffer = ReadEntireFile(filename);
    Model model = ParseTfLiteModelFromBuffer(buffer.data());

    if (verbose) {
        model.Dump(std::cout);
    }

    ScheduleOptions options;
    options.verbose = verbose;
    ModelInterpreter interpreter(std::move(model), options);

    auto begin = std::chrono::high_resolution_clock::now();
    auto end = begin;
    int loops = 0;
    do {
        interpreter.Execute();
        loops++;
        end = std::chrono::high_resolution_clock::now();
    } while (end - begin < std::chrono::seconds(1));
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::microseconds>((end - begin) / loops).count() << " us" << std::endl;

    if (verbose) {
        std::cout << "Outputs:\n";
        std::vector<Tensor *> outputs = interpreter.Outputs();
        for (Tensor *t : outputs) {
            APP_CHECK(t);
            std::cout << "  \"" << t->Name() << "\" : " << TensorTypeToString(t->Type()) << " x " << t->Shape() << "\n";
        }
    }
}

}  // namespace interpret_nn

int main(int argc, char **argv) {
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose")) {
            verbose = true;
            continue;
        }
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose")) {
            continue;
        }
        interpret_nn::RunBenchmark(argv[i], verbose);
        std::cout << std::endl;
    }

    std::cout << "Done!\n";
    return 0;
}
