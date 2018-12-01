#include <string>
#include <vector>
#include <unistd.h>

#include "Debug.h"
#include "ThroughputPredictorPipeline.h"

using std::vector;
using std::string;
using std::map;

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::AutoScheduleModel;

struct Sample {
    vector<float> runtimes;
    double prediction;
    string filename;
    int32_t schedule_id;
    Runtime::Buffer<float> schedule_features;
};

struct PipelineSample {
    int32_t pipeline_id;
    int32_t num_stages;
    Runtime::Buffer<float> pipeline_features;
    map<uint64_t, Sample> schedules;
};

uint64_t hash_floats(uint64_t h, float *begin, float *end) {
    while (begin != end) {
        uint32_t bits = *((uint32_t *)begin);
        // From boost
        h ^= (bits + 0x9e3779b9 + (h<<6) + (h>>2));
        begin++;
    }
    return h;
}

// Load all the samples, reading filenames from stdin
map<int, PipelineSample> load_samples() {
    map<int, PipelineSample> result;
    vector<float> scratch(1024 * 1024);

    int best = -1;
    float best_runtime = 1e20f;

    size_t num_read = 0, num_unique = 0;
    while (!std::cin.eof()) {
        string s;
        std::cin >> s;
        internal_assert(!ends_with(s, "\n"));
        if (!ends_with(s, ".sample")) {
            debug(0) << "Skipping file: " << s << "\n";
            continue;
        }
        std::ifstream file(s);
        file.read((char *)(scratch.data()), scratch.size() * sizeof(float));
        const size_t floats_read = file.gcount() / sizeof(float);
        const size_t num_features = floats_read - 3;
        const size_t features_per_stage = 26 + 57 * 7;
        file.close();

        if (floats_read == scratch.size()) {
            debug(0) << "Too-large sample: " << s << " " << floats_read << "\n";
            continue;
        }
        if (num_features % features_per_stage != 0) {
            debug(0) << "Truncated sample: " << s << " " << floats_read << "\n";
            continue;
        }
        const size_t num_stages = num_features / features_per_stage;

        const float runtime = scratch[num_features];
        if (runtime <= 0 || runtime > 1000 * 1000) {
            debug(0) << "Implausible runtime in ms: " << runtime << "\n";
            continue;
        }
        // debug(0) << "Runtime: " << runtime << "\n";

        const int pipeline_id = *((int32_t *)(&scratch[num_features + 1]));
        const int schedule_id = *((int32_t *)(&scratch[num_features + 2]));

        if (runtime < best_runtime) {
            best_runtime = runtime;
            best = schedule_id;
        }

        PipelineSample &ps = result[pipeline_id];
        if (ps.pipeline_features.data() == nullptr) {
            ps.pipeline_id = pipeline_id;
            ps.num_stages = (int)num_stages;
            ps.pipeline_features = Runtime::Buffer<float>(56, 7, num_stages);
            for (size_t i = 0; i < num_stages; i++) {
                for (int x = 0; x < 56; x++) {
                    for (int y = 0; y < 7; y++) {
                        ps.pipeline_features(x, y, i) = scratch[i * features_per_stage + (x + 1) * 7 + y + 26];
                    }
                }
            }
        }


        uint64_t schedule_hash = 0;
        for (size_t i = 0; i < num_stages; i++) {
            schedule_hash =
                hash_floats(schedule_hash,
                            &scratch[i * features_per_stage],
                            &scratch[i * features_per_stage + 26]);
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
        } else {
            Sample sample;
            sample.filename = s;
            sample.runtimes.push_back(runtime);
            sample.prediction = 0.0;
            sample.schedule_id = schedule_id;
            sample.schedule_features = Runtime::Buffer<float>(26, num_stages);

            for (size_t i = 0; i < num_stages; i++) {
                for (int x = 0; x < 26; x++) {
                    sample.schedule_features(x, i) = scratch[i * features_per_stage + x];
                }
            }
            ps.schedules.emplace(schedule_hash, std::move(sample));
            num_unique++;
        }
        num_read++;

        if (num_read % 10000 == 0) {
            debug(0) << "Samples loaded: " << num_read << " (" << num_unique << " unique)\n";
        }
    }

    // Check the noise level
    for (const auto &pipe : result) {
        double variance_sum = 0;
        size_t count = 0;
        // Compute the weighted average of variances across all samples
        for (const auto &p : pipe.second.schedules) {
            debug(0) << "Unique sample: " << p.second.filename << "\n";
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
        double stddev = std::sqrt(variance_sum / count);
        debug(0) << "Noise level: " << stddev << "\n";
    }

    debug(0) << "Best schedule id / runtime: " << best << " / " << best_runtime << "\n";
    return result;
}


int main(int argc, char **argv) {
    auto samples = load_samples();

    // Iterate through the pipelines
    ThroughputPredictorPipeline tpp;

    float rates[] = {1e-4f};

    for (float learning_rate : rates) {
        for (int batch = 0; batch < 1000000; batch++) {
            for (auto &p : samples) {
                tpp.reset();
                tpp.set_pipeline_features(p.second.pipeline_features);

                Runtime::Buffer<float> runtimes(1024);

                for (int i = 0; i < 1024; i++) {
                    int j = rand() % p.second.schedules.size();
                    auto it = p.second.schedules.begin();
                    std::advance(it, j);
                    auto &sched = it->second;
                    Runtime::Buffer<float> buf;
                    tpp.enqueue(p.second.num_stages, &buf, &sched.prediction);
                    runtimes(i) = sched.runtimes[0];
                    buf.copy_from(sched.schedule_features);
                }

                float loss = tpp.backprop(runtimes, learning_rate);

                debug(0) << "RMS error = " << std::sqrt(loss) << "\n";

                if (batch % 10 == 0 && batch > 100) {
                    int good = 0, bad = 0;
                    while (good + bad < 1024) {
                        int j1 = rand() % p.second.schedules.size();
                        int j2 = rand() % p.second.schedules.size();
                        auto it1 = p.second.schedules.begin();
                        auto it2 = p.second.schedules.begin();
                        std::advance(it1, j1);
                        std::advance(it2, j2);
                        auto &sched1 = it1->second;
                        auto &sched2 = it2->second;
                        if (sched1.prediction == 0 || sched2.prediction == 0) continue;
                        if (std::abs(sched1.runtimes[0] - sched2.runtimes[0]) > 0.1f) {
                            if ((sched1.prediction > sched2.prediction) ==
                                (sched1.runtimes[0] > sched2.runtimes[0])) {
                                good++;
                            } else {
                                bad++;
                            }
                        }
                    }
                    debug(0) << "Correct ordering rate = " << (float)good / (good + bad) << "\n";
                }

                if (batch % 100 == 0) {
                    for (auto &sched : p.second.schedules) {
                        debug(0) << sched.second.runtimes[0] << " " << sched.second.prediction << "\n";
                    }
                }
            }

            if (batch % 100 == 0) {
                tpp.save_weights();
            }
        }
    }


    return 0;
}
