#include <cstring>
#include <functional>
#include <vector>

#include "util/model_runner.h"

int main(int argc, char **argv) {
    using hannk::ModelRunner;

    ModelRunner runner;

    // Default the exernal delegate to disabled, since it may
    // need extra setup to work (eg LD_LIBRARY_PATH or --external_delegate_path)
    runner.do_run[ModelRunner::kExternalDelegate] = false;

    std::vector<std::string> files_to_process;
    int r = runner.parse_flags(argc, argv, files_to_process);
    if (r != 0) {
        return r;
    }

    runner.status();

    for (auto f : files_to_process) {
        runner.run(f);
        halide_profiler_report(nullptr);
        halide_profiler_reset();
    }
    return 0;
}
