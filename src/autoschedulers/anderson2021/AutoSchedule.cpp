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

  HL_MACHINE_PARAMS
  An architecture description string. Used by Halide master to configure the cost model. We only use the first term. Set it to the number of SMs on the target GPU.

  HL_PERMIT_FAILED_UNROLL
  Set to 1 to tell Halide not to freak out if we try to unroll a loop that doesn't have a constant extent. Should generally not be necessary, but sometimes the autoscheduler's model for what will and will not turn into a constant during lowering is inaccurate, because Halide isn't perfect at constant-folding.

  HL_SCHEDULE_FILE
  *** DEPRECATED *** use the 'schedule' output from Generator instead
  Write out a human-and-machine readable block of scheduling source code for the selected schedule into this file.

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

  HL_SEARCH_SPACE_OPTIONS
  Allow/disallow search space options to be considered by the autoscheduler.
  Expects a string of four 0/1 values that allow/disallow the following options: compute root, inline, compute at the block level, compute at the thread level e.g. 1000 would allow compute root only

  HL_RANDOMIZE_TILINGS
  If set, only a random subset of the generated tilings for each stage will be accepted into the beam

  HL_FREEZE_INLINE_COMPUTE_ROOT
  If set, run a pre-pass where only compute_root and inline scheduling options are considered. The cheapest stages (according to the cost model) have these decisions 'frozen' for the remaining autoscheduling passes

  TODO: expose these settings by adding some means to pass args to
  generator plugins instead of environment vars.
*/
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
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
#include "HalidePlugin.h"
#include "LoopNest.h"
#include "LoopNestParser.h"
#include "NetworkSize.h"
#include "ParamParser.h"
#include "PerfectHashMap.h"
#include "State.h"

#ifdef _WIN32
#include <io.h>
#define _isatty isatty;
#endif

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct Anderson2021Params {
    /* Maximum level of parallelism available i.e. number of SMs on target GPU */
    int parallelism = 80;
};

using std::string;
using std::vector;

// Get the HL_RANDOM_DROPOUT environment variable. Purpose of this is described above.
double get_dropout_threshold() {
    string random_dropout_str = get_env_variable("HL_RANDOM_DROPOUT");
    if (!random_dropout_str.empty()) {
        return atof(random_dropout_str.c_str());
    } else {
        return 100;
    }
}

// Decide whether or not to drop a beam search state. Used for
// randomly exploring the search tree for autotuning and to generate
// training data.
bool random_dropout(std::mt19937 &rng, size_t num_decisions) {
    static double random_dropout_threshold = std::max(0.0, get_dropout_threshold());
    if (random_dropout_threshold >= 100) {
        return false;
    }

    // The random dropout threshold is the chance that we operate
    // entirely greedily and never discard anything.
    double t = random_dropout_threshold;
    t /= 100;
    t = std::pow(t, 1.0f / num_decisions);
    t *= 100;

    double r = rng() % 100;
    bool drop_it = r >= t;
    return drop_it;
}

// Get the HL_SEARCH_SPACE_OPTIONS environment variable. Described above
std::string get_search_space_options() {
    std::string options = get_env_variable("HL_SEARCH_SPACE_OPTIONS");
    if (options.empty()) {
        return "1111";
    }
    return options;
}

// Configure a cost model to process a specific pipeline.
void configure_pipeline_features(const FunctionDAG &dag,
                                 int hardware_parallelism,
                                 CostModel *cost_model) {
    cost_model->reset();
    cost_model->set_pipeline_features(dag, hardware_parallelism);
}

AutoSchedule::AutoSchedule(const FunctionDAG &dag,
                           int hardware_parallelism,
                           const Target &target,
                           const std::vector<Function> &outputs,
                           std::mt19937 &rng,
                           CostModel *cost_model,
                           Statistics &stats,
                           SearchSpace &search_space,
                           const LoopNestParser *partial_schedule)
    : dag{dag}, hardware_parallelism{hardware_parallelism}, target{target}, outputs{outputs}, rng{rng}, cost_model{cost_model}, stats{stats}, search_space{search_space}, partial_schedule{partial_schedule} {
    configure_pipeline_features(dag, hardware_parallelism, cost_model);
}

// A single pass of coarse-to-fine beam search.
IntrusivePtr<State> AutoSchedule::optimal_schedule_pass(int beam_size,
                                                        int pass_idx,
                                                        int num_passes,
                                                        ProgressBar &tick,
                                                        std::unordered_set<uint64_t> &permitted_hashes) {
    StateQueue q, pending;

    // The initial state, with no decisions made
    {
        IntrusivePtr<State> initial{new State};
        initial->root = new LoopNest;
        q.emplace(std::move(initial));
    }

    int expanded = 0;

    std::function<void(IntrusivePtr<State> &&)> enqueue_new_children =
        [&](IntrusivePtr<State> &&s) {
            // aslog(0) << "\n** Generated child: ";
            // s->dump();
            // s->calculate_cost(dag, params, nullptr, true);

            // Each child should have one more decision made than its parent state.
            internal_assert(s->num_decisions_made == s->parent->num_decisions_made + 1);

            int progress = s->num_decisions_made * beam_size + expanded;
            size_t max_progress = dag.nodes.size() * beam_size * 2;

            // Update the progress bar
            tick.set(double(progress) / max_progress);
            s->penalized = false;

            ++stats.num_states_added;

            // Add the state to the list of states to evaluate
            q.emplace(std::move(s));
        };

    string cyos_str = get_env_variable("HL_CYOS");
    string cyos_from_file_str = get_env_variable("HL_CYOS_FROM_FILE");
    bool cyos_from_file = !cyos_from_file_str.empty();
    bool cyos_is_enabled = cyos_from_file || cyos_str == "1";

    std::unique_ptr<LoopNestParser> target_loop_nest;
    if (cyos_from_file) {
        target_loop_nest = LoopNestParser::from_file(cyos_from_file_str);
    }

    // This loop is beam search over the sequence of decisions to make.
    for (int i = 0;; i++) {
        std::unordered_map<uint64_t, int> hashes;
        q.swap(pending);

        if (pending.empty()) {
            if ((false) && beam_size < 1000) {  // Intentional dead code. Extra parens to pacify clang-tidy.
                // Total mortality. Double the beam size and
                // restart. Disabled for now because total mortality
                // may indicate a bug.
                return optimal_schedule_pass(beam_size * 2,
                                             pass_idx,
                                             num_passes,
                                             tick,
                                             permitted_hashes);
            } else {
                internal_error << "Ran out of legal states with beam size " << beam_size << "\n";
            }
        }

        if ((int)pending.size() > beam_size * 10000) {
            aslog(0) << "Warning: Huge number of states generated (" << pending.size() << ").\n";
        }

        expanded = 0;
        while (expanded < beam_size && !pending.empty()) {

            IntrusivePtr<State> state{pending.pop()};

            if (beam_size > 1 && num_passes > 1 && pass_idx >= 0) {
                // We are doing coarse-to-fine beam search using the
                // hashing strategy mentioned in the paper.
                //
                // We will lazily apply cost penalties to the queue
                // according to structural uniqueness.
                if (!state->penalized) {
                    uint64_t h1 = state->structural_hash(pass_idx + 1);
                    uint64_t h0 = state->structural_hash(pass_idx - 1);
                    // We penalize the cost of a state proportionately
                    // to how many states we've already seen with that
                    // hash.
                    int penalty = ++hashes[h1];
                    if (pass_idx > 0 && !permitted_hashes.count(h0)) {
                        // It's possible to get yourself into a state
                        // where the only things in the beam that match
                        // the hash were quick-rejected due to details not
                        // captured in the hash, so we apply a huge
                        // penalty, but leave the impermissible state in
                        // the beam.
                        penalty += 10;
                    }
                    if (penalty > 1) {
                        state->penalized = true;
                        state->cost *= penalty;
                        for (auto &c : state->cost_per_stage) {
                            c *= penalty;
                        }
                        // After penalizing this state, if it's no
                        // longer the best, defer it. We set the
                        // 'penalized' flag so that we know not to
                        // penalize and defer it again.
                        if (!pending.empty() && state->cost > pending.top()->cost) {
                            pending.emplace(std::move(state));
                            continue;
                        }
                    }
                }
            }

            // Random dropout
            if (pending.size() > 1 && random_dropout(rng, dag.nodes.size() * 2)) {
                continue;
            }

            if (state->num_decisions_made == 2 * (int)dag.nodes.size()) {
                // We've reached the end of the pass. The first state
                // must be the best, because we're pulling off a
                // priority queue.
                auto best = state;

                // Bless the reasonable stuff in the beam as
                // permissible states to visit again. We define
                // reasonable as having a cost no more than 20% higher
                // than the cost of the best thing. Only do this if
                // there are more coarse-to-fine passes yet to come.
                if (pass_idx >= 0 && pass_idx + 1 < num_passes) {
                    int blessed = 0;
                    while (state->cost <= 1.2 * best->cost && blessed < beam_size) {
                        const State *s = state.get();
                        while (s) {
                            uint64_t h1 = s->structural_hash(pass_idx);
                            permitted_hashes.insert(h1);
                            s = s->parent.get();
                        }
                        if (pending.empty()) {
                            break;
                        }
                        state = pending.pop();
                        blessed++;
                    }
                }

                return best;
            }

            Timer timer;
            search_space.generate_children(state, enqueue_new_children, pass_idx, pass_idx == -1);
            stats.generate_children_time += timer.elapsed();
            expanded++;
        }

        // Drop the other states unconsidered.
        pending.clear();

        int cur_node = (q[0]->num_decisions_made - 1) / 2;
        const FunctionDAG::Node *node = &dag.nodes[cur_node];
        if (partial_schedule && partial_schedule->is_in_partial_schedule(node)) {
            bool found = false;
            for (int i = (int)q.size() - 1; i >= 0; i--) {
                auto state = q[i];
                LoopNestParser option = LoopNestParser::from_string(state->root->to_string());

                if (partial_schedule->contains_sub_loop_nest_for_shared_stages(option)) {
                    if (cost_model) {
                        cost_model->evaluate_costs();
                    }

                    auto selected = q[i];
                    q.clear();
                    q.emplace(std::move(selected));
                    found = true;
                    break;
                }
            }

            if (found) {
                continue;
            }

            aslog(0) << "Options:\n";
            for (int i = (int)q.size() - 1; i >= 0; i--) {
                auto state = q[i];
                LoopNestParser option = LoopNestParser::from_string(state->root->to_string());
                aslog(0) << "Option " << i << ":\n";
                option.dump();
            }
            aslog(0) << "\nTarget partial schedule:\n";
            partial_schedule->dump();
            internal_assert(false) << "Partial schedule not found";
        }

        if (cost_model) {
            // Now evaluate all the costs and re-sort them in the priority queue
            Timer timer;
            cost_model->evaluate_costs();
            stats.cost_model_evaluation_time += timer.elapsed();
            q.resort();
        }

        if (cyos_is_enabled) {
            int selection = -1;
            bool found = false;
            if (cyos_from_file) {
                for (int choice_label = (int)q.size() - 1; choice_label >= 0; choice_label--) {
                    auto state = q[choice_label];
                    LoopNestParser option = LoopNestParser::from_string(state->root->to_string());

                    if (target_loop_nest->contains_sub_loop_nest(option)) {
                        found = true;
                        selection = choice_label;
                        aslog(0) << "\nFound matching option\n";
                        break;
                    }
                }
            }

            if (!cyos_from_file || !found) {
                // The user has set HL_CYOS, and wants to navigate the
                // search space manually.  Discard everything in the queue
                // except for the user-chosen option.
                aslog(0) << "\n--------------------\n";
                aslog(0) << "Select a schedule:\n";
                for (int choice_label = (int)q.size() - 1; choice_label >= 0; choice_label--) {
                    auto state = q[choice_label];
                    aslog(0) << "\n[" << choice_label << "]:\n";
                    state->dump();
                }

                int next_node = q[0]->num_decisions_made / 2;
                if (next_node < (int)dag.nodes.size()) {
                    const FunctionDAG::Node *node = &dag.nodes[next_node];
                    aslog(0) << "\nNext node to be scheduled: " << node->func.name() << "\n";
                }
            }
            cost_model->evaluate_costs();

            if (cyos_from_file && !found) {
                aslog(0) << "\nTarget loop nest was not found.\n";
            }

            if (!cyos_from_file || !found) {
                // Select next partial schedule to expand.
                while (selection < 0 || selection >= (int)q.size()) {
                    aslog(0) << "\nEnter selection: ";
                    std::cin >> selection;
                }
            }

            auto selected = q[selection];
            selected->dump();
            q.clear();
            q.emplace(std::move(selected));
        }
    }
}

// Performance coarse-to-fine beam search and return the best state found.
IntrusivePtr<State> AutoSchedule::optimal_schedule(int beam_size) {
    IntrusivePtr<State> best;

    std::unordered_set<uint64_t> permitted_hashes;

    // If the beam size is one, it's pointless doing multiple passes.
    int num_passes = (beam_size == 1) ? 1 : 5;

    string cyos_str = get_env_variable("HL_CYOS");
    string cyos_from_file_str = get_env_variable("HL_CYOS_FROM_FILE");
    if (!cyos_from_file_str.empty()) {
        cyos_str = "1";
    }
    if (cyos_str == "1") {
        // If the user is manually navigating the search space, don't
        // ask them to do more than one pass.
        num_passes = 1;
    }

    string num_passes_str = get_env_variable("HL_NUM_PASSES");
    if (!num_passes_str.empty()) {
        // The user has requested a non-standard number of passes.
        num_passes = std::atoi(num_passes_str.c_str());
    }

    bool use_pre_pass = get_env_variable("HL_FREEZE_INLINE_COMPUTE_ROOT") == "1";
    int pass_idx = 0;

    if (use_pre_pass && num_passes > 1) {
        pass_idx = -1;
        --num_passes;
    }

    for (; pass_idx < num_passes; pass_idx++) {
        ProgressBar tick;

        auto pass = optimal_schedule_pass(beam_size, pass_idx, num_passes, tick, permitted_hashes);

        tick.clear();

        if (aslog::aslog_level() == 0) {
            aslog(0) << "Pass " << pass_idx + 1 << " of " << num_passes << ", cost: " << pass->cost << "\n";
        } else {
            aslog(0) << "Pass " << pass_idx + 1 << " result: ";
            pass->dump();
        }

        if (pass_idx == -1) {
            search_space.freeze_lowest_cost_stages(pass);
        }

        if (pass_idx >= 0 && (pass_idx == 0 || pass->cost < best->cost)) {
            // Track which pass produced the lowest-cost state. It's
            // not necessarily the final one.
            best = pass;
        }
    }

    aslog(0) << "Best cost: " << best->cost << "\n";

    return best;
}

// The main entrypoint to generate a schedule for a pipeline.
void generate_schedule(const std::vector<Function> &outputs,
                       const Target &target,
                       int hardware_parallelism,
                       AutoSchedulerResults *auto_scheduler_results) {
    internal_assert(target.has_gpu_feature()) << "Specified target (" << target.to_string() << ") does not support GPU";

    Timer timer;
    aslog(0) << "generate_schedule for target=" << target.to_string() << "\n";
    aslog(0) << "hardware_parallelism = " << hardware_parallelism << "\n";

    // Start a timer
    HALIDE_TIC;

    // Get the seed for random dropout
    string seed_str = get_env_variable("HL_SEED");
    // Or use the time, if not set.
    int seed = (int)time(nullptr);
    if (!seed_str.empty()) {
        seed = atoi(seed_str.c_str());
    }

    aslog(1) << "Dropout seed = " << seed << "\n";

    // Get the beam size
    string beam_size_str = get_env_variable("HL_BEAM_SIZE");
    // Defaults to 32
    size_t beam_size = 32;
    if (!beam_size_str.empty()) {
        beam_size = atoi(beam_size_str.c_str());
    }

    string weights_in_path = get_env_variable("HL_WEIGHTS_DIR");
    string weights_out_path;  // deliberately empty

    string randomize_weights_str = get_env_variable("HL_RANDOMIZE_WEIGHTS");
    bool randomize_weights = randomize_weights_str == "1";

    // Analyse the Halide algorithm and construct our abstract representation of it
    FunctionDAG dag(outputs, target);
    if (aslog::aslog_level() > 0) {
        dag.dump();
    }

    Statistics stats;

    // Construct a cost model to use to evaluate states. Currently we
    // just have the one, but it's an abstract interface, so others
    // can be slotted in for experimentation.
    std::unique_ptr<CostModel> cost_model = make_default_cost_model(stats, weights_in_path, weights_out_path, randomize_weights);
    internal_assert(cost_model != nullptr);

    IntrusivePtr<State> optimal;

    string partial_schedule_filename = get_env_variable("PARTIAL_SCHEDULE");
    std::unique_ptr<LoopNestParser> partial_schedule;
    if (!partial_schedule_filename.empty()) {
        aslog(0) << "Loading partial schedule from " << partial_schedule_filename << "\n";
        partial_schedule = LoopNestParser::from_file(partial_schedule_filename);
        aslog(0) << "Partial schedule:\n";
        partial_schedule->dump();
        aslog(0) << "\n";
    }

    std::mt19937 rng{(uint32_t)seed};
    SearchSpace search_space{dag, hardware_parallelism, target, get_search_space_options(), rng, cost_model.get(), stats, partial_schedule.get()};

    AutoSchedule autoschedule{dag, hardware_parallelism, target, outputs, rng, cost_model.get(), stats, search_space, partial_schedule.get()};

    // Run beam search
    optimal = autoschedule.optimal_schedule(beam_size);

    HALIDE_TOC;

    // Dump the schedule found
    aslog(1) << "** Optimal schedule:\n";

    // Just to get the debugging prints to fire
    optimal->calculate_cost(dag, hardware_parallelism, target, cost_model.get(), stats, aslog::aslog_level() > 0);

    // Apply the schedules to the pipeline
    optimal->apply_schedule(dag, hardware_parallelism, target);

    // Print out the schedule
    if (aslog::aslog_level() > 0) {
        aslog(0) << "BEGIN Final generated loop nest and schedule:\n";
        optimal->dump();
        aslog(0) << "END Final generated loop nest and schedule\n";
        optimal->print_compute_locations();
    }

    string schedule_file = get_env_variable("HL_SCHEDULE_FILE");
    if (!schedule_file.empty()) {
        user_warning << "HL_SCHEDULE_FILE is deprecated; use the schedule output from Generator instead\n";
        aslog(1) << "Writing schedule to " << schedule_file << "...\n";
        std::ofstream f(schedule_file);
        f << "// --- BEGIN machine-generated schedule\n"
          << optimal->schedule_source
          << "// --- END machine-generated schedule\n";
        f.close();
        internal_assert(!f.fail()) << "Failed to write " << schedule_file;
    }

    // Save the featurization, so that we can use this schedule as
    // training data (once we've benchmarked it).
    string feature_file = get_env_variable("HL_FEATURE_FILE");
    if (!feature_file.empty()) {
        user_warning << "HL_FEATURE_FILE is deprecated; use the featurization output from Generator instead\n";
        std::ofstream binfile(feature_file, std::ios::binary | std::ios_base::trunc);
        optimal->save_featurization(dag, hardware_parallelism, target, binfile);
        binfile.close();
        internal_assert(!binfile.fail()) << "Failed to write " << feature_file;
    }

    if (auto_scheduler_results) {
#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
        auto_scheduler_results->scheduler_name = "Anderson2021";
#endif
        auto_scheduler_results->schedule_source = optimal->schedule_source;
        {
            std::ostringstream out;
            optimal->save_featurization(dag, hardware_parallelism, target, out);
            auto_scheduler_results->featurization.resize(out.str().size());
            memcpy(auto_scheduler_results->featurization.data(), out.str().data(), out.str().size());
        }
    }

    aslog(1) << "Number of states added: " << stats.num_states_added << '\n';
    aslog(1) << "Number of featurizations computed: " << stats.num_featurizations << '\n';
    aslog(1) << "Number of memoization hits: " << stats.num_memoization_hits << '\n';
    aslog(1) << "Number of memoization misses: " << stats.num_memoization_misses << '\n';
    aslog(1) << "Number of block memoization hits: " << stats.num_block_memoization_hits << '\n';
    aslog(1) << "Number of block memoization misses: " << stats.num_block_memoization_misses << '\n';
    aslog(1) << "Total featurization time (ms): " << stats.total_featurization_time() << "\n";
    aslog(1) << "Average featurization time (ms): " << stats.average_featurization_time() << "\n";
    aslog(1) << "Total enqueue time (ms): " << stats.total_enqueue_time() << "\n";
    aslog(1) << "Total calculate cost time (ms): " << stats.total_calculate_cost_time() << "\n";
    aslog(1) << "Total feature write time (ms): " << stats.total_feature_write_time() << "\n";
    aslog(1) << "Total generate children time (ms): " << stats.total_generate_children_time() << "\n";
    aslog(1) << "Total compute in tiles time (ms): " << stats.total_compute_in_tiles_time() << "\n";
    aslog(1) << "Total filter thread tiles time (ms): " << stats.total_filter_thread_tiles_time() << "\n";
    aslog(1) << "Total filter parallel tiles time (ms): " << stats.total_filter_parallel_tiles_time() << "\n";

    aslog(1) << "Number of schedules evaluated by cost model: " << stats.num_schedules_enqueued << '\n';
    aslog(1) << "Number of tilings generated: " << stats.num_tilings_generated << '\n';
    aslog(1) << "Number of tilings accepted: " << stats.num_tilings_accepted << '\n';
    aslog(1) << "Total cost model evaluation time (ms): " << stats.total_cost_model_evaluation_time() << "\n";
    aslog(1) << "Average cost model evaluation time (ms): " << stats.average_cost_model_evaluation_time() << "\n";
    std::chrono::duration<double> total_time = timer.elapsed();
    aslog(1) << "Time taken for autoscheduler (s): " << std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count() / 1000.0 << '\n';
}

// Halide uses a plugin architecture for registering custom
// autoschedulers. We register our autoscheduler using a static
// constructor.
//struct RegisterAutoscheduler {
    //RegisterAutoscheduler() {
        //aslog(1) << "Registering autoscheduler 'Anderson2021'...\n";
        //Pipeline::add_autoscheduler("Anderson2021", *this);
    //}

    //void operator()(const Pipeline &p, const Target &target, const MachineParams &params, AutoSchedulerResults *results) {
        //std::vector<Function> outputs;
        //for (const Func& f : p.outputs()) {
            //outputs.push_back(f.function());
        //}
        //Autoscheduler::generate_schedule(outputs, target, params.parallelism, results);
    //}
//} register_auto_scheduler;

struct Anderson2021 {
#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
    void operator()(const Pipeline &p, const Target &target, const MachineParams &params_in, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (const Func &f : p.outputs()) {
            outputs.push_back(f.function());
        }
        Anderson2021Params params;
        params.parallelism = params_in.parallelism;
        Autoscheduler::generate_schedule(outputs, target, params.parallelism, results);
    }
#else
    void operator()(const Pipeline &p, const Target &target, const AutoschedulerParams &params_in, AutoSchedulerResults *results) {
        internal_assert(params_in.name == "Anderson2021");

        std::vector<Function> outputs;
        for (const Func &f : p.outputs()) {
            outputs.push_back(f.function());
        }
        Anderson2021Params params;
        {
            ParamParser parser(params_in.extra);
            parser.parse("parallelism", &params.parallelism);
            parser.finish();
        }
        Autoscheduler::generate_schedule(outputs, target, params.parallelism, results);
        results->autoscheduler_params = params_in;
    }
#endif
};

REGISTER_AUTOSCHEDULER(Anderson2021)

// An alternative entrypoint for other uses
void find_and_apply_schedule(FunctionDAG &dag,
                             const std::vector<Function> &outputs,
                             int hardware_parallelism,
                             const Target &target,
                             CostModel *cost_model,
                             int beam_size,
                             StageMap<ScheduleFeatures> *schedule_features) {

    Statistics stats;
    std::mt19937 rng{(uint32_t)12345};

    string partial_schedule_filename = get_env_variable("PARTIAL_SCHEDULE");
    std::unique_ptr<LoopNestParser> partial_schedule;
    if (!partial_schedule_filename.empty()) {
        aslog(0) << "Loading partial schedule from " << partial_schedule_filename << "\n";
        partial_schedule = LoopNestParser::from_file(partial_schedule_filename);
        aslog(0) << "Partial schedule:\n";
        partial_schedule->dump();
        aslog(0) << "\n";
    }

    SearchSpace search_space{dag, hardware_parallelism, target, get_env_variable("HL_SEARCH_SPACE_OPTIONS"), rng, cost_model, stats, partial_schedule.get()};
    AutoSchedule autoschedule{dag, hardware_parallelism, target, outputs, rng, cost_model, stats, search_space, partial_schedule.get()};

    IntrusivePtr<State> optimal = autoschedule.optimal_schedule(beam_size);

    // Apply the schedules
    optimal->apply_schedule(dag, hardware_parallelism, target);

    if (schedule_features) {
        optimal->compute_featurization(dag, hardware_parallelism, target, schedule_features, stats);
    }
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
