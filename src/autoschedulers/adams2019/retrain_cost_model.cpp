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
    bool randomize_weights = false;
    string best_benchmark_path;
    string best_schedule_path;

    Flags(int argc, char **argv) {
        cmdline::parser a;

        const char *kNoDesc = "";

        constexpr bool kOptional = false;
        a.add<int>("epochs");
        a.add<string>("rates");
        a.add<string>("initial_weights", '\0', kNoDesc, kOptional, "");
        a.add<string>("weights_out");
        a.add<bool>("randomize_weights", '\0', kNoDesc, kOptional, false);
        a.add<int>("num_cores");
        a.add<string>("best_benchmark");
        a.add<string>("best_schedule");

        a.parse_check(argc, argv);  // exits if parsing fails

        epochs = a.get<int>("epochs");
        rates = parse_floats(a.get<string>("rates"));
        initial_weights_path = a.get<string>("initial_weights");
        weights_out_path = a.get<string>("weights_out");
        randomize_weights = a.exist("randomize_weights") && a.get<bool>("randomize_weights");
        best_benchmark_path = a.get<string>("best_benchmark");
        best_schedule_path = a.get<string>("best_schedule");

        if (epochs <= 0) {
            std::cerr << "--epochs must be specified and > 0.\n";
            std::cerr << a.usage();
            exit(1);
        }
        if ((!initial_weights_path.empty()) == randomize_weights) {
            std::cerr << "You must specify exactly one of --initial_weights or --randomize_weights.\n";
            std::cerr << a.usage();
            exit(1);
        }
        if (weights_out_path.empty()) {
            std::cerr << "--weights_out must be specified.\n";
            std::cerr << a.usage();
            exit(1);
        }
        if (rates.empty()) {
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
    Buffer<float> schedule_features;
};

struct PipelineSample {
    int32_t pipeline_id;
    int32_t num_stages;
    Buffer<float> pipeline_features;
    map<uint64_t, Sample> schedules;
    uint64_t fastest_schedule_hash;
    float fastest_runtime;  // in msec
    uint64_t pipeline_hash;
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
map<int, PipelineSample> load_samples(const Flags &flags) {
    map<int, PipelineSample> result;
    vector<float> scratch(10 * 1024 * 1024);

    int best = -1;
    float best_runtime = 1e20f;
    string best_path;

    size_t num_read = 0, num_unique = 0;
    while (!std::cin.eof()) {
        string s;
        std::cin >> s;
        if (s.empty()) {
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
            std::cout << "Truncated sample: " << s << " " << floats_read << "\n";
            continue;
        }
        const size_t num_stages = num_features / features_per_stage;

        const float runtime = scratch[num_features];
        if (runtime > 100000 || runtime < 0.1) {  // Don't try to predict runtime over 100s
            std::cout << "Implausible runtime in ms: " << runtime << "\n";
            continue;
        }

        int pipeline_id = *((int32_t *)(&scratch[num_features + 1]));
        const int schedule_id = *((int32_t *)(&scratch[num_features + 2]));

        if (runtime < best_runtime) {
            best_runtime = runtime;
            best = schedule_id;
            best_path = s;
        }

        uint64_t pipeline_hash = 0;
        for (size_t i = 0; i < num_stages; i++) {
            pipeline_hash =
                hash_floats(pipeline_hash,
                            &scratch[i * features_per_stage + head2_w],
                            &scratch[(i + 1) * features_per_stage]);
        }

        // Just use the hash as the id. Hash collisions are very very unlikely.
        PipelineSample &ps = result[pipeline_hash];

        if (ps.pipeline_features.data() == nullptr) {
            ps.pipeline_id = pipeline_id;
            ps.num_stages = (int)num_stages;
            ps.pipeline_features = Buffer<float>(head1_w, head1_h, num_stages);
            ps.fastest_runtime = 1e30f;
            for (size_t i = 0; i < num_stages; i++) {
                for (int x = 0; x < head1_w; x++) {
                    for (int y = 0; y < head1_h; y++) {
                        float f = scratch[i * features_per_stage + (x + 1) * 7 + y + head2_w];
                        if (f < 0 || std::isnan(f)) {
                            std::cout << "Negative or NaN pipeline feature: " << x << " " << y << " " << i << " " << f << "\n";
                        }
                        ps.pipeline_features(x, y, i) = f;
                    }
                }
            }

            ps.pipeline_hash = pipeline_hash;
        } else {
            // Even for a huge number of pipelines, a hash collision is
            // vanishingly unlikely. Still, this will detect ones that are going
            // to cause UB during training:
            if ((int)num_stages != ps.num_stages) {
                std::cout << "Hash collision: two pipelines with a different number of stages both hashed to " << pipeline_hash << "\n";
                continue;
            }
        }

        uint64_t schedule_hash = 0;
        for (size_t i = 0; i < num_stages; i++) {
            schedule_hash =
                hash_floats(schedule_hash,
                            &scratch[i * features_per_stage],
                            &scratch[i * features_per_stage + head2_w]);
        }

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
            sample.filename = s;
            sample.runtimes.push_back(runtime);
            for (double &d : sample.prediction) {
                d = 0.0;
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

    // Check the noise level
    for (const auto &pipe : result) {
        double variance_sum = 0;
        size_t count = 0;
        // Compute the weighted average of variances across all samples
        for (const auto &p : pipe.second.schedules) {
            if (p.second.runtimes.empty()) {
                std::cerr << "Empty runtimes for schedule: " << p.first << "\n";
                abort();
            }
            std::cout << "Unique sample: " << leaf(p.second.filename) << " : " << p.second.runtimes[0] << "\n";
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

    std::cout << "Distinct pipelines: " << result.size() << "\n";

    std::ostringstream o;
    o << "Best runtime is " << best_runtime << " msec, from schedule id " << best << " in file " << best_path << "\n";
    std::cout << o.str();
    if (!flags.best_benchmark_path.empty()) {
        std::ofstream f(flags.best_benchmark_path, std::ios_base::trunc);
        f << o.str();
        f.close();
        assert(!f.fail());
    }
    if (!flags.best_schedule_path.empty()) {
        // best_path points to a .sample file; look for a .schedule.h file in the same dir
        size_t dot = best_path.rfind('.');
        assert(dot != string::npos && best_path.substr(dot) == ".sample");
        string schedule_file = best_path.substr(0, dot) + ".schedule.h";
        std::ifstream src(schedule_file);
        std::ofstream dst(flags.best_schedule_path);
        dst << src.rdbuf();
        assert(!src.fail());
        assert(!dst.fail());
    }

    return result;
}

}  // namespace

int main(int argc, char **argv) {
    Flags flags(argc, argv);

    auto samples = load_samples(flags);

    // Iterate through the pipelines
    vector<std::unique_ptr<DefaultCostModel>> tpp;
    for (int i = 0; i < kModels; i++) {
        tpp.emplace_back(make_default_cost_model(flags.initial_weights_path, flags.weights_out_path, flags.randomize_weights));
    }

    std::cout.setf(std::ios::fixed, std::ios::floatfield);
    std::cout.precision(4);

    auto seed = time(nullptr);
    std::mt19937 rng((uint32_t)seed);

    std::cout << "Iterating over " << samples.size() << " samples using seed = " << seed << "\n";
    decltype(samples) validation_set;
    uint64_t unique_schedules = 0;
    if (samples.size() > 16) {
        for (const auto &p : samples) {
            unique_schedules += p.second.schedules.size();
            // Whether or not a pipeline is part of the validation set
            // can't be a call to rand. It must be a fixed property of a
            // hash of some aspect of it.  This way you don't accidentally
            // do a training run where a validation set member was in the
            // training set of a previous run. The id of the fastest
            // schedule will do as a hash.
            if ((p.second.pipeline_hash & 7) == 0) {
                validation_set.insert(p);
            }
        }

        for (const auto &p : validation_set) {
            samples.erase(p.first);
        }
    }

    std::cout << "Number of unique schedules: " << unique_schedules << "\n";

    for (float learning_rate : flags.rates) {
        float loss_sum[kModels] = {0}, loss_sum_counter[kModels] = {0};
        float single_shot_loss_sum[kModels] = {0};
        float single_shot_loss_count[kModels] = {0};
        float v_single_shot_loss_sum[kModels] = {0};
        float v_single_shot_loss_count[kModels] = {0};

        float r2[kModels] = {0};
        float v_r2[kModels] = {0};

        for (int e = 0; e < flags.epochs; e++) {
            float worst_miss = 0;
            uint64_t worst_miss_pipeline_id = 0;
            uint64_t worst_miss_schedule_id = 0;

#if defined(_OPENMP)
#pragma omp parallel for
#endif
            for (int model = 0; model < kModels; model++) {
                for (int train = 0; train < 2; train++) {
                    auto &tp = tpp[model];

                    double sum_actual = 0, sum2_actual = 0, sum_predicted = 0, sum2_predicted = 0, sum_predicted_times_actual = 0;
                    int n = 0;

                    for (auto &p : train ? samples : validation_set) {
                        if (kModels > 1 && rng() & 1) {
                            continue;  // If we are training multiple kModels, allow them to diverge.
                        }
                        // if (p.second.schedules.size() < 8) {
                        //     continue;
                        // }
                        tp->reset();
                        tp->set_pipeline_features(p.second.pipeline_features, flags.num_cores);

                        size_t batch_size = std::min((size_t)1024, p.second.schedules.size());

                        size_t fastest_idx = 0;
                        Halide::Runtime::Buffer<float> runtimes(batch_size);

                        size_t first = 0;
                        if (p.second.schedules.size() > 1024) {
                            first = rng() % (p.second.schedules.size() - 1024);
                        }

                        auto it = p.second.schedules.begin();
                        std::advance(it, first);
                        for (size_t j = 0; j < batch_size; j++) {
                            auto &sched = it->second;
                            Halide::Runtime::Buffer<float> buf;
                            tp->enqueue(p.second.num_stages, &buf, &sched.prediction[model]);
                            runtimes(j) = sched.runtimes[0];
                            if (runtimes(j) < runtimes(fastest_idx)) {
                                fastest_idx = j;
                            }
                            buf.copy_from(sched.schedule_features);
                            it++;
                        }

                        float loss = 0.0f;
                        if (train) {
                            loss = tp->backprop(runtimes, learning_rate);
                            assert(!std::isnan(loss));
                            loss_sum[model] += loss;
                            loss_sum_counter[model]++;

                            auto it = p.second.schedules.begin();
                            std::advance(it, first);
                            for (size_t j = 0; j < batch_size; j++) {
                                auto &sched = it->second;
                                float m = sched.runtimes[0] / (sched.prediction[model] + 1e-10f);
                                if (m > worst_miss) {
                                    worst_miss = m;
                                    worst_miss_pipeline_id = p.first;
                                    worst_miss_schedule_id = it->first;
                                }
                                it++;
                            }
                        } else {
                            tp->evaluate_costs();
                        }

                        // Compute statistics for R^2 on relative throughput
                        if (true) {
                            auto &ref = p.second.schedules[p.second.fastest_schedule_hash];
                            for (auto &sched : p.second.schedules) {
                                if (sched.second.prediction[model] == 0) {
                                    continue;
                                }
                                double actual = ref.runtimes[0] / sched.second.runtimes[0];
                                double predicted = ref.runtimes[0] / sched.second.prediction[model];
                                sum_actual += actual;
                                sum2_actual += actual * actual;
                                sum_predicted += predicted;
                                sum2_predicted += predicted * predicted;
                                sum_predicted_times_actual += predicted * actual;
                                n++;
                            }
                        }

                        // Compute how much performance we would leave on the
                        // floor doing single-shot autoscheduling
                        if (true) {
                            auto &ref = p.second.schedules[p.second.fastest_schedule_hash];
                            double best_predicted_runtime = 1e50;
                            double actual_runtime_of_best_predicted_runtime = 0;
                            for (auto &sched : p.second.schedules) {
                                double predicted = sched.second.prediction[model];
                                double actual = sched.second.runtimes[0];
                                if (predicted == 0) {
                                    continue;
                                }
                                assert(actual >= ref.runtimes[0]);
                                if (predicted < best_predicted_runtime) {
                                    best_predicted_runtime = predicted;
                                    actual_runtime_of_best_predicted_runtime = actual;
                                }
                            }
                            if (train) {
                                single_shot_loss_sum[model] += ref.runtimes[0] / actual_runtime_of_best_predicted_runtime;
                                single_shot_loss_count[model]++;
                            } else {
                                v_single_shot_loss_sum[model] += ref.runtimes[0] / actual_runtime_of_best_predicted_runtime;
                                v_single_shot_loss_count[model]++;
                            }
                        }
                    }

                    // Compute R^2
                    double covariance = n * sum_predicted_times_actual - sum_predicted * sum_actual;
                    double predicted_variance = n * sum2_predicted - sum_predicted * sum_predicted;
                    double actual_variance = n * sum2_actual - sum_actual * sum_actual;
                    double r = (covariance * covariance) / (predicted_variance * actual_variance);
                    if (train) {
                        r2[model] = r;
                    } else {
                        v_r2[model] = r;
                    }
                }
            }

            std::cout << "Loss: ";
            for (int model = 0; model < kModels; model++) {
                std::cout << loss_sum[model] / loss_sum_counter[model] << " ";
            }
            if (kModels > 1) {
                std::cout << "\n";
            }

            std::cout << " R^2: ";
            for (int model = 0; model < kModels; model++) {
                std::cout << r2[model] << " " << v_r2[model] << " ";
            }
            if (kModels > 1) {
                std::cout << "\n";
            }

            std::cout << " Single-shot: ";
            int best_model = 0;
            float best_rate = 0;
            for (int model = 0; model < kModels; model++) {
                float rate = single_shot_loss_sum[model] / single_shot_loss_count[model];
                std::cout << rate << " ";

                rate = v_single_shot_loss_sum[model] / v_single_shot_loss_count[model];
                if (rate < best_rate) {
                    best_model = model;
                    best_rate = rate;
                }
                std::cout << rate << " ";
            }

            if (kModels > 1) {
                std::cout << "\n";
            }
            if (samples.count(worst_miss_pipeline_id)) {
                std::cout << " Worst: " << worst_miss << " " << leaf(samples[worst_miss_pipeline_id].schedules[worst_miss_schedule_id].filename) << "\n";
                // samples[worst_miss_pipeline_id].schedules.erase(worst_miss_schedule_id);
            } else {
                std::cout << "\n";
            }

            tpp[best_model]->save_weights();

            if (loss_sum[best_model] < 1e-5f) {
                std::cout << "Zero loss, returning early\n";
                return 0;
            }

            const float kSmoothing = 0.0f;
            for (int model = 0; model < kModels; model++) {
                loss_sum[model] *= kSmoothing;
                loss_sum_counter[model] *= kSmoothing;
                single_shot_loss_sum[model] *= kSmoothing;
                single_shot_loss_count[model] *= kSmoothing;
                v_single_shot_loss_sum[model] *= kSmoothing;
                v_single_shot_loss_count[model] *= kSmoothing;
            }
        }
    }

    // tpp.save_weights();

    return 0;
}
