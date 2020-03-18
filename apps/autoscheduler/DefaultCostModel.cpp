// This file is a wrapper around the cost model that loads and saves
// weights, and maintains state of various kinds. For the actual cost
// model, see cost_model_generator.cpp

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <random>
#include <sstream>
#include <string>

#include "ASLog.h"
#include "DefaultCostModel.h"
#include "HalideBuffer.h"
#include "NetworkSize.h"
#include "cost_model.h"
#include "train_cost_model.h"

// This is an embedded version of `baseline.weights`.
// The embedding is done using binary2cpp.

extern "C" unsigned char baseline_weights[];
extern "C" int baseline_weights_length;

namespace Halide {
namespace {

using Halide::Internal::aslog;
using Halide::Internal::PipelineFeatures;
using Halide::Internal::ScheduleFeatures;
using Halide::Internal::Weights;
using Halide::Runtime::Buffer;

bool ends_with(const std::string &str, const std::string &suffix) {
    if (str.size() < suffix.size()) return false;
    size_t off = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (str[off + i] != suffix[i]) return false;
    }
    return true;
}

}  // namespace

void DefaultCostModel::set_pipeline_features(const Internal::Autoscheduler::FunctionDAG &dag,
                                             const MachineParams &params) {

    const int pipeline_feat_size = head1_w * head1_h;
    // We ignore the first seven pipeline features in the cost
    // model. It's just a mask of which types are in use.
    static_assert(sizeof(PipelineFeatures) - 7 * sizeof(int) ==
                      sizeof(int) * pipeline_feat_size,
                  "Incorrect size for pipeline features");
    int num_stages = 0;
    for (const auto &n : dag.nodes) {
        if (!n.is_input) num_stages += (int)n.stages.size();
    }
    Runtime::Buffer<float> pipeline_features(head1_w, head1_h, num_stages);
    int stage = 0;
    for (const auto &n : dag.nodes) {
        if (n.is_input) continue;
        for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
            const auto &s = *it;
            const int *pipeline_feats = (const int *)(&(s.features)) + 7;
            // skip the first 7 features
            for (int i = 0; i < pipeline_feat_size; i++) {
                int x = i / 7;
                int y = i % 7;
                pipeline_features(x, y, stage) = pipeline_feats[i];
            }
            stage += 1;
        }
    }
    internal_assert(stage == num_stages);
    pipeline_feat_queue = pipeline_features;
    internal_assert(params.parallelism > 0);
    num_cores = params.parallelism;
}

void DefaultCostModel::set_pipeline_features(const Runtime::Buffer<float> &pipeline_feats, int n) {
    pipeline_feat_queue = pipeline_feats;
    internal_assert(n > 0);
    num_cores = n;
}

void DefaultCostModel::enqueue(const Internal::Autoscheduler::FunctionDAG &dag,
                               const Halide::Internal::Autoscheduler::StageMapOfScheduleFeatures &schedule_feats,
                               double *cost_ptr) {
    num_stages = (int)schedule_feats.size();

    Runtime::Buffer<float> schedule_features;

    // Tell the cost model about this state. It won't actually
    // evaluate it until we call evaluate_costs (or if it runs out
    // of internal buffer space), so that the evaluations can be
    // batched.
    enqueue(num_stages, &schedule_features, cost_ptr);

    // index of current stage whose features we are reading
    int stage = 0;
    // load schedule features into input buffer
    for (const auto &n : dag.nodes) {

        // Inputs are computed outside of the pipeline and don't count.
        if (n.is_input) continue;

        // The remaining stages are not yet
        // scheduled. Optimistically assume their internal costs
        // will not depend on the decisions made already, so
        // there's no point adding it on to the total because it's
        // the same across all states.  An underestimate of the
        // cost for loading from these unscheduled stages is
        // already baked into the scheduled stages that consume
        // them.
        if (stage >= num_stages) break;

        // Load up the schedule features for all stages of this Func.
        for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
            internal_assert(schedule_feats.contains(&*it)) << n.func.name() << "\n";
            const auto &feat = schedule_feats.get(&*it);
            for (size_t i = 0; i < ScheduleFeatures::num_features(); i++) {
                schedule_features(i, stage) = feat[i];
            }
            stage += 1;
        }
    }
    // Check we considered everything we were supposed to.
    internal_assert(stage == num_stages);
}

void DefaultCostModel::enqueue(int ns, Runtime::Buffer<float> *schedule_feats, double *cost_ptr) {
    num_stages = ns;

    // We know the most stages that will ever be enqueued from the schedule features
    internal_assert(pipeline_feat_queue.data() && "Call set_schedule_features before calling enqueue\n");
    const int max_num_stages = pipeline_feat_queue.dim(2).extent();
    internal_assert(num_stages <= max_num_stages)
        << "schedule features has more stages (" << num_stages
        << ") than pipeline features (" << max_num_stages << ")\n";

    const int batch_size = 1024;
    if (!schedule_feat_queue.data() ||
        schedule_feat_queue.dim(2).extent() < max_num_stages) {
        internal_assert(cursor == 0);
        schedule_feat_queue = Runtime::Buffer<float>(batch_size, head2_w, max_num_stages);
        if (!costs.data()) {
            internal_assert(!cost_ptrs.data());
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
}  // namespace Halide

// Backprop state. To run ADAM we need a running average of the
// gradients and gradients squared. We add an outer dimension of
// size 3 to the new weight outputs to track this state. So buf(_,
// 0) is the new weight, buf(_, 1) is the ADAM running average of
// the first moment, and buf(_, 2) is the ADAM running average of
// the second moment.
float DefaultCostModel::backprop(const Runtime::Buffer<const float> &true_runtimes, float learning_rate) {
    internal_assert(cursor != 0);
    internal_assert(pipeline_feat_queue.data());
    internal_assert(schedule_feat_queue.data());

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

    int result = train_cost_model(num_stages,
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
    (void)result;
    internal_assert(result == 0);

    bool any_nans = false;
    for (int i = 0; i < cursor; i++) {
        internal_assert(cost_ptrs(i));
        *(cost_ptrs(i)) = dst(i);
        if (std::isnan(dst(i))) {
            any_nans = true;
            aslog(0) << "Prediction " << i << " is NaN. True runtime is " << true_runtimes(i) << "\n";
            aslog(0) << "Checking pipeline features for NaNs...\n";
            pipeline_feat_queue.for_each_value([&](float f) { if (std::isnan(f)) abort(); });
            aslog(0) << "None found\n";
            aslog(0) << "Checking schedule features for NaNs...\n";
            schedule_feat_queue.for_each_value([&](float f) { if (std::isnan(f)) abort(); });
            aslog(0) << "None found\n";
            aslog(0) << "Checking network weights for NaNs...\n";
            weights.for_each_buffer([&](const Runtime::Buffer<float> &buf) {
                buf.for_each_value([&](float f) { if (std::isnan(f)) abort(); });
            });
            aslog(0) << "None found\n";
        }
        internal_assert(true_runtimes(i) > 0);
    }
    if (any_nans) abort();

    // Update weights locally
    auto update_weight = [](const Runtime::Buffer<float> &src, Runtime::Buffer<float> &dst) {
        dst.copy_from(src.sliced(src.dimensions() - 1, 0));
    };
    update_weight(head1_filter_update, weights.head1_filter);
    update_weight(head1_bias_update, weights.head1_bias);
    update_weight(head2_filter_update, weights.head2_filter);
    update_weight(head2_bias_update, weights.head2_bias);
    update_weight(conv1_filter_update, weights.conv1_filter);
    update_weight(conv1_bias_update, weights.conv1_bias);

    internal_assert(cursor != 0);

    return loss();
}

void DefaultCostModel::evaluate_costs() {
    if (cursor == 0 || !schedule_feat_queue.data()) return;

    internal_assert(pipeline_feat_queue.data());
    internal_assert(schedule_feat_queue.data());

    Runtime::Buffer<float> dst = costs.cropped(0, 0, cursor);

    auto loss = Runtime::Buffer<float>::make_scalar();

    int result = cost_model(num_stages,
                            cursor,
                            num_cores,
                            pipeline_feat_queue,
                            schedule_feat_queue,
                            weights.head1_filter, weights.head1_bias,
                            weights.head2_filter, weights.head2_bias,
                            weights.conv1_filter, weights.conv1_bias,
                            0.0f, 0, 0, nullptr,
                            dst, loss);
    (void)result;
    internal_assert(result == 0);

    for (int i = 0; i < cursor; i++) {
        internal_assert(cost_ptrs(i));
        *(cost_ptrs(i)) = dst(i);
    }

    cursor = 0;
}

void DefaultCostModel::load_weights() {
    bool need_randomize = randomize_weights;

    if (weights_in_path.empty()) {
        aslog(1) << "Loading weights from built-in data...\n";
        // This copy shouldn't be necessary, but std::istream in C++ doesn't seem
        // to have a convenient wrap-around-constant-data variant... and since
        // this isn't much data, just copy it.
        const std::string baseline_weights_data((const char *)&baseline_weights[0], baseline_weights_length);
        std::istringstream i(baseline_weights_data);
        if (!weights.load(i)) {
            std::cerr << "The built-in baseline weights should never fail to load\n";
            internal_assert(0);
        }
    } else if (ends_with(weights_in_path, ".weights")) {
        aslog(1) << "Loading weights from " << weights_in_path << " ...\n";
        if (!weights.load_from_file(weights_in_path)) {
            // Emit to cout (rather than cerr) because the latter is hidden during the autotune loop,
            // and we want this to be seen.
            std::cout << "WARNING, error in reading weights from " << weights_in_path << ", randomizing...\n";
            need_randomize = true;
        }
    } else {
        aslog(1) << "Loading weights from directory " << weights_in_path << " ...\n";
        std::cerr << "Loading weights from a directory is deprecated; please convert to a .weights file\n";
        if (!weights.load_from_dir(weights_in_path)) {
            std::cout << "WARNING, error in reading weights from " << weights_in_path << ", randomizing...\n";
            need_randomize = true;
        }
    }

    if (!need_randomize && weights.pipeline_features_version != PipelineFeatures::version()) {
        // Emit to cout (rather than cerr) because the latter is hidden during the autotune loop,
        // and we want this to be seen.
        std::cout << "WARNING: loaded weights have pipeline_version = "
                  << weights.pipeline_features_version
                  << " but current pipeline_version is " << PipelineFeatures::version()
                  << "; the weights may be invalid. Using anyway.\n";
    }

    if (!need_randomize && weights.schedule_features_version != ScheduleFeatures::version()) {
        // Emit to cout (rather than cerr) because the latter is hidden during the autotune loop,
        // and we want this to be seen.
        std::cout << "WARNING: loaded weights have schedule_features_version = "
                  << weights.schedule_features_version
                  << " but current schedule_features_version is " << ScheduleFeatures::version()
                  << "; the weights may be invalid. Using anyway.\n";
    }

    if (need_randomize) {
        auto seed = time(NULL);
        std::cout << "Randomizing weights using seed = " << seed << "\n";
        weights.randomize((uint32_t)seed);
    }

    // Update so that any version of this we save will have the current version
    weights.pipeline_features_version = PipelineFeatures::version();
    weights.schedule_features_version = ScheduleFeatures::version();
}

void DefaultCostModel::save_weights() {
    internal_assert(!weights_out_path.empty())
        << "Unable to save weights: no output path specified\n";

    if (ends_with(weights_out_path, ".weights")) {
        internal_assert(weights.save_to_file(weights_out_path))
            << "Unable to save weights to file: " << weights_out_path << "\n";
    } else {
        std::cerr << "Saving weights to a directory is deprecated; please convert to a .weights file\n";
        internal_assert(weights.save_to_dir(weights_out_path))
            << "Unable to save weights to file: " << weights_out_path << "\n";
    }
}

// Discard any enqueued but unevaluated schedules
void DefaultCostModel::reset() {
    cursor = 0;
}

std::unique_ptr<DefaultCostModel> make_default_cost_model(const std::string &weights_in_path,
                                                          const std::string &weights_out_path,
                                                          bool randomize_weights) {
    return std::unique_ptr<DefaultCostModel>(new DefaultCostModel(weights_in_path, weights_out_path, randomize_weights));
}

}  // namespace Halide
