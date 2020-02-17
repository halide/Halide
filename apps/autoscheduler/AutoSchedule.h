#ifndef AUTO_SCHEDULE_H
#define AUTO_SCHEDULE_H

#include <random>
#include <vector>

#include "CostModel.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "PerfectHashMap.h"
#include "State.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct RNG {
    std::mt19937 gen;
    std::uniform_real_distribution<double> dis;

    RNG(uint32_t seed)
        : gen{seed}
        , dis{0.0, 100.0}
    {}

    double operator()() {
        return dis(gen);
    }
};

struct ProgressBar {
    void set(double progress) {
        if (!draw_progress_bar) return;
        counter++;
        const int bits = 11;
        if (counter & ((1 << bits) - 1)) return;
        const int pos = (int)(progress * 78);
        aslog(0) << '[';
        for (int j = 0; j < 78; j++) {
            if (j < pos) {
                aslog(0) << '.';
            } else if (j - 1 < pos) {
                aslog(0) << "/-\\|"[(counter >> bits) % 4];
            } else {
                aslog(0) << ' ';
            }
        }
        aslog(0) << ']';
        for (int j = 0; j < 80; j++) {
            aslog(0) << '\b';
        }
    }

    void clear() {
        if (counter) {
            for (int j = 0; j < 80; j++) {
                aslog(0) << ' ';
            }
            for (int j = 0; j < 80; j++) {
                aslog(0) << '\b';
            }
        }
    }

private:
    uint32_t counter = 0;
    const bool draw_progress_bar = isatty(2);
};


typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

struct AutoSchedule {
    const FunctionDAG &dag;
    const MachineParams &params;
    const Target &target;
    const std::vector<Function>& outputs;
    std::mt19937 rng;
    CostModel *cost_model;

    Statistics stats;
    NodeMap<bool> inlined_nodes;
    NodeMap<std::vector<IntrusivePtr<const LoopNest>>> compute_root_nodes;
    NodeMap<std::map<int, std::vector<IntrusivePtr<const LoopNest>>>> memoized_compute_root_blocks;

    AutoSchedule(const FunctionDAG &dag, const MachineParams &params, const Target &target, const std::vector<Function>& outputs, uint32_t seed, CostModel *cost_model);

    IntrusivePtr<State> optimal_schedule_pass(int beam_size,
                                              int pass_idx,
                                              int num_passes,
                                              ProgressBar &tick,
                                              std::unordered_set<uint64_t> &permitted_hashes);

    // Performance coarse-to-fine beam search and return the best state found.
    IntrusivePtr<State> optimal_schedule(int beam_size);

    void freeze_lowest_cost_stages(const IntrusivePtr<State> best);

};

void find_and_apply_schedule(FunctionDAG& dag, const std::vector<Function> &outputs, const MachineParams &params, const Target &target, CostModel* cost_model, int beam_size, StageMapOfScheduleFeatures* schedule_features);

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // AUTO_SCHEDULE_H
