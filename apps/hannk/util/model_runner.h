#ifndef HANNK_MODEL_RUNNER_H
#define HANNK_MODEL_RUNNER_H

#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#include "util/buffer_util.h"

struct TfLiteDelegate;

namespace hannk {

struct ModelRunner {
    enum WhichRun {
        kTfLite,
        kHannk,
        kExternalDelegate,
        kInternalDelegate,

        kNumRuns  // keep last
    };

    int seed = 0;
    int threads = 1;
    int verbosity = 0;
    bool do_run[kNumRuns];  // no way to default-init everything to anything but zero, alas
    bool do_benchmark = true;
    bool do_compare_results = true;
    double tolerance;
    std::string external_delegate_path;
    std::ostream *outstream = nullptr;
    std::ostream *errstream = nullptr;

    ModelRunner();
    void run(const std::string &filename);

    // Movable but not copyable.
    ModelRunner(const ModelRunner &) = delete;
    ModelRunner &operator=(const ModelRunner &) = delete;
    ModelRunner(ModelRunner &&) = delete;
    ModelRunner &operator=(ModelRunner &&) = delete;

private:
    std::map<std::string, int> seeds_;

    int seed_for_name(const std::string &name);

    struct RunResult {
        std::vector<HalideBuffer<const void>> outputs;
        std::chrono::duration<double> time{0};
    };
    RunResult run_in_hannk(const std::vector<char> &buffer);
    RunResult run_in_tflite(const std::vector<char> &buffer, TfLiteDelegate *delegate = nullptr);
    bool compare_results(const std::string &msg, const RunResult &a, const RunResult &b);
};

}  // namespace hannk

#endif  // HANNK_MODEL_RUNNER_H
