/*
  This file is the core of the autoscheduler. Most of the code here is
  about navigating the search space and computing the
  featurization. This also contains the top-level interface into the
  autoscheduler.

  The most interesting classes to look at are:

  LoopNest               Represents one node in our tree representation of loop nests. (Now in LoopNest.(h | cpp)).
  State                  A state in the beam search. Holds a root loop nest. (Now in State.(h | cpp)).

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
  An architecture description string. Used by Halide master to configure the cost model. We only use the first term. Set it to the number of cores to target.

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

  HL_AUTOSCHEDULE_MEMORY_LIMIT
  If set, only consider schedules that allocate at most this much memory (measured in bytes).

  HL_DISABLE_MEMOIZED_FEATURES
  If set, features of possible schedules are always recalculated, and are not cached across passes.
  (see Cache.h for more information)

  HL_DISABLE_MEMOIZED_BLOCKS
  If set, then tiling sizes are not cached across passes.
  (see Cache.h for more information)

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
#include "Cache.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Errors.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "LoopNest.h"
#include "NetworkSize.h"
#include "PerfectHashMap.h"
#include "State.h"
#include "Timer.h"

#ifdef _WIN32
#include <io.h>
#define _isatty isatty;
#endif

namespace Halide {
namespace Internal {
namespace Autoscheduler {

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

// Get the HL_RANDOM_DROPOUT environment variable. Purpose of this is described above.
uint32_t get_dropout_threshold() {
    string random_dropout_str = get_env_variable("HL_RANDOM_DROPOUT");
    if (!random_dropout_str.empty()) {
        return atoi(random_dropout_str.c_str());
    } else {
        return 100;
    }
}

// Decide whether or not to drop a beam search state. Used for
// randomly exploring the search tree for autotuning and to generate
// training data.
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

// A priority queue of states, sorted according to increasing
// cost. Never shrinks, to avoid reallocations.
// Can't use std::priority_queue because it doesn't support unique_ptr.
class StateQueue {
private:
    struct CompareStates {
        bool operator()(const IntrusivePtr<State> &a, const IntrusivePtr<State> &b) const {
            return a->cost > b->cost;
        }
    };

    std::vector<IntrusivePtr<State>> storage;
    size_t sz = 0;

public:
    void emplace(IntrusivePtr<State> &&s) {
        if (sz >= storage.size()) {
            storage.resize(std::max(sz * 2, (size_t)64));
        }
        internal_assert(sz < storage.size()) << sz << " " << storage.size() << "\n";
        storage[sz] = std::move(s);
        sz++;
        std::push_heap(storage.begin(), storage.begin() + sz, CompareStates{});
    }

    IntrusivePtr<State> pop() {
        internal_assert(sz <= storage.size()) << sz << " " << storage.size() << "\n";
        std::pop_heap(storage.begin(), storage.begin() + sz, CompareStates{});
        sz--;
        return std::move(storage[sz]);
    }

    const IntrusivePtr<State> &top() {
        return storage[0];
    }

    bool empty() const {
        return sz == 0;
    }

    size_t size() const {
        return sz;
    }

    void swap(StateQueue &other) {
        storage.swap(other.storage);
        std::swap(sz, other.sz);
    }

    IntrusivePtr<State> operator[](int idx) const {
        return storage[idx];
    }

    void resort() {
        std::make_heap(storage.begin(), storage.begin() + sz, CompareStates{});
    }

    void clear() {
        for (size_t i = 0; i < sz; i++) {
            storage[i] = IntrusivePtr<State>{};
        }
        sz = 0;
    }
};

// Configure a cost model to process a specific pipeline.
void configure_pipeline_features(const FunctionDAG &dag,
                                 const MachineParams &params,
                                 CostModel *cost_model) {
    cost_model->reset();
    cost_model->set_pipeline_features(dag, params);
}

// A single pass of coarse-to-fine beam search.
IntrusivePtr<State> optimal_schedule_pass(FunctionDAG &dag,
                                          const vector<Function> &outputs,
                                          const MachineParams &params,
                                          CostModel *cost_model,
                                          std::mt19937 &rng,
                                          int beam_size,
                                          int64_t memory_limit,
                                          int pass_idx,
                                          int num_passes,
                                          ProgressBar &tick,
                                          std::unordered_set<uint64_t> &permitted_hashes,
                                          Cache *cache) {

    if (cost_model) {
        configure_pipeline_features(dag, params, cost_model);
    }

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

            // Add the state to the list of states to evaluate
            q.emplace(std::move(s));
        };

    string cyos_str = get_env_variable("HL_CYOS");

    // This loop is beam search over the sequence of decisions to make.
    for (int i = 0;; i++) {
        std::unordered_map<uint64_t, int> hashes;
        q.swap(pending);

        if (pending.empty()) {
            if ((false) && beam_size < 1000) {  // Intentional dead code. Extra parens to pacify clang-tidy.
                // Total mortality. Double the beam size and
                // restart. Disabled for now because total mortality
                // may indicate a bug.
                return optimal_schedule_pass(dag,
                                             outputs,
                                             params,
                                             cost_model,
                                             rng,
                                             beam_size * 2,
                                             memory_limit,
                                             pass_idx,
                                             num_passes,
                                             tick,
                                             permitted_hashes,
                                             cache);
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

            if (beam_size > 1 && num_passes > 1) {
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
                if (pass_idx + 1 < num_passes) {
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

            state->generate_children(dag, params, cost_model, memory_limit, enqueue_new_children, cache);
            expanded++;
        }

        // Drop the other states unconsidered.
        pending.clear();

        if (cost_model) {
            // Now evaluate all the costs and re-sort them in the priority queue
            cost_model->evaluate_costs();
            q.resort();
        }

        if (cyos_str == "1") {
            // The user has set HL_CYOS, and wants to navigate the
            // search space manually.  Discard everything in the queue
            // except for the user-chosen option.
            aslog(0) << "\n--------------------\n";
            aslog(0) << "Select a schedule:\n";
            for (int choice_label = (int)q.size() - 1; choice_label >= 0; choice_label--) {
                auto state = q[choice_label];
                aslog(0) << "\n[" << choice_label << "]:\n";
                state->dump();
                state->calculate_cost(dag, params, cost_model, cache->options, memory_limit, true);
            }
            cost_model->evaluate_costs();

            // Select next partial schedule to expand.
            int selection = -1;
            while (selection < 0 || selection >= (int)q.size()) {
                aslog(0) << "\nEnter selection: ";
                std::cin >> selection;
            }

            auto selected = q[selection];
            selected->dump();
            q.clear();
            q.emplace(std::move(selected));
        }
    }
}

// Performance coarse-to-fine beam search and return the best state found.
IntrusivePtr<State> optimal_schedule(FunctionDAG &dag,
                                     const vector<Function> &outputs,
                                     const MachineParams &params,
                                     CostModel *cost_model,
                                     std::mt19937 &rng,
                                     int beam_size,
                                     int64_t memory_limit,
                                     const CachingOptions &options) {

    IntrusivePtr<State> best;

    std::unordered_set<uint64_t> permitted_hashes;

    // Set up cache with options and size.
    Cache cache(options, dag.nodes.size());

    // If the beam size is one, it's pointless doing multiple passes.
    int num_passes = (beam_size == 1) ? 1 : 5;

    string cyos_str = get_env_variable("HL_CYOS");
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

    for (int i = 0; i < num_passes; i++) {
        ProgressBar tick;

        Timer timer;

        auto pass = optimal_schedule_pass(dag, outputs, params, cost_model,
                                          rng, beam_size, memory_limit,
                                          i, num_passes, tick, permitted_hashes, &cache);

        std::chrono::duration<double> total_time = timer.elapsed();
        auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();

        tick.clear();

        if (aslog::aslog_level() == 0) {
            aslog(0) << "Pass " << i << " of " << num_passes << ", cost: " << pass->cost << ", time (ms): " << milli << "\n";
        } else {
            aslog(0) << "Pass " << i << " result: ";
            pass->dump();
        }

        if (i == 0 || pass->cost < best->cost) {
            // Track which pass produced the lowest-cost state. It's
            // not necessarily the final one.
            best = pass;
        }
    }

    aslog(0) << "Best cost: " << best->cost << "\n";

    if (options.cache_blocks) {
        aslog(0) << "Cache (block) hits: " << cache.cache_hits << "\n";
        aslog(0) << "Cache (block) misses: " << cache.cache_misses << "\n";
    }

    return best;
}

// Keep track of how many times we evaluated a state.
int State::cost_calculations = 0;

// The main entrypoint to generate a schedule for a pipeline.
void generate_schedule(const std::vector<Function> &outputs,
                       const Target &target,
                       const MachineParams &params,
                       AutoSchedulerResults *auto_scheduler_results) {
    aslog(0) << "generate_schedule for target=" << target.to_string() << "\n";

    // Start a timer
    HALIDE_TIC;

    State::cost_calculations = 0;

    // Get the seed for random dropout
    string seed_str = get_env_variable("HL_SEED");
    // Or use the time, if not set.
    int seed = (int)time(nullptr);
    if (!seed_str.empty()) {
        seed = atoi(seed_str.c_str());
    }
    aslog(1) << "Dropout seed = " << seed << "\n";
    std::mt19937 rng((uint32_t)seed);

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

    string memory_limit_str = get_env_variable("HL_AUTOSCHEDULE_MEMORY_LIMIT");
    int64_t memory_limit = memory_limit_str.empty() ? (uint64_t)(-1) : std::atoll(memory_limit_str.c_str());

    // Analyse the Halide algorithm and construct our abstract representation of it
    FunctionDAG dag(outputs, params, target);
    if (aslog::aslog_level() > 0) {
        dag.dump();
    }

    // Construct a cost model to use to evaluate states. Currently we
    // just have the one, but it's an abstract interface, so others
    // can be slotted in for experimentation.
    std::unique_ptr<CostModel> cost_model = make_default_cost_model(weights_in_path, weights_out_path, randomize_weights);
    internal_assert(cost_model != nullptr);

    IntrusivePtr<State> optimal;

    // Options generated from environment variables, decide whether or not to cache features and/or tilings.
    CachingOptions cache_options = CachingOptions::MakeOptionsFromEnviron();

    // Run beam search
    optimal = optimal_schedule(dag, outputs, params, cost_model.get(), rng, beam_size, memory_limit, cache_options);

    HALIDE_TOC;

    aslog(1) << "Cost evaluated this many times: " << State::cost_calculations << "\n";

    // Dump the schedule found
    aslog(1) << "** Optimal schedule:\n";

    // Just to get the debugging prints to fire
    optimal->calculate_cost(dag, params, cost_model.get(), cache_options, memory_limit, aslog::aslog_level() > 0);

    // Apply the schedules to the pipeline
    optimal->apply_schedule(dag, params);

    // Print out the schedule
    if (aslog::aslog_level() > 0) {
        optimal->dump();
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
        optimal->save_featurization(dag, params, cache_options, binfile);
        binfile.close();
        internal_assert(!binfile.fail()) << "Failed to write " << feature_file;
    }

    if (auto_scheduler_results) {
        auto_scheduler_results->scheduler_name = "Adams2019";
        auto_scheduler_results->schedule_source = optimal->schedule_source;
        {
            std::ostringstream out;
            optimal->save_featurization(dag, params, cache_options, out);
            auto_scheduler_results->featurization.resize(out.str().size());
            memcpy(auto_scheduler_results->featurization.data(), out.str().data(), out.str().size());
        }
    }
}

struct Adams2019 {
    void operator()(const Pipeline &p, const Target &target, const MachineParams &params, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (const Func &f : p.outputs()) {
            outputs.push_back(f.function());
        }
        Autoscheduler::generate_schedule(outputs, target, params, results);
    }
};

REGISTER_AUTOSCHEDULER(Adams2019)

// An alternative entrypoint for other uses
void find_and_apply_schedule(FunctionDAG &dag,
                             const std::vector<Function> &outputs,
                             const MachineParams &params,
                             CostModel *cost_model,
                             int beam_size,
                             int64_t memory_limit,
                             StageMap<ScheduleFeatures> *schedule_features) {

    std::mt19937 rng(12345);
    CachingOptions cache_options = CachingOptions::MakeOptionsFromEnviron();
    IntrusivePtr<State> optimal = optimal_schedule(dag, outputs, params, cost_model, rng, beam_size, memory_limit, cache_options);

    // Apply the schedules
    optimal->apply_schedule(dag, params);

    if (schedule_features) {
        optimal->compute_featurization(dag, params, schedule_features, cache_options);
    }
}

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

template<>
RefCount &ref_count<Autoscheduler::State>(const Autoscheduler::State *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<Autoscheduler::State>(const Autoscheduler::State *t) {
    delete t;
}

}  // namespace Internal
}  // namespace Halide
