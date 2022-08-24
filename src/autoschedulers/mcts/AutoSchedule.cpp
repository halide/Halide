/*
  This file is the core of the autoscheduler. Most of the code here is
  about navigating the search space and computing the
  featurization. This also contains the top-level interface into the
  autoscheduler.

  The most interesting classes to look at are:

  LoopNest               Represents one node in our tree representation of loop nests.
  State                  A state in the beam search. Holds a root loop nest.

  Interesting functions below are:

  generate_schedule            The top-level entrypoint, which computes and applies a schedule to a Halide pipeline
  optimal_schedule             Runs the passes of the coarse-to-fine beam search
  optimal_schedule_pass        Runs a single pass of beam search
  LoopNest::compute_features   Recursively walks over a loop nest tree, computing our featurization using Halide's analysis tools.
  LoopNest::apply              Actually apply a computed schedule to a Halide pipeline
  State::generate_children     Generates successor states to a state in the beam search

  Environment variables used (directly or indirectly):

  HL_BEAM_SIZE
  Beam size to use in the beam search. Defaults to 32. Use 1 to get a greedy search instead.

  HL_CYOS
  "Choose-your-own-schedule". If set to 1, lets you navigate the search tree by hand in the terminal. Whee! This is for debugging the autoscheduler.

  HL_FEATURE_FILE -> output
  *** DEPRECATED *** use the 'featurization' output from Generator instead
  Write out a training featurization for the selected schedule into this file.
  Needs to be converted to a sample file with the runtime using featurization_to_sample before it can be used to train.

  MctsParams
  An architecture description string. Used by Halide master to configure the cost model. We only use the first term. Set it to the number of cores to target.

  HL_PERMIT_FAILED_UNROLL
  Set to 1 to tell Halide not to freak out if we try to unroll a loop that doesn't have a constant extent. Should generally not be necessary, but sometimes the autoscheduler's model for what will and will not turn into a constant during lowering is inaccurate, because Halide isn't perfect at constant-folding.

  HL_SCHEDULE_FILE
    *** DEPRECATED *** use the 'schedule' output from Generator instead
    Write out a human-and-machine MctsParams block of scheduling source code for the selected schedule into this file.

  HL_RANDOM_DROPOUT
  percent chance of accepting each state in the beam. Normalized by the number of decisions made, so 5 would be there's a 5 percent chance of never rejecting any states.

  HL_SEED
  Random seed used by the random dropout.

  HL_WEIGHTS_DIR
  When training or schedule, read weights from this directory or file
  (if path ends in `.weights` it is written as a single file, otherwise a directory of files)

  HL_NO_SUBTILING
  If set to 1, limits the search space to that of Mullapudi et al.

  HL_DEBUG_AUTOSCHEDULE
  If set, is used for the debug log level for auto-schedule generation (overriding the
  value of HL_DEBUG_CODEGEN, if any).

  HL_AUTOSCHEDULE_MEMORY_LIMIT
  If set, only consider schedules that allocate at most this much memory (measured in bytes).

  TODO: expose these settings by adding some means to pass args to
  generator plugins instead of environment vars.
*/
#include "HalidePlugin.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ASLog.h"
#include "AutoSchedule.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Errors.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "LoopNest.h"
#include "NetworkSize.h"
#include "PerfectHashMap.h"
#include "ParamParser.h"

#include "CPU_State.h"
#include "CostPrinter.h"
#include "MCTS.h"
#include "Timer.h"

#ifdef _WIN32
#include <io.h>
#define _isatty isatty;
#endif

namespace MCTS {
uint32_t get_dropout_threshold() {
    std::string random_dropout_str = Halide::Internal::get_env_variable("HL_RANDOM_DROPOUT");
    if (!random_dropout_str.empty()) {
        return atoi(random_dropout_str.c_str());
    } else {
        return 100;
    }
}

bool random_dropout(std::mt19937 &rng, size_t num_decisions) {
    static double random_dropout_threshold = get_dropout_threshold();
    if (random_dropout_threshold >= 100) {
        return false;
    }

    // The random dropout threshold is the chance that we operate
    // entirely greedily and never discard anything.
    double t = random_dropout_threshold;
    t /= 100;
    t = std::pow(t, 1.0f / num_decisions);
    t *= 100;

    uint32_t r = rng();
    bool drop_it = (r % 100) >= t;
    return drop_it;
}

double get_exploration_percent() {
    std::string exploration_str = Halide::Internal::get_env_variable("HL_MCTS_EXPLORATION");
    if (!exploration_str.empty()) {
        return std::stod(exploration_str.c_str());
    } else {
        return .025;
    }
}

double get_exploitation_percent() {
    std::string exploitation_str = Halide::Internal::get_env_variable("HL_MCTS_EXPLOITATION");
    if (!exploitation_str.empty()) {
        return std::stod(exploitation_str.c_str());
    } else {
        return .025;
    }
}

uint32_t get_min_explore() {
    std::string min_iters_str = Halide::Internal::get_env_variable("HL_MCTS_EXPLORE_MIN");
    if (!min_iters_str.empty()) {
        return atoi(min_iters_str.c_str());
    } else {
        return 4;
    }
}

uint32_t get_min_exploit() {
    std::string min_iters_str = Halide::Internal::get_env_variable("HL_MCTS_EXPLOIT_MIN");
    if (!min_iters_str.empty()) {
        return atoi(min_iters_str.c_str());
    } else {
        return 4;
    }
}

uint32_t get_rollout_length() {
    std::string rollout_str = Halide::Internal::get_env_variable("HL_MCTS_ROLLOUT_LENGTH");
    if (!rollout_str.empty()) {
        return atoi(rollout_str.c_str());
    } else {
        return 4;
    }
}

uint32_t get_beam_size() {
    std::string beam_str = Halide::Internal::get_env_variable("HL_MCTS_BEAM_SIZE");
    if (!beam_str.empty()) {
        return atoi(beam_str.c_str());
    } else {
        return 4;
    }
}

bool use_beam() {
    std::string beam_str = Halide::Internal::get_env_variable("HL_MCTS_DISABLE_BEAM");
    return beam_str != "1";
}

void print_env_variables() {
    // TODO: add to this if we add to the variables above
    std::cerr << "export HL_RANDOM_DROPOUT=" << get_dropout_threshold() << ";  ";
    std::cerr << "export HL_MCTS_EXPLORATION=" << get_exploration_percent() << ";  ";
    std::cerr << "export HL_MCTS_EXPLOITATION=" << get_exploitation_percent() << ";  ";
    std::cerr << "export HL_MCTS_EXPLORE_MIN=" << get_min_explore() << ";  ";
    std::cerr << "export HL_MCTS_EXPLOIT_MIN=" << get_min_exploit() << ";  ";
    std::cerr << "export HL_MCTS_ROLLOUT_LENGTH=" << get_rollout_length() << ";  ";
    std::cerr << "export HL_MCTS_BEAM_SIZE=" << get_beam_size() << ";  ";
    std::cerr << "export HL_MCTS_DISABLE_BEAM=" << !use_beam() << ";\n";
}
}  // namespace MCTS

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using std::map;
using std::pair;
using std::string;
using std::vector;

struct ProgressBar {
    void set(double progress) {
        if (!draw_progress_bar) {
            return;
        }
        counter++;
        const int bits = 11;
        if (counter & ((1 << bits) - 1)) {
            return;
        }
        const int pos = (int)(progress * 78);
        aslog(0) << "[";
        for (int j = 0; j < 78; j++) {
            if (j < pos) {
                aslog(0) << ".";
            } else if (j - 1 < pos) {
                aslog(0) << "/-\\|"[(counter >> bits) % 4];
            } else {
                aslog(0) << " ";
            }
        }
        aslog(0) << "]";
        for (int j = 0; j < 80; j++) {
            aslog(0) << "\b";
        }
    }

    void clear() {
        if (counter) {
            for (int j = 0; j < 80; j++) {
                aslog(0) << " ";
            }
            for (int j = 0; j < 80; j++) {
                aslog(0) << "\b";
            }
        }
    }

private:
    uint32_t counter = 0;
    const bool draw_progress_bar = isatty(2);
};

// Configure a cost model to process a specific pipeline.
void configure_pipeline_features(const FunctionDAG &dag,
                                 const MctsParams &params,
                                 CostModel *cost_model) {
    cost_model->reset();
    cost_model->set_pipeline_features(dag, params);
}

// The main entrypoint to generate a schedule for a pipeline.
void generate_schedule(const std::vector<Function> &outputs,
                       const Target &target,
                       const MctsParams &params,
                       AutoSchedulerResults *auto_scheduler_results) {
    aslog(0) << "generate_schedule for target=" << target.to_string() << "\n";

    // Start a timer
    HALIDE_TIC;

    // Get the seed for random dropout
    string seed_str = get_env_variable("HL_SEED");
    // Or use the time, if not set.
    int seed = (int)time(nullptr);
    if (!seed_str.empty()) {
        seed = atoi(seed_str.c_str());
    }
    aslog(0) << "Random seed = " << seed << "\n";
    std::mt19937 rng((uint32_t)seed);

    string weights_in_path = get_env_variable("HL_WEIGHTS_DIR");
    string weights_out_path;  // deliberately empty

    string randomize_weights_str = get_env_variable("HL_RANDOMIZE_WEIGHTS");
    bool randomize_weights = randomize_weights_str == "1";

    string memory_limit_str = get_env_variable("HL_AUTOSCHEDULE_MEMORY_LIMIT");
    int64_t memory_limit = memory_limit_str.empty() ? (uint64_t)(-1) : std::atoll(memory_limit_str.c_str());

    // Analyse the Halide algorithm and construct our abstract representation of it
    FunctionDAG dag(outputs, target);
    if (aslog::aslog_level() > 0) {
        dag.dump();
    }

    // Construct a cost model to use to evaluate states. Currently we
    // just have the one, but it's an abstract interface, so others
    // can be slotted in for experimentation.
    std::unique_ptr<CostModel> cost_model = make_default_cost_model(weights_in_path, weights_out_path, randomize_weights);
    internal_assert(cost_model != nullptr);
    configure_pipeline_features(dag, params, cost_model.get());

    aslog(0) << "Size: " << dag.nodes.size() << "\n";

    // TODO(rootjalex): should probably only print these if a verbose flag is set.
    MCTS::print_env_variables();

    Timer timer;
    auto solver = MCTS::Solver<CPU_State, CPU_Action>::MakeRandomizedSolver();

    // TODO(rootjalex): do this in parallel.
    LoopNest *root = new LoopNest;
    CPU_State start_state(&dag, &params, cost_model.get(), root, /* n_decisions */ 0, memory_limit);
    aslog(0) << "Starting\n";
    MCTS::state_count = 0;
    std::shared_ptr<MCTS::TreeNode<CPU_State, CPU_Action>> best_action = nullptr;
    double cost = 0.0f;
    std::string schedule_source;
    std::string python_schedule_source;

    try {
        CPU_State optimal = (MCTS::use_beam()) ?
                                solver.solve_beam(start_state, /* n_decisions*/ dag.nodes.size() * 2, seed) :
                                solver.solve(start_state, /* n_decisions*/ dag.nodes.size() * 2, seed);
        cost = optimal.calculate_cost();
        schedule_source = optimal.apply_schedule(python_schedule_source);
        std::cerr << "is_terminal? " << optimal.is_terminal() << std::endl;
        std::cerr << "n states generated: " << MCTS::state_count << std::endl;

        LoopNest optimal_root;
        optimal.copy_root_to(&optimal_root);

        // Save the featurization, so that we can use this schedule as
        // training data (once we've benchmarked it).
        internal_assert(optimal.dag_ptr) << "Dag ptr empty " << optimal.dag_ptr;
        internal_assert(optimal.params_ptr) << "Params ptr empty " << optimal.params_ptr;

        string feature_file = get_env_variable("HL_FEATURE_FILE");
        if (!feature_file.empty()) {
            user_warning << "HL_FEATURE_FILE is deprecated; use the featurization output from Generator instead\n";
            std::ofstream binfile(feature_file, std::ios::binary | std::ios_base::trunc);
            save_featurization(optimal.dag_ptr, optimal.params_ptr, &optimal_root, binfile);
            binfile.close();
            internal_assert(!binfile.fail()) << "Failed to write " << feature_file;
        }

        if (auto_scheduler_results) {
            auto_scheduler_results->autoscheduler_params.name = "mcts";
            auto_scheduler_results->schedule_source = schedule_source;
            auto_scheduler_results->python_schedule_source = python_schedule_source;
            {
                std::ostringstream out;
                save_featurization(optimal.dag_ptr, optimal.params_ptr, &optimal_root, out);
                auto_scheduler_results->featurization.resize(out.str().size());
                memcpy(auto_scheduler_results->featurization.data(), out.str().data(), out.str().size());
            }
        }

    } catch (const std::bad_alloc &e) {
        std::cerr << "Allocation failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Some other exception?" << std::endl;
    }
    // TODO: Search time includes the time needed to save features. Need to remove it from timing
    std::chrono::duration<double> total_time = timer.elapsed();
    auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();

    // aslog(0) << "Found Pipeline with cost: " << cost << "\n";
    aslog(0) << "Best cost: " << cost << "\n";
    aslog(0) << "Execution time: " << milli << " ms\n\n";

    // aslog(0) << "Source:" << schedule_source << "\n\n\n";
    HALIDE_TOC;

    // TODO(rootjalex): dump cost info and stuff.

    string schedule_file = get_env_variable("HL_SCHEDULE_FILE");
    if (!schedule_file.empty()) {
        user_warning << "HL_SCHEDULE_FILE is deprecated; use the schedule output from Generator instead\n";
        aslog(1) << "Writing schedule to " << schedule_file << "...\n";
        std::ofstream f(schedule_file);
        f << "// --- BEGIN machine-MctsParams schedule\n"
          << schedule_source
          << "// --- END machine-MctsParams schedule\n";
        f.close();
        internal_assert(!f.fail()) << "Failed to write " << schedule_file;
    }

    string python_schedule_file = get_env_variable("HL_PYTHON_SCHEDULE_FILE");
    if (!python_schedule_file.empty()) {
        user_warning << "HL_PYTHON_SCHEDULE_FILE is deprecated; use the schedule output from Generator instead\n";
        aslog(1) << "Writing schedule to " << python_schedule_file << "...\n";
        std::ofstream f(python_schedule_file);
        f << "# --- BEGIN machine-MctsParams schedule\n"
          << python_schedule_source
          << "# --- END machine-MctsParams schedule\n";
        f.close();
        internal_assert(!f.fail()) << "Failed to write " << python_schedule_file;
    }
}

struct mcts {
    void operator()(const Pipeline &p, const Target &target, const AutoschedulerParams &params_in, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (const Func &f : p.outputs()) {
            outputs.push_back(f.function());
        }
        auto params = MctsParams::generic();
        {
            ParamParser parser(params_in.extra);
            parser.parse("parallelism", &params.parallelism);
            parser.parse("last_level_cache_size", &params.last_level_cache_size);
            parser.parse("balance", &params.balance);
            parser.finish();
        }
        Autoscheduler::generate_schedule(outputs, target, MctsParams::generic(), results);
    }
};

REGISTER_AUTOSCHEDULER(mcts)

// An alternative entrypoint for other uses
// void find_and_apply_schedule(FunctionDAG &dag,
//                              const std::vector<Function> &outputs,
//                              const MctsParams &params,
//                              CostModel *cost_model,
//                              int beam_size,
//                              int64_t memory_limit,
//                              StageMap<ScheduleFeatures> *schedule_features) {

//     std::mt19937 rng(12345);
//     IntrusivePtr<State> optimal = optimal_schedule(dag, outputs, params, cost_model, rng, beam_size, memory_limit);

//     // Apply the schedules
//     optimal->apply_schedule(dag, params);

//     if (schedule_features) {
//         optimal->compute_featurization(dag, params, schedule_features);
//     }
// }

}  // namespace Autoscheduler

// Intrusive shared ptr helpers.
template<>
RefCount &ref_count<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) {
    delete t;
}

}  // namespace Internal
}  // namespace Halide
