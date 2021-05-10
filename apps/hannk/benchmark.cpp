#include <chrono>
#include <fstream>
#include <iostream>

#include "HalideRuntime.h"

#include "halide_benchmark.h"
#include "interpreter/interpreter.h"
#include "tflite/tflite_parser.h"
#include "util/error_util.h"
#include "util/file_util.h"

namespace hannk {

void run_benchmark(const std::string &filename, const InterpreterOptions &options) {
    if (!options.trace) {
        // In trace mode, don't send *anything* to stdout
        std::cout << filename;
    }

    std::vector<char> buffer = read_entire_file(filename);
    std::unique_ptr<OpGroup> model = parse_tflite_model_from_buffer(buffer.data());

    if (options.verbose) {
        model->dump(std::cout);
    }

    Interpreter interpreter(std::move(model), options);

    if (!options.trace) {
        auto result = Halide::Tools::benchmark([&]() { interpreter.execute(); });
        std::cout << ": " << result.wall_time * 1e6 << " us" << std::endl;

        halide_profiler_report(nullptr);
        halide_profiler_reset();
    } else {
        std::cout << std::endl;
        interpreter.execute();
    }
}

}  // namespace hannk

int main(int argc, char **argv) {
    hannk::InterpreterOptions options;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose")) {
            options.verbose = true;
            continue;
        }
        if (!strcmp(argv[i], "--trace")) {
            options.trace = true;
            continue;
        }
        if (argv[i][0] == '-') {
            HLOG(ERROR) << "Unknown flag: " << argv[i] << ".\n";
            exit(-1);
        }
    }

    if (options.verbose && options.trace) {
        HLOG(ERROR) << "You cannot specify --trace and --verbose at the same time.\n";
        exit(-1);
    }

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--", 2)) {
            continue;
        }
        hannk::run_benchmark(argv[i], options);
    }

    std::cout << "Done!\n";
    return 0;
}
