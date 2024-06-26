#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

#include "cmdline.h"

#include "DefaultCostModel.h"
#include "HalideBuffer.h"
#include "NetworkSize.h"

namespace {

using namespace Halide;

using Halide::Runtime::Buffer;
using std::map;
using std::string;
using std::vector;

struct Flags {
    int epochs = 0;
    std::vector<float> rates = {0.0001f};
    string initial_weights_path;
    string weights_out_path;
    int num_cores = 32;
    bool reset_weights = false;
    bool randomize_weights = false;
    string best_benchmark_path;
    string best_schedule_path;
    string predictions_file;
    bool verbose;
    bool partition_schedules;
    int limit;

    Flags(int argc, char **argv) {
        cmdline::parser a;

        const char *kNoDesc = "";

        constexpr bool kOptional = false;
        a.add<int>("epochs");
        a.add<string>("rates");
        a.add<string>("initial_weights", '\0', kNoDesc, kOptional, "");
        a.add<string>("weights_out");
        a.add<bool>("reset_weights", '\0', kNoDesc, kOptional, false);
        a.add<bool>("randomize_weights", '\0', kNoDesc, kOptional, false);
        a.add<int>("num_cores");
        a.add<string>("best_benchmark");
        a.add<string>("best_schedule");
        a.add<string>("predictions_file");
        a.add<bool>("verbose");
        a.add<bool>("partition_schedules");
        a.add<int>("limit");

        a.parse_check(argc, argv);  // exits if parsing fails

        epochs = a.get<int>("epochs");
        rates = parse_floats(a.get<string>("rates"));
        initial_weights_path = a.get<string>("initial_weights");
        weights_out_path = a.get<string>("weights_out");
        reset_weights = a.exist("reset_weights") && a.get<bool>("reset_weights");
        randomize_weights = a.exist("randomize_weights") && a.get<bool>("randomize_weights");
        best_benchmark_path = a.get<string>("best_benchmark");
        best_schedule_path = a.get<string>("best_schedule");
        predictions_file = a.get<string>("predictions_file");
        verbose = a.exist("verbose") && a.get<bool>("verbose");
        partition_schedules = a.exist("partition_schedules") && a.get<bool>("partition_schedules");
        limit = a.get<int>("limit");

        if (!reset_weights && epochs <= 0) {
            std::cerr << "--epochs must be specified and > 0.\n";
            std::cerr << a.usage();
            exit(1);
        }
        if (!reset_weights && (!initial_weights_path.empty()) == randomize_weights) {
            std::cerr << "You must specify exactly one of --initial_weights or --randomize_weights.\n";
            std::cerr << a.usage();
            exit(1);
        }
        if (weights_out_path.empty()) {
            std::cerr << "--weights_out must be specified.\n";
            std::cerr << a.usage();
            exit(1);
        }
        if (!reset_weights && rates.empty()) {
            std::cerr << "--rates cannot be empty.\n";
            std::cerr << a.usage();
            exit(1);
        }
    }

    std::vector<float> parse_floats(const std::string &s) {
        const char *c = s.c_str();
        std::vector<float> v;
        while (isspace(*c)) {
            ++c;
        }
        while (*c) {
            string f;
            while (*c && !isspace(*c)) {
                f += *c++;
            }
            v.push_back(std::atof(f.c_str()));
            while (isspace(*c)) {
                ++c;
            }
        }
        return v;
    }
};

constexpr int kModels = 1;

struct Sample {
    vector<float> runtimes;  // in msec
    double prediction[kModels];
    string filename;
    int32_t schedule_id;
    uint64_t schedule_hash;
    Buffer<float> schedule_features;
};

struct PipelineData {
    int32_t pipeline_id;
    int32_t num_stages;
    Buffer<float> pipeline_features;
    uint64_t pipeline_hash;
};

struct PipelineSample {
    map<uint64_t, Sample> schedules;
    uint64_t fastest_schedule_hash;
    float fastest_runtime{1e30f};  // in msec
};

uint64_t hash_floats(uint64_t h, const float *begin, const float *end) {
    while (begin != end) {
        uint32_t bits = *((const uint32_t *)begin);
        // From boost
        h ^= (bits + 0x9e3779b9 + (h << 6) + (h >> 2));
        begin++;
    }
    return h;
}

bool ends_with(const string &str, const string &suffix) {
    if (str.size() < suffix.size()) {
        return false;
    }
    size_t off = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (str[off + i] != suffix[i]) {
            return false;
        }
    }
    return true;
}

string leaf(const string &path) {
    size_t slash_pos = path.rfind('/');
#ifdef _WIN32
    if (slash_pos == string::npos) {
        // Windows is a thing
        slash_pos = path.rfind('\\');
    }
#endif
    if (slash_pos != string::npos) {
        return path.substr(slash_pos + 1);
    } else {
        return path;
    }
}

// Load all the samples, reading filenames from stdin
size_t load_samples(map<int, PipelineSample> &training_set, map<int, PipelineSample> &validation_set, map<int, PipelineData> &pipelines, const Flags &flags, bool predict_only) {
    vector<float> scratch(10 * 1024 * 1024);

    int best = -1;
    float best_runtime = 1e20f;
    string best_path;

    size_t num_read = 0, num_unique = 0;
    auto start = Clock::now();
    std::cout << "Loading samples...\n";
    while (!std::cin.eof()) {
        string s;
        std::cin >> s;
        if (s.empty()) {
            std::cout << "Empty: " << s << "\n";
            continue;
        }
        if (!ends_with(s, ".sample")) {
            std::cout << "Skipping file: " << s << "\n";
            continue;
        }
        std::ifstream file(s);
        file.read((char *)(scratch.data()), scratch.size() * sizeof(float));
        const size_t floats_read = file.gcount() / sizeof(float);
        const size_t num_features = floats_read - 3;
        const size_t features_per_stage = head2_w + (head1_w + 1) * head1_h;
        file.close();
        // Note we do not check file.fail(). The various failure cases
        // are handled below by checking the number of floats read. We
        // expect truncated files if the benchmarking or
        // autoscheduling procedure crashes and want to filter them
        // out with a warning.

        if (floats_read == scratch.size()) {
            std::cout << "Too-large sample: " << s << " " << floats_read << "\n";
            continue;
        }
        if (num_features % features_per_stage != 0) {
            if (flags.verbose) {
                std::cout << "Truncated sample: " << s << " " << floats_read << "\n";
            }
            continue;
        }
        const size_t num_stages = num_features / features_per_stage;

        const float runtime = scratch[num_features];
        if (runtime > 100000) {  // Don't try to predict runtime over 100s
            std::cout << "Implausible runtime in ms: " << runtime << "\n";
            continue;
        }
        // std::cout << "Runtime: " << runtime << "\n";

        int pipeline_id = *((int32_t *)(&scratch[num_features + 1]));
        const int schedule_id = *((int32_t *)(&scratch[num_features + 2]));

        if (runtime < best_runtime) {
            best_runtime = runtime;
            best = schedule_id;
            best_path = s;
        }

        PipelineData &p = pipelines[pipeline_id];

        if (p.pipeline_features.data() == nullptr) {
            p.pipeline_id = pipeline_id;
            p.num_stages = (int)num_stages;
            p.pipeline_features = Runtime::Buffer<float>(head1_w, head1_h, num_stages);
            for (size_t i = 0; i < num_stages; i++) {
                for (int x = 0; x < head1_w; x++) {
                    for (int y = 0; y < head1_h; y++) {
                        float f = scratch[i * features_per_stage + (x + 1) * 7 + y + head2_w];
                        if (f < 0 || std::isnan(f)) {
                            std::cout << "Negative or NaN pipeline feature: " << x << " " << y << " " << i << " " << f << "\n";
                        }
                        p.pipeline_features(x, y, i) = f;
                    }
                }
            }

            p.pipeline_hash = hash_floats(0, p.pipeline_features.begin(), p.pipeline_features.end());
        }

        uint64_t schedule_hash = 0;
        for (size_t i = 0; i < num_stages; i++) {
            schedule_hash = hash_floats(schedule_hash,
                                        &scratch[i * features_per_stage],
                                        &scratch[i * features_per_stage + head2_w]);
        }

        uint64_t hash = flags.partition_schedules ? schedule_hash : p.pipeline_hash;

        // Whether or not a pipeline/schedule is part of the validation set
        // can't be a call to rand. It must be a fixed property of a
        // hash of some aspect of it.  This way you don't accidentally
        // do a training run where a validation set member was in the
        // training set of a previous run. The id of the fastest
        // schedule will do as a hash.
        PipelineSample &ps = ((hash & 7) == 0) ? validation_set[pipeline_id] : training_set[pipeline_id];

        auto it = ps.schedules.find(schedule_hash);
        if (it != ps.schedules.end()) {
            // Keep the smallest runtime at the front
            float best = it->second.runtimes[0];
            if (runtime < best) {
                it->second.runtimes.push_back(best);
                it->second.runtimes[0] = runtime;
                it->second.filename = s;
            } else {
                it->second.runtimes.push_back(runtime);
            }
            if (runtime < ps.fastest_runtime) {
                ps.fastest_runtime = runtime;
                ps.fastest_schedule_hash = schedule_hash;
            }
        } else {
            Sample sample;
            sample.schedule_hash = schedule_hash;
            sample.filename = s;
            sample.runtimes.push_back(runtime);
            for (double &i : sample.prediction) {
                i = 0.0;
            }
            sample.schedule_id = schedule_id;
            sample.schedule_features = Buffer<float>(head2_w, num_stages);

            bool ok = true;
            for (size_t i = 0; i < num_stages; i++) {
                for (int x = 0; x < head2_w; x++) {
                    float f = scratch[i * features_per_stage + x];
                    if (f < 0 || f > 1e14 || std::isnan(f)) {
                        std::cout << "Negative or implausibly large schedule feature: " << i << " " << x << " " << f << "\n";
                        // Something must have overflowed
                        ok = false;
                    }
                    sample.schedule_features(x, i) = f;
                }
                /*
                if (sample.schedule_features(0, i) != sample.schedule_features(1, i)) {
                    std::cout << "Rejecting sliding window schedule for now\n";
                    ok = false;
                }
                */
            }
            if (ok) {
                if (runtime < ps.fastest_runtime) {
                    ps.fastest_runtime = runtime;
                    ps.fastest_schedule_hash = schedule_hash;
                }
                ps.schedules.emplace(schedule_hash, std::move(sample));
                num_unique++;
            }
        }
        num_read++;

        if (num_read % 10000 == 0) {
            std::cout << "Samples loaded: " << num_read << " (" << num_unique << " unique)\n";
        }
    }

    auto dur = Clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
    auto avg = ms / (float)num_read;
    std::cout << "Samples loaded: " << num_read << " (" << num_unique << " unique) in " << ms << "ms (avg. per sample = " << avg << " ms)\n";

    // If the training set is empty, we are likely training on a single pipeline
    if (training_set.empty()) {
        training_set.swap(validation_set);
    }

    // Check the noise level
    for (const auto &pipe : training_set) {
        double variance_sum = 0;
        size_t count = 0;
        // Compute the weighted average of variances across all samples
        for (const auto &p : pipe.second.schedules) {
            if (p.second.runtimes.empty()) {
                std::cerr << "Empty runtimes for schedule: " << p.first << "\n";
                abort();
            }
            if (flags.verbose) {
                std::cout << "Unique sample: " << leaf(p.second.filename) << " : " << p.second.runtimes[0] << "\n";
            }
            if (p.second.runtimes.size() > 1) {
                // Compute variance from samples
                double mean = 0;
                for (float f : p.second.runtimes) {
                    mean += f;
                }
                mean /= p.second.runtimes.size();
                double variance = 0;
                for (float f : p.second.runtimes) {
                    f -= mean;
                    variance += f * f;
                }
                variance_sum += variance;
                count += p.second.runtimes.size() - 1;
            }
        }
        if (count > 0) {
            double stddev = std::sqrt(variance_sum / count);
            std::cout << "Noise level: " << stddev << "\n";
        }
    }

    std::cout << "Distinct pipelines: " << training_set.size() + validation_set.size() << "\n";

    std::ostringstream o;
    o << "Best runtime is " << best_runtime << " msec, from schedule id " << best << " in file " << best_path << "\n";
    std::cout << o.str();
    if (!predict_only && !flags.best_benchmark_path.empty()) {
        std::ofstream f(flags.best_benchmark_path, std::ios_base::trunc);
        f << o.str();
        f.close();
        assert(!f.fail());
    }
    if (!predict_only && !flags.best_schedule_path.empty()) {
        // best_path points to a .sample file; look for a .schedule.h file in the same dir
        size_t dot = best_path.rfind('.');
        assert(dot != string::npos && best_path.substr(dot) == ".sample");
        string schedule_file = best_path.substr(0, dot) + ".schedule.h";
        std::ifstream src(schedule_file);

        if (!src.good()) {
            std::cout << "Could not find " << schedule_file << ". Unable to save it as the best schedule. Continuing...\n";
            return num_read;
        }
        std::ofstream dst(flags.best_schedule_path);
        dst << src.rdbuf();
        assert(!src.fail());
        assert(!dst.fail());
    }

    return num_read;
}

void save_predictions(const map<int, PipelineSample> &samples, const string &filename) {
    std::ostringstream out;

    for (const auto &p : samples) {
        for (const auto &sched : p.second.schedules) {
            out << sched.second.filename << ", " << sched.second.prediction[0] << ", " << sched.second.runtimes[0] << "\n";
        }
    }

    std::ofstream file(filename, std::ios_base::trunc);
    file << out.str();
    file.close();
    assert(!file.fail());

    std::cout << "Predictions saved to: " << filename << "\n";
}

void print_statistics(const map<int, PipelineSample> &training_set, const map<int, PipelineSample> &validation_set) {
    int64_t num_training_set_schedules = 0;
    int64_t num_val_set_schedules = 0;

    for (const auto &ps : training_set) {
        num_training_set_schedules += ps.second.schedules.size();
    }

    for (const auto &ps : validation_set) {
        num_val_set_schedules += ps.second.schedules.size();
    }

    std::cout << "Training set: "
              << training_set.size()
              << " pipelines, "
              << num_training_set_schedules
              << " schedules. Validation set: "
              << validation_set.size()
              << " pipelines, "
              << num_val_set_schedules
              << " schedules.\n";
}

}  // namespace

int main(int argc, char **argv) {
    Flags flags(argc, argv);

    // Iterate through the pipelines
    vector<std::unique_ptr<DefaultCostModel>> tpp;
    Internal::Autoscheduler::Statistics stats;
    for (int i = 0; i < kModels; i++) {
        tpp.emplace_back(make_default_cost_model(stats, flags.initial_weights_path, flags.weights_out_path, flags.randomize_weights || flags.reset_weights));
    }

    if (flags.reset_weights) {
        std::cout << "Saving new random weights...\n";
        for (int i = 0; i < kModels; i++) {
            tpp[i]->save_weights();
        }
        return 0;
    }

    map<int, PipelineSample> samples;
    map<int, PipelineSample> validation_set;
    map<int, PipelineData> pipelines;
    bool predict_only = !flags.predictions_file.empty();
    size_t num_samples = load_samples(samples, validation_set, pipelines, flags, predict_only);
    print_statistics(samples, validation_set);

    if (predict_only) {
        std::cout << "Predicting only (no training)\n";
        flags.epochs = 1;
    }

    std::cout.setf(std::ios::fixed, std::ios::floatfield);
    std::cout.precision(4);

    auto seed = time(nullptr);
    std::mt19937 rng((uint32_t)seed);

    std::cout << "Iterating over " << samples.size() << " pipelines using seed = " << seed << "\n";

    std::cout << "Constructing training batches\n";
    struct Batch {
        int pipeline_id;
        int first;
        int batch_size;
    };
    vector<Batch> training_batches, validation_batches;
    for (int train = 0; train < 2; train++) {
        for (auto &p : train ? samples : validation_set) {
            for (int first = 0; first < (int)p.second.schedules.size(); first += 64) {
                Batch b;
                b.pipeline_id = p.first;
                b.first = first;
                int end = std::min((int)p.second.schedules.size(), first + 64);
                b.batch_size = end - first;
                if (b.batch_size > 8) {
                    if (train) {
                        training_batches.push_back(b);
                    } else {
                        validation_batches.push_back(b);
                    }
                }
            }
        }
    }
    std::cout << training_batches.size() << " " << validation_batches.size() << " batches constructed\n";

    std::chrono::time_point<Clock> start = Clock::now();
    for (float learning_rate : flags.rates) {
        float loss_sum[kModels] = {0}, loss_sum_counter[kModels] = {0};
        float correct_ordering_rate_sum[kModels] = {0};
        float correct_ordering_rate_count[kModels] = {0};
        float v_correct_ordering_rate_sum[kModels] = {0};
        float v_correct_ordering_rate_count[kModels] = {0};

        for (int e = 0; e < flags.epochs; e++) {
            std::chrono::time_point<Clock> epoch_start = Clock::now();
            int counter = 0;

            float worst_miss = 0;
            uint64_t worst_miss_pipeline_id = 0;
            uint64_t worst_miss_schedule_id = 0;

            struct Inversion {
                int pipeline_id;
                string f1, f2;
                float p1, p2;
                float r1, r2;
                float badness = 0;
            } worst_inversion;

#if defined(_OPENMP)
#pragma omp parallel for
#endif
            for (int model = 0; model < kModels; model++) {
                loss_sum[model] = 0;
                loss_sum_counter[model] = 0;
                correct_ordering_rate_sum[model] = 0;
                correct_ordering_rate_count[model] = 0;
                v_correct_ordering_rate_sum[model] = 0;
                v_correct_ordering_rate_count[model] = 0;

                std::shuffle(training_batches.begin(), training_batches.end(), rng);

                for (int train = 0; train < 2; train++) {
                    auto &tp = tpp[model];

                    for (auto &p : train ? training_batches : validation_batches) {
                        tp->reset();
                        const auto &pipeline = pipelines[p.pipeline_id];
                        auto &sample = train ? samples[p.pipeline_id] : validation_set[p.pipeline_id];
                        tp->set_pipeline_features(pipeline.pipeline_features, flags.num_cores);

                        int fastest_idx = 0;
                        Halide::Runtime::Buffer<float> runtimes(p.batch_size);

                        auto it = sample.schedules.begin();
                        std::advance(it, p.first);
                        std::vector<std::vector<double>> cost_per_stage;
                        cost_per_stage.resize(p.batch_size);
                        for (int j = 0; j < p.batch_size; j++) {
                            auto &sched = it->second;
                            Halide::Runtime::Buffer<float> buf;
                            tp->enqueue(pipeline.num_stages, &buf, &sched.prediction[model], &cost_per_stage[j]);
                            runtimes(j) = sched.runtimes[0];
                            if (runtimes(j) < runtimes(fastest_idx)) {
                                fastest_idx = j;
                            }
                            buf.copy_from(sched.schedule_features);
                            it++;
                        }

                        float loss = 0.0f;
                        if (train && !predict_only) {
                            loss = tp->backprop(runtimes, learning_rate);
                            assert(!std::isnan(loss));
                            loss_sum[model] += loss;
                            loss_sum_counter[model]++;

                            auto it = sample.schedules.begin();
                            std::advance(it, p.first);
                            for (int j = 0; j < p.batch_size; j++) {
                                auto &sched = it->second;
                                float m = sched.runtimes[0] / (sched.prediction[model] + 1e-10f);
                                if (m > worst_miss) {
                                    worst_miss = m;
                                    worst_miss_pipeline_id = p.pipeline_id;
                                    worst_miss_schedule_id = it->first;
                                }
                                it++;
                            }
                        } else {
                            tp->evaluate_costs();
                        }

                        if (true) {
                            int good = 0, bad = 0;
                            for (auto &sched : sample.schedules) {
                                auto &ref = sample.schedules[sample.fastest_schedule_hash];
                                if (sched.second.prediction[model] == 0) {
                                    continue;
                                }
                                assert(sched.second.runtimes[0] >= ref.runtimes[0]);
                                float runtime_ratio = sched.second.runtimes[0] / ref.runtimes[0];
                                if (runtime_ratio <= 1.3f) {
                                    continue;  // Within 30% of the runtime of the best
                                }
                                if (sched.second.prediction[model] >= ref.prediction[model]) {
                                    good++;
                                } else {
                                    if (train) {
                                        float badness = (sched.second.runtimes[0] - ref.runtimes[0]) *
                                                        (ref.prediction[model] - sched.second.prediction[model]);
                                        badness /= (ref.runtimes[0] * ref.runtimes[0]);
                                        if (badness > worst_inversion.badness) {
                                            worst_inversion.pipeline_id = p.pipeline_id;
                                            worst_inversion.badness = badness;
                                            worst_inversion.r1 = ref.runtimes[0];
                                            worst_inversion.r2 = sched.second.runtimes[0];
                                            worst_inversion.p1 = ref.prediction[model];
                                            worst_inversion.p2 = sched.second.prediction[model];
                                            worst_inversion.f1 = ref.filename;
                                            worst_inversion.f2 = sched.second.filename;
                                        }
                                    }
                                    bad++;
                                }
                            }
                            if (train) {
                                correct_ordering_rate_sum[model] += good;
                                correct_ordering_rate_count[model] += good + bad;
                            } else {
                                v_correct_ordering_rate_sum[model] += good;
                                v_correct_ordering_rate_count[model] += good + bad;
                            }
                        }
                    }
                }

                counter++;
            }

            std::cout << "Loss: ";
            for (int model = 0; model < kModels; model++) {
                std::cout << loss_sum[model] / loss_sum_counter[model] << " ";
            }
            if (kModels > 1) {
                std::cout << "\n";
            }
            std::cout << " Rate: ";
            int best_model = 0;
            float best_rate = 0;
            for (int model = 0; model < kModels; model++) {
                float rate = correct_ordering_rate_sum[model] / correct_ordering_rate_count[model];
                if (correct_ordering_rate_count[model] == 0) {
                    std::cout << "? ";
                } else {
                    std::cout << rate << " ";
                }

                rate = v_correct_ordering_rate_sum[model] / v_correct_ordering_rate_count[model];
                if (rate < best_rate) {
                    best_model = model;
                    best_rate = rate;
                }
                if (v_correct_ordering_rate_count[model] == 0) {
                    std::cout << "? ";
                } else {
                    std::cout << rate << " ";
                }
            }

            if (kModels > 1) {
                std::cout << "\n";
            }
            if (!predict_only && samples.count(worst_miss_pipeline_id)) {
                std::cout << " Worst: " << worst_miss << " " << leaf(samples[worst_miss_pipeline_id].schedules[worst_miss_schedule_id].filename) << " ";
            }

            auto epoch_duration = Clock::now() - epoch_start;
            auto total_duration = Clock::now() - start;

            auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch_duration).count();
            auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_duration).count();
            std::cout << "(Epoch " << e + 1 << " ";
            std::cout << "took " << epoch_ms << " ms. ";
            std::cout << "Total time: " << total_ms << " ms. ";
            std::cout << "Avg. time per epoch: " << total_ms / (float)(e + 1) << " ms. ";
            std::cout << "Avg. time per epoch, per sample: " << total_ms / (float)((e + 1) * num_samples) << " ms)\n";

            if (worst_inversion.badness > 0) {
                std::cout << "Worst inversion:\n"
                          << leaf(worst_inversion.f1) << " predicted: " << worst_inversion.p1 << " actual: " << worst_inversion.r1 << "\n"
                          << leaf(worst_inversion.f2) << " predicted: " << worst_inversion.p2 << " actual: " << worst_inversion.r2 << "\n";
                if (samples.size() > 50000) {
                    // For robustness during training on large numbers
                    // of random pipelines, we discard poorly
                    // performing samples from the training set
                    // only. Some of them are weird degenerate
                    // pipelines.
                    samples.erase(worst_inversion.pipeline_id);
                }
            }

            if (!predict_only) {
                tpp[best_model]->save_weights();
            }

            if (!predict_only && loss_sum[best_model] < 1e-5f) {
                std::cout << "Zero loss, returning early\n";
                return 0;
            }
        }
    }

    if (predict_only) {
        save_predictions(samples, flags.predictions_file);
        save_predictions(validation_set, flags.predictions_file + "_validation_set");
    }

    return 0;
}
