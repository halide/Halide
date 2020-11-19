#include <chrono>
#include <fstream>
#include <iostream>

#include "app_util.h"
#include "halide_benchmark.h"
#include "interpreter.h"
#include "tflite_parser.h"

using app_util::read_entire_file;

namespace interpret_nn {

void RunBenchmark(const std::string &filename, const ScheduleOptions &options) {
    std::cout << "Benchmarking " << filename << std::endl;

    std::vector<char> buffer = read_entire_file(filename);
    Model model = parse_tflite_model_from_buffer(buffer.data());

    if (options.verbose) {
        model.dump(std::cout);
    }

    ModelInterpreter interpreter(std::move(model), options);

    auto result = Halide::Tools::benchmark([&]() { interpreter.execute(); });
    std::cout << "Time: " << result.wall_time * 1e6 << " us" << std::endl;

    if (options.verbose) {
        std::cout << "Outputs:\n";
        std::vector<Tensor *> outputs = interpreter.outputs();
        for (Tensor *t : outputs) {
            APP_CHECK(t);
            std::cout << "  \"" << t->name() << "\" : " << to_string(t->type()) << " x " << t->shape() << "\n";
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
