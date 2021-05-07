#ifndef HANNK_MODEL_RUNNER_H
#define HANNK_MODEL_RUNNER_H

#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#include "util/buffer_util.h"

struct TfLiteDelegate;
struct TfLiteInterpreter;
struct TfLiteInterpreterOptions;
struct TfLiteModel;

namespace hannk {

struct SeedTracker {
    SeedTracker() = default;

    void reset(int seed);
    int next_seed() const;
    int seed_for_name(const std::string &name);
private:
    int next_seed_ = 0;
    std::map<std::string, int> seeds_;

    // Movable but not copyable.
    SeedTracker(const SeedTracker &) = delete;
    SeedTracker &operator=(const SeedTracker &) = delete;
    SeedTracker(SeedTracker &&) = delete;
    SeedTracker &operator=(SeedTracker &&) = delete;
};

class TfLiteModelRunner {
    TfLiteModel *tf_model_ = nullptr;
    TfLiteInterpreterOptions *tf_options_ = nullptr;
    TfLiteInterpreter *tf_interpreter_ = nullptr;
    std::ostream *verbose_output_ = nullptr;

public:
    TfLiteModelRunner(const std::vector<char> &buffer,
                      int threads,
                      SeedTracker &seed_tracker,
                      std::ostream *verbose_output,
                      TfLiteDelegate *delegate);
    void run_once();
    std::vector<HalideBuffer<const void>> copy_outputs();
    ~TfLiteModelRunner();

    // Movable but not copyable.
    TfLiteModelRunner(const TfLiteModelRunner &) = delete;
    TfLiteModelRunner &operator=(const TfLiteModelRunner &) = delete;
    TfLiteModelRunner(TfLiteModelRunner &&) = delete;
    TfLiteModelRunner &operator=(TfLiteModelRunner &&) = delete;
};

// TODO: add a way to bottleneck stdout/stdout, or just errors/warnings in general
struct ModelRunner {
    enum WhichRun {
        kTfLite,
        kHannk,
        kExternalDelegate,
        kInternalDelegate,

        kNumRuns  // keep last
    };

    int threads = 1;
    int verbosity = 0;
    bool do_run[kNumRuns];  // no way to default-init everything to anything but zero, alas
    bool do_benchmark = true;
    bool do_compare_results = true;
    double tolerance;
    std::string external_delegate_path;

    ModelRunner();

    void set_seed(int seed);
    void status();
    void run(const std::string &filename);

    // Movable but not copyable.
    ModelRunner(const ModelRunner &) = delete;
    ModelRunner &operator=(const ModelRunner &) = delete;
    ModelRunner(ModelRunner &&) = delete;
    ModelRunner &operator=(ModelRunner &&) = delete;

private:
    SeedTracker seed_tracker_;

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
