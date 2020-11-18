#include <chrono>
#include <fstream>
#include <iostream>

#include "app_util.h"
#include "interpreter.h"
#include "tflite_parser.h"

using app_util::ReadEntireFile;

namespace interpret_nn {

void RunBenchmark(const std::string &filename, const ScheduleOptions &options) {
    std::cout << "Benchmarking " << filename << std::endl;

    std::vector<char> buffer = ReadEntireFile(filename);
    Model model = ParseTfLiteModelFromBuffer(buffer.data());

    if (options.verbose) {
        model.Dump(std::cout);
    }

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

    if (options.verbose) {
        std::cout << "Outputs:\n";
        std::vector<Tensor *> outputs = interpreter.Outputs();
        for (Tensor *t : outputs) {
            APP_CHECK(t);
            std::cout << "  \"" << t->Name() << "\" : " << to_string(t->Type()) << " x " << t->Shape() << "\n";
        }
    }
}

}  // namespace interpret_nn

int main(int argc, char **argv) {
    interpret_nn::ScheduleOptions options;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose")) {
            options.verbose = true;
            continue;
        }
        // TODO: Make this a numeric parameter.
        if (!strcmp(argv[i], "--working_set")) {
            options.target_working_set_size_bytes = 1024 * 512;
            continue;
        }
    }

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--", 2)) {
            continue;
        }
        interpret_nn::RunBenchmark(argv[i], options);
        std::cout << std::endl;
    }

    std::cout << "Done!\n";
    return 0;
}
