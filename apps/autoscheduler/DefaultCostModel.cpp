// This file is a wrapper around the cost model that loads and saves
// weights, and maintains state of various kinds. For the actual cost
// model, see cost_model_generator.cpp

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <ctime>

#include "HalideBuffer.h"
#include "cost_model.h"
#include "train_cost_model.h"

#include "CostModel.h"
#include "NetworkSize.h"

// These are weights embedded from the raw arrays in the weights
// dir. The embedding is done using binary2cpp.
extern "C" float weights_pipeline_mean[];
extern "C" int weights_pipeline_mean_length;
extern "C" float weights_pipeline_std[];
extern "C" int weights_pipeline_std_length;
extern "C" float weights_schedule_mean[];
extern "C" int weights_schedule_mean_length;
extern "C" float weights_schedule_std[];
extern "C" int weights_schedule_std_length;

extern "C" float weights_head1_conv1_bias[];
extern "C" int weights_head1_conv1_bias_length;
extern "C" float weights_head1_conv1_weight[];
extern "C" int weights_head1_conv1_weight_length;
extern "C" float weights_head2_conv1_bias[];
extern "C" int weights_head2_conv1_bias_length;
extern "C" float weights_head2_conv1_weight[];
extern "C" int weights_head2_conv1_weight_length;
extern "C" float weights_trunk_conv1_bias[];
extern "C" int weights_trunk_conv1_bias_length;
extern "C" float weights_trunk_conv1_weight[];
extern "C" int weights_trunk_conv1_weight_length;

using namespace Halide;

Runtime::Buffer<float> buffer_from_file(const std::string &filename, const std::vector<int> &shape) {
    Runtime::Buffer<float> buf(shape);

    std::ifstream i(filename, std::ios_base::binary);
    i.read((char *)(buf.data()), buf.size_in_bytes());
    i.close();

    if (i.fail()) {
        auto seed = time(NULL);
        std::mt19937 rng((uint32_t) seed);
        std::cerr << "Could not load buffer from file: " << filename << "\n Using random values with seed = " << seed << " instead.\n";
        buf.for_each_value([&rng](float &f) {
                f = ((float)rng()) / rng.max() - 0.5f;
            });
    }

    return buf;
}

void buffer_to_file(const Runtime::Buffer<float> &buf, const std::string &filename) {
    std::ofstream o(filename, std::ios_base::trunc | std::ios_base::binary);
    o.write((const char *)(buf.data()), buf.size_in_bytes());
    o.close();
    assert(!o.fail());
}

struct Weights {
    Runtime::Buffer<float> head1_filter;
    Runtime::Buffer<float> head1_bias;

    Runtime::Buffer<float> head2_filter;
    Runtime::Buffer<float> head2_bias;

    Runtime::Buffer<float> conv1_filter;
    Runtime::Buffer<float> conv1_bias;
};

class DefaultCostModel : public CostModel {
    Weights weights;
    Runtime::Buffer<float> schedule_feat_queue, pipeline_feat_queue, costs;
    Runtime::Buffer<double *> cost_ptrs;
    int cursor, num_stages, num_cores;

    const std::string weights_in_dir, weights_out_dir;
    const bool randomize_weights;

 public:

    DefaultCostModel(const std::string &weights_in_dir,
                     const std::string &weights_out_dir,
                     bool randomize_weights) :
        weights_in_dir(weights_in_dir),
        weights_out_dir(weights_out_dir),
        randomize_weights(randomize_weights) {

        load_weights();
    }

    void set_pipeline_features(const Runtime::Buffer<float> &pipeline_feats, int n) {
        pipeline_feat_queue = pipeline_feats;
        assert(n > 0);
        num_cores = n;
    }

    void enqueue(int ns, Runtime::Buffer<float> *schedule_feats, double *cost_ptr) {
        num_stages = ns;

        // We know the most stages that will ever be enqueued from the schedule features
        assert(pipeline_feat_queue.data() && "Call set_schedule_features before calling enqueue\n");
        const int max_num_stages = pipeline_feat_queue.dim(2).extent();
        if (num_stages > max_num_stages){
            std::cerr
                << "schedule features has more stages (" << num_stages
                << ") than pipeline features (" << max_num_stages << ")\n";
            abort();
        }

        const int batch_size = 1024;
        if (!schedule_feat_queue.data() ||
            schedule_feat_queue.dim(2).extent() < max_num_stages) {
            assert(cursor == 0);
            schedule_feat_queue = Runtime::Buffer<float>(batch_size, head2_w, max_num_stages);
            if (!costs.data()) {
                assert(!cost_ptrs.data());
                costs = Runtime::Buffer<float>(batch_size);
                cost_ptrs = Runtime::Buffer<double *>(batch_size);
            }
        }

        if (cursor == batch_size) {
            evaluate_costs();
        }

        *schedule_feats = schedule_feat_queue.sliced(0, cursor);
        cost_ptrs(cursor) = cost_ptr;

        cursor++;
    }

    // Backprop state. To run ADAM we need a running average of the
    // gradients and gradients squared. We add an outer dimension of
    // size 3 to the new weight outputs to track this state. So buf(_,
    // 0) is the new weight, buf(_, 1) is the ADAM running average of
    // the first moment, and buf(_, 2) is the ADAM running average of
    // the second moment.
    Runtime::Buffer<float>
        head1_filter_update, head1_bias_update,
        head2_filter_update, head2_bias_update,
        conv1_filter_update, conv1_bias_update;
    int timestep = 0;

    float backprop(const Runtime::Buffer<const float> &true_runtimes, float learning_rate) {
        assert(cursor != 0);
        assert(pipeline_feat_queue.data());
        assert(schedule_feat_queue.data());

        auto loss = Runtime::Buffer<float>::make_scalar();

        if (!head1_filter_update.data()) {
            auto weight_update_buffer = [](const Runtime::Buffer<float> &w) {
                std::vector<int> size;
                for (int i = 0; i < w.dimensions(); i++) {
                    size.push_back(w.dim(i).extent());
                }
                size.push_back(4);
                auto buf = Runtime::Buffer<float>(size);
                buf.fill(0.0f);
                return buf;
            };

            head1_filter_update = weight_update_buffer(weights.head1_filter);
            head1_bias_update = weight_update_buffer(weights.head1_bias);
            head2_filter_update = weight_update_buffer(weights.head2_filter);
            head2_bias_update = weight_update_buffer(weights.head2_bias);
            conv1_filter_update = weight_update_buffer(weights.conv1_filter);
            conv1_bias_update = weight_update_buffer(weights.conv1_bias);
            timestep = 0;
        }

        Runtime::Buffer<float> dst = costs.cropped(0, 0, cursor);

        int fastest_idx = 0;
        for (int i = 0; i < cursor; i++) {
            if (true_runtimes(i) < true_runtimes(fastest_idx)) {
                fastest_idx = i;
            }
        }

        train_cost_model(num_stages,
                         cursor,
                         num_cores,
                         pipeline_feat_queue,
                         schedule_feat_queue,
                         weights.head1_filter, weights.head1_bias,
                         weights.head2_filter, weights.head2_bias,
                         weights.conv1_filter, weights.conv1_bias,
                         learning_rate, timestep++,
                         fastest_idx,
                         true_runtimes.alias(),
                         head1_filter_update, head1_bias_update,
                         head2_filter_update, head2_bias_update,
                         conv1_filter_update, conv1_bias_update,
                         dst,
                         loss);

        bool any_nans = false;
        for (int i = 0; i < cursor; i++) {
            assert(cost_ptrs(i));
            *(cost_ptrs(i)) = dst(i);
            if (std::isnan(dst(i))) {
                any_nans = true;
                std::cerr << "Prediction " << i << " is NaN. True runtime is " << true_runtimes(i) << "\n";
                std::cerr << "Checking pipeline features for NaNs...\n";
                pipeline_feat_queue.for_each_value([&](float f) { if (std::isnan(f)) abort(); });
                std::cerr << "None found\n";
                std::cerr << "Checking schedule features for NaNs...\n";
                schedule_feat_queue.for_each_value([&](float f) { if (std::isnan(f)) abort(); });
                std::cerr << "None found\n";
                std::cerr << "Checking network weights for NaNs...\n";
                for_each_weight([&](const Runtime::Buffer<float> &buf) {
                        buf.for_each_value([&](float f) { if (std::isnan(f)) abort(); });
                    });
                std::cerr << "None found\n";
            }
            assert(true_runtimes(i) > 0);
        }
        if (any_nans) abort();

        // Update weights locally
        auto update_weight = [](const Runtime::Buffer<float> &src, Runtime::Buffer<float> &dst) {
            dst.copy_from(src.sliced(src.dimensions()-1, 0));
        };
        update_weight(head1_filter_update, weights.head1_filter);
        update_weight(head1_bias_update, weights.head1_bias);
        update_weight(head2_filter_update, weights.head2_filter);
        update_weight(head2_bias_update, weights.head2_bias);
        update_weight(conv1_filter_update, weights.conv1_filter);
        update_weight(conv1_bias_update, weights.conv1_bias);

        assert(cursor != 0);

        return loss();
    }

    void evaluate_costs() {
        if (cursor == 0 || !schedule_feat_queue.data()) return;

        assert(pipeline_feat_queue.data());
        assert(schedule_feat_queue.data());

        Runtime::Buffer<float> dst = costs.cropped(0, 0, cursor);

        auto loss = Runtime::Buffer<float>::make_scalar();

        cost_model(num_stages,
                   cursor,
                   num_cores,
                   pipeline_feat_queue,
                   schedule_feat_queue,
                   weights.head1_filter, weights.head1_bias,
                   weights.head2_filter, weights.head2_bias,
                   weights.conv1_filter, weights.conv1_bias,
                   0.0f, 0, 0, nullptr,
                   dst, loss);

        for (int i = 0; i < cursor; i++) {
            assert(cost_ptrs(i));
            *(cost_ptrs(i)) = dst(i);
        }

        cursor = 0;
    }

    void load_weights() {

        if (weights_in_dir.empty()) {
            weights.head1_filter = Runtime::Buffer<float>(weights_head1_conv1_weight, head1_channels, head1_w, head1_h);
            assert(weights_head1_conv1_weight_length == (int)weights.head1_filter.size_in_bytes());

            weights.head1_bias = Runtime::Buffer<float>(weights_head1_conv1_bias, head1_channels);
            assert(weights_head1_conv1_bias_length == (int)weights.head1_bias.size_in_bytes());

            weights.head2_filter = Runtime::Buffer<float>(weights_head2_conv1_weight, head2_channels, head2_w);
            assert(weights_head2_conv1_weight_length == (int)weights.head2_filter.size_in_bytes());

            weights.head2_bias = Runtime::Buffer<float>(weights_head2_conv1_bias, head2_channels);
            assert(weights_head2_conv1_bias_length == (int)weights.head2_bias.size_in_bytes());

            weights.conv1_filter = Runtime::Buffer<float>(weights_trunk_conv1_weight, conv1_channels, head1_channels + head2_channels);
            assert(weights_trunk_conv1_weight_length == (int)weights.conv1_filter.size_in_bytes());

            weights.conv1_bias = Runtime::Buffer<float>(weights_trunk_conv1_bias, conv1_channels);
            assert(weights_trunk_conv1_bias_length == (int)weights.conv1_bias.size_in_bytes());
        } else {
            weights.head1_filter = buffer_from_file(weights_in_dir + "/head1_conv1_weight.data", {head1_channels, head1_w, head1_h});
            weights.head1_bias = buffer_from_file(weights_in_dir + "/head1_conv1_bias.data", {head1_channels});

            weights.head2_filter = buffer_from_file(weights_in_dir + "/head2_conv1_weight.data", {head2_channels, head2_w});
            weights.head2_bias = buffer_from_file(weights_in_dir + "/head2_conv1_bias.data", {head2_channels});

            weights.conv1_filter = buffer_from_file(weights_in_dir + "/trunk_conv1_weight.data", {conv1_channels, head1_channels + head2_channels});
            weights.conv1_bias = buffer_from_file(weights_in_dir + "/trunk_conv1_bias.data", {conv1_channels});
        }

        if (randomize_weights) {
            auto seed = time(NULL);
            std::cout << "Randomizing weights using seed = " << seed << "\n";
            std::mt19937 rng((uint32_t) seed);
            // Fill the weights with random values
            for_each_weight([&rng](Runtime::Buffer<float> &w) {
                    w.for_each_value([&rng](float &f) {
                            f = ((float)rng()) / rng.max() - 0.5f;
                        });
                });
        }
    }

    void save_weights() {
        if (weights_out_dir.empty()) return;

        buffer_to_file(weights.head1_filter, weights_out_dir + "/head1_conv1_weight.data");
        buffer_to_file(weights.head1_bias, weights_out_dir + "/head1_conv1_bias.data");
        buffer_to_file(weights.head2_filter, weights_out_dir + "/head2_conv1_weight.data");
        buffer_to_file(weights.head2_bias, weights_out_dir + "/head2_conv1_bias.data");
        buffer_to_file(weights.conv1_filter, weights_out_dir + "/trunk_conv1_weight.data");
        buffer_to_file(weights.conv1_bias, weights_out_dir + "/trunk_conv1_bias.data");
    }

    template<typename F>
    void for_each_weight(F f) {
        f(weights.head1_filter);
        f(weights.head1_bias);
        f(weights.head2_filter);
        f(weights.head2_bias);
        f(weights.conv1_filter);
        f(weights.conv1_bias);
    }

    template<typename F>
    void for_each_gradient(F f) {
        auto slice_and_call_f = [&](const Runtime::Buffer<float> &buf) {
            f(buf.sliced(buf.dimensions()-1, 3));
        };
        slice_and_call_f(head1_filter_update);
        slice_and_call_f(head1_bias_update);
        slice_and_call_f(head2_filter_update);
        slice_and_call_f(head2_bias_update);
        slice_and_call_f(conv1_filter_update);
        slice_and_call_f(conv1_bias_update);
    }

    // Discard any enqueued but unevaluated schedules
    void reset() {
        cursor = 0;
    }

};




std::unique_ptr<CostModel> CostModel::make_default(const std::string &weights_in_dir,
                                                   const std::string &weights_out_dir,
                                                   bool randomize_weights) {
    return std::unique_ptr<CostModel>(new DefaultCostModel(weights_in_dir, weights_out_dir, randomize_weights));
}
