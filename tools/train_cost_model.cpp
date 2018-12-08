#include <string>
#include <vector>
#include <set>
#include <unistd.h>

#include "Debug.h"
#include "ThroughputPredictorPipeline.h"

using std::vector;
using std::string;
using std::map;
using std::set;

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::AutoScheduleModel;

const int models = 80;

struct Sample {
    vector<float> runtimes;
    double prediction[models];
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
    vector<float> scratch(10 * 1024 * 1024);

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
        if (runtime <= 0 || runtime > 1000) { // Don't try to predict runtime over 1s
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
            for (int i = 0; i < models; i++) {
                sample.prediction[i] = 0.0;
            }
            sample.schedule_id = schedule_id;
            sample.schedule_features = Runtime::Buffer<float>(26, num_stages);

            bool ok = true;
            for (size_t i = 0; i < num_stages; i++) {
                for (int x = 0; x < 26; x++) {
                    float f = scratch[i * features_per_stage + x];
                    if (f < 0 || f > 1e14) {
                        debug(0) << "Negative or implausibly large schedule feature: " << i << " " << x << " " << f << "\n";
                        // Something must have overflowed
                        ok = false;
                    }
                    sample.schedule_features(x, i) = f;
                }
            }
            if (ok) {
                ps.schedules.emplace(schedule_hash, std::move(sample));
                num_unique++;
            }
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
            internal_assert(!p.second.runtimes.empty()) << "Empty runtimes for schedule: " << p.first << "\n";
            debug(0) << "Unique sample: " << p.second.filename << " : " << p.second.runtimes[0] << "\n";
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
            debug(0) << "Noise level: " << stddev << "\n";
        }
    }

    debug(0) << "Distinct pipelines: " << result.size() << "\n";

    debug(0) << "Best schedule id / runtime: " << best << " / " << best_runtime << "\n";
    return result;
}


int main(int argc, char **argv) {
    auto samples = load_samples();

    // Iterate through the pipelines
    ThroughputPredictorPipeline tpp[models];

    float rates[] = {0.01f};

    int num_cores = atoi(getenv("HL_NUM_THREADS"));

    std::set<int> blacklist;

    for (float learning_rate : rates) {
        for (int batch = 0; batch < atoi(argv[1]); batch++) {
            int counter = 0;
            float loss_sum[models] = {0}, loss_sum_counter[models] = {0};
            float correct_ordering_rate_sum[models] = {0};
            float correct_ordering_rate_count[models] = {0};
            debug(0) << "Iterating over " << samples.size() << " samples\n";
            #pragma omp parallel for
            for (int model = 0; model < models; model++) {
                loss_sum[model] = loss_sum_counter[model] = correct_ordering_rate_sum[model] = correct_ordering_rate_count[model] = 0;
                auto &tp = tpp[model];
                for (auto &p : samples) {
                    debug(1) << "Pipeline " << p.first << " has " << p.second.schedules.size() << " schedules\n";
                    if (blacklist.count(p.first)) continue;
                    if (p.second.schedules.size() < 16) continue;
                    tp.reset();
                    tp.set_num_cores(num_cores);
                    tp.set_pipeline_features(p.second.pipeline_features);

                    size_t batch_size = std::min((size_t)1024, p.second.schedules.size());

                    Runtime::Buffer<float> runtimes(batch_size);

                    size_t first = 0;
                    if (p.second.schedules.size() > 1024) {
                        first = rand() % (p.second.schedules.size() - 1024);
                    }

                    for (size_t j = 0; j < batch_size; j++) {
                        internal_assert(j + first < p.second.schedules.size());
                        auto it = p.second.schedules.begin();
                        std::advance(it, j + first);
                        auto &sched = it->second;
                        Runtime::Buffer<float> buf;
                        tp.enqueue(p.second.num_stages, &buf, &sched.prediction[model]);
                        runtimes(j) = sched.runtimes[0];
                        buf.copy_from(sched.schedule_features);
                    }

                    float loss = tp.backprop(runtimes, learning_rate);
                    debug(1) << "Loss = " << loss << "\n";
                    loss_sum[model] += loss;
                    loss_sum_counter[model] ++;

                    if (true) {
                        int good = 0, bad = 0;
                        int attempts = 0;
                        while (good + bad < batch_size && attempts < batch_size * 2) {
                            int j1 = rand() % p.second.schedules.size();
                            int j2 = rand() % p.second.schedules.size();
                            auto it1 = p.second.schedules.begin();
                            auto it2 = p.second.schedules.begin();
                            std::advance(it1, j1);
                            std::advance(it2, j2);
                            auto &sched1 = it1->second;
                            auto &sched2 = it2->second;
                            if (sched1.prediction[model] == 0 || sched2.prediction[model] == 0) continue;
                            if (sched1.runtimes[0] > 1.5f*sched2.runtimes[0] ||
                                sched2.runtimes[0] > 1.5f*sched1.runtimes[0]) {
                                if ((sched1.prediction[model] > sched2.prediction[model]) ==
                                    (sched1.runtimes[0] > sched2.runtimes[0])) {
                                    good++;
                                } else {
                                    bad++;
                                }
                            }
                            attempts++;
                        }
                        correct_ordering_rate_sum[model] += good;
                        correct_ordering_rate_count[model] += good + bad;
                        if (false && good < bad) {
                            debug(0) << "Blacklisting " << p.first << "\n";
                            // Assume this pipeline is some sort of outlier
                            blacklist.insert(p.first);
                        }
                    }
                }

                /*
                if (batch % 100 == 0) {
                    for (auto &sched : p.second.schedules) {
                        debug(0) << sched.second.runtimes[0] << " " << sched.second.prediction << "\n";
                    }
                }
                */
                if (counter % 1000 == 999) {
                    debug(0) << "Saving weights... ";
                    // tpp.save_weights();
                }
                counter++;
            }

            debug(0) << "RMS errors: ";
            for (int model = 0; model < models; model++) {
                debug(0) << loss_sum[model] / loss_sum_counter[model] << " ";
            }
            debug(0) << "\nCorrect ordering rate: ";
            int best_model = 0;
            float best_rate = 0;
            for (int model = 0; model < models; model++) {
                float rate = correct_ordering_rate_sum[model] / correct_ordering_rate_count[model];
                if (rate < best_rate) {
                    best_model = model;
                    best_rate = rate;
                }
                debug(0) << rate << " ";
            }
            debug(0) << "\n";
            tpp[best_model].save_weights();
        }
    }

    // tpp.save_weights();

    return 0;
}
