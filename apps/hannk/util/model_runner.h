#ifndef HANNK_MODEL_RUNNER_H
#define HANNK_MODEL_RUNNER_H

#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

#include "util/buffer_util.h"
#include "util/file_util.h"

struct TfLiteDelegate;
struct TfLiteInterpreter;
struct TfLiteInterpreterOptions;
struct TfLiteModel;

namespace hannk {

struct FlagProcessor {
    using Fn = std::function<int(const std::string &)>;
    using FnMap = std::map<std::string, Fn>;

    std::map<std::string, Fn> flag_handlers;
    Fn nonflag_handler = handle_nonflag;
    Fn unknown_flag_handler = handle_unknown_flag;
    Fn missing_value_handler = handle_missing_value;

    // Returns 0 for success, nonzero for fatal error.
    int process(int argc, char **argv) const;

    // default impls
    static int handle_nonflag(const std::string &);
    static int handle_unknown_flag(const std::string &);
    static int handle_missing_value(const std::string &);

    // Movable but not copyable.
    FlagProcessor() = default;
    FlagProcessor(const FlagProcessor &) = delete;
    FlagProcessor &operator=(const FlagProcessor &) = delete;
    FlagProcessor(FlagProcessor &&) = delete;
    FlagProcessor &operator=(FlagProcessor &&) = delete;
};

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
    TfLiteModelRunner(const ReadOnlyFileView &file_view,
                      int threads,
                      SeedTracker &seed_tracker,
                      std::ostream *verbose_output,
                      TfLiteDelegate *delegate);
    void run_once();
    std::vector<HalideBuffer<const void>> copy_outputs();
    ~TfLiteModelRunner();

    static void ErrorReporter(void *user_data, const char *format, va_list args);

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
    bool keep_going = false;
    bool use_mmap = false;  // TODO: should this default to true?
    double tolerance;
    std::string external_delegate_path;

    ModelRunner();

    int parse_flags(int argc, char **argv, std::vector<std::string> &files_to_process);

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
    RunResult run_in_hannk(const ReadOnlyFileView &file_view);
    RunResult run_in_tflite(const ReadOnlyFileView &file_view, TfLiteDelegate *delegate = nullptr);
    bool compare_results(const std::string &name_a, const std::string &name_b, const RunResult &a, const RunResult &b);
};

}  // namespace hannk

#endif  // HANNK_MODEL_RUNNER_H
