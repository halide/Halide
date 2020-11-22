#include <chrono>
#include <fstream>
#include <iostream>

#include "HalideRuntime.h"

#include "error_util.h"
#include "file_util.h"
#include "halide_benchmark.h"
#include "interpreter.h"
#include "tflite_parser.h"

namespace interpret_nn {

void run_benchmark(const std::string &filename, const ScheduleOptions &options) {
    if (!options.trace) {
        // In trace mode, don't send *anything* to stdout
        std::cout << "Benchmarking " << filename << std::endl;
    }

    std::vector<char> buffer = read_entire_file(filename);
    Model model = parse_tflite_model_from_buffer(buffer.data());

    if (options.verbose) {
        model.dump(std::cout);
    }

    ModelInterpreter interpreter(std::move(model), options);

    if (!options.trace) {
        auto result = Halide::Tools::benchmark([&]() { interpreter.execute(); });
        std::cout << "Time: " << result.wall_time * 1e6 << " us" << std::endl;

        halide_profiler_report(nullptr);
        halide_profiler_reset();
    } else {
        interpreter.execute();
    }

    if (options.verbose) {
        std::cout << "Outputs:\n";
        std::vector<Tensor *> outputs = interpreter.outputs();
        for (Tensor *t : outputs) {
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
        if (!strcmp(argv[i], "--trace")) {
            options.trace = true;
            continue;
        }
        // TODO: Make this a numeric parameter.
        if (!strcmp(argv[i], "--working_set")) {
            options.target_working_set_size_bytes = 1024 * 512;
            continue;
        }
    }

    if (options.verbose && options.trace) {
        std::cerr << "You cannot specify --trace and --verbose at the same time.\n";
        exit(-1);
    }

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--", 2)) {
            continue;
        }
        interpret_nn::run_benchmark(argv[i], options);
        std::cout << std::endl;
    }

    std::cout << "Done!\n";
    return 0;
}
