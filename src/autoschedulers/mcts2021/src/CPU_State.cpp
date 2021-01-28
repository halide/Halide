#include "CPU_State.h"
#include <algorithm>        // std::min
#include <iostream>         // std::cerr
#include <map>

using std::vector;
using std::map;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

void compute_featurization(const FunctionDAG *dag, const MachineParams *params, StageMap<ScheduleFeatures> *features) {

}

vector<CPU_Action> CPU_State::generate_possible_actions() const {
    dump();
    vector<CPU_Action> actions;
    if (is_terminal()) {
        // This is a leaf node.
        return actions;
    }

    // TODO(rootjalex): understand this better.
    int next_node = n_decisions_made / 2;
    int phase = n_decisions_made % 2;

    if (!may_subtile()) {
        // When emulating the older search space, we do all
        // parallelizing last, so that it is independent of the
        // tiling decisions.
        next_node = n_decisions_made % dag_ptr->nodes.size();
        phase = n_decisions_made / dag_ptr->nodes.size();
    }

    // Enumerate all legal ways to schedule the next Func
    const FunctionDAG::Node *node = &dag_ptr->nodes[next_node];

    for (const auto *e : node->outgoing_edges) {
        internal_assert(root->computes(e->consumer->node))
            << "Partially scheduled code doesn't compute " << e->consumer->name
            << ", which is one of the consumers of " << node->func.name();
    }

    if (node->is_input) {
        // We don't need to schedule nodes that represent inputs,
        // and there are no other decisions to be made about them
        // at this time.
        actions.push_back(CPU_Action(CPU_ScheduleAction::Input, root));
        return actions;
    }

    if (!node->outgoing_edges.empty() && !root->calls(node)) {
        debug(0) << "In state:\n";
        // TODO(rootjalex): make a dump() function
        // dump();
        debug(0) << node->func.name() << " is consumed by:\n";
        for (const auto *e : node->outgoing_edges) {
            debug(0) << e->consumer->name << "\n";
            debug(0) << "Which in turn consumes:\n";
            for (const auto *e2 : e->consumer->incoming_edges) {
                debug(0) << "  " << e2->producer->func.name() << "\n";
            }
        }
        internal_error << "Pipeline so far doesn't use next Func: " << node->func.name() << '\n';
    }

    if (phase == 0) {
        // Injecting realizations.
        actions = generate_injected_realizations(node);
    } else {
        // We are parallelizing the loops of the func we just injected a realization for.
        actions = generate_parallel_realizations(node);
    }

    if (actions.empty()) {
        aslog(0) << "Warning: Found no legal way to schedule "
                    << node->func.name() << " in the following State:\n";
        dump();
        // This State generated no children. Maybe other states have had
        // children. Carry on.
    }

    return actions;
}

vector<CPU_Action> CPU_State::generate_injected_realizations(const FunctionDAG::Node *node) const {
    vector<CPU_Action> actions;

    // First, try to inline this func.
    if (node->stages.size() == 1 && !node->is_output) {
        LoopNest *new_root = new LoopNest;
        new_root->copy_from(*root);
        new_root->inline_func(node);
        if (!prunable(dag_ptr, params_ptr, new_root, memory_limit)) {
            actions.push_back(CPU_Action(CPU_ScheduleAction::Inline, new_root));
        } else {
            // TODO(rootjalex): is this a leak in adams2019?
            delete new_root;
        }
    }

    // TODO(rootjalex): *IMPORTANT* Ask Andrew/Luke/somebody about the
    // (num_children > 0) part - we may need to prune above to decide if
    // inlining is even possible...

    // Some search-space pruning. If a node is pointwise, and
    // so are all its inputs and so is its sole output, and
    // inlining it is legal, just inline it. This saves time
    // on long chains of pointwise things.
    bool must_inline = (node->is_pointwise &&
                        (actions.size() > 0) &&
                        (node->outgoing_edges.size() == 1));
    if (must_inline) {
        for (const auto *e : node->stages[0].incoming_edges) {
            must_inline &= e->producer->is_pointwise;
        }
        for (const auto *e : node->outgoing_edges) {
            must_inline &= (e->consumer->node->is_pointwise ||
                            e->consumer->node->is_boundary_condition);
        }
        if (must_inline) {
            return actions;
        }
    }


    // Construct a list of plausible dimensions to vectorize
    // over. Currently all of them. TODO: Pre-prune the list
    // of sane dimensions to vectorize a Func over to reduce
    // branching factor.
    vector<int> vector_dims;
    if (!node->is_input && !node->is_output) {
        for (int v = 0; v < node->dimensions; v++) {
            const auto &p = root->get_bounds(node)->region_computed(v);
            if (p.extent() >= node->vector_size) {
                vector_dims.push_back(v);
            }
        }
    }

    // Outputs must be vectorized over their innermost
    // dimension, because we don't have control of the
    // storage. Infer which dimension(s) is(are) the innermost one(s) by
    // looking at the stride. Note that there can be more than one in
    // case some dimensions have an extent of 1.
    if (node->is_output && !node->func.output_buffers().empty()) {
        const Parameter &output = node->func.output_buffers()[0];
        int num_dims = output.dimensions();
        for (int i = 0; i < num_dims; ++i) {
            const Expr stride = output.stride_constraint(i);
            const int64_t *s = as_const_int(stride);
            if (s && *s == 1) {
                vector_dims.push_back(i);
            }
        }
    }

    if (vector_dims.empty()) {
        // This can happen if the output strides aren't known, or if all
        // the dimensions are smaller than the vector size.
        // TBD: consider extending compute_in_tiles to support -1 as a
        // vector dim to indicate no vectorization.
        for (int v = 0; v < node->dimensions; v++) {
            vector_dims.push_back(v);
        }
        // Handle the case of full reductions that generate a scalar.
        // We need at least one vector dimension to call cmopute_in_tiles
        // below.
        // TBD: figure out a better fallback strategy.
        if (vector_dims.empty()) {
            vector_dims.push_back(0);
        }
    }

    // Realize it somewhere.
    for (int vector_dim : vector_dims) {
        auto tile_options = root->compute_in_tiles(node, /* parent */nullptr, *params_ptr, vector_dim, /* in_realization */false);
        for (IntrusivePtr<const LoopNest> &n : tile_options) {
            // TODO(rootjalex): do we want to prune here too? Or prune later?
            //                  We may screw up the exploration value by not pruning now.
            //                  but we might save a lot of time by not preemptively pruning.
            actions.push_back(CPU_Action(CPU_ScheduleAction::Vectorize, std::move(n)));
        }
    }

    return actions;
}

// phase == 1 in generate_children()
vector<CPU_Action> CPU_State::generate_parallel_realizations(const FunctionDAG::Node *node) const {
    vector<CPU_Action> actions;

    // TODO(rootjalex): understand this well enough to write a description.
    bool should_parallelize = false;
    const vector<int64_t> *pure_size = nullptr;
    if (params_ptr->parallelism > 1) {
        for (const auto &c : root->children) {
            if (c->node == node && node->dimensions > 0) {
                if (c->stage->index == 0) {
                    pure_size = &(c->size);
                }
                should_parallelize = true;
            }
        }
    }


    if (!should_parallelize) {
        // The Func must be scalar, or not compute_root, or
        // we're not asking to use multiple cores.  Just
        // return a copy of the parent state
        actions.push_back(CPU_Action(CPU_ScheduleAction::Parallelize, root));
    } else {
        internal_assert(pure_size) << "generate_parallel_realizations did not find pure_size\n";

        // Generate some candidate parallel task shapes.
        auto tilings = generate_tilings(*pure_size, node->dimensions - 1, 2, true);

        // We could also just parallelize the outer loop entirely
        std::vector<int64_t> ones;
        ones.resize(pure_size->size(), 1);
        tilings.emplace_back(std::move(ones));

        // Sort / filter the options
        struct Option {
            vector<int64_t> tiling;
            double idle_core_wastage;
            bool entire;
            bool operator<(const Option &other) const {
                return idle_core_wastage < other.idle_core_wastage;
            }
            // Ensure we don't accidentally copy this type
            Option() = default;
            Option(Option &&) = default;
            Option &operator=(Option &&) = default;
            Option(const Option &) = delete;
            Option &operator=(const Option &) = delete;
        };

        // TODO(rootjalex): is there a way to make this less expensive??
        vector<Option> options;
        for (size_t i = 0; i < tilings.size(); i++) {
            auto &t = tilings[i];
            Option o;
            o.entire = (i == tilings.size() - 1);

            for (size_t j = 0; j < pure_size->size(); j++) {
                t[j] = ((*pure_size)[j] + t[j] - 1) / t[j];
            }
            t.swap(o.tiling);

            // Compute max idle cores across the other stages of the Func
            int64_t min_total = 0, max_total = 0;
            o.idle_core_wastage = 1;
            for (const auto &c : root->children) {
                if (c->node == node) {
                    int64_t total = 1;
                    for (const auto &l : c->stage->loop) {
                        if (!l.rvar) {
                            total *= o.tiling[l.pure_dim];
                        }
                    }
                    if (min_total != 0) {
                        min_total = std::min(min_total, total);
                    } else {
                        min_total = total;
                    }
                    max_total = std::max(max_total, total);
                    const double tasks_per_core = ((double)total) / params_ptr->parallelism;
                    o.idle_core_wastage = std::max(o.idle_core_wastage,
                                                    std::ceil(tasks_per_core) /
                                                        tasks_per_core);
                }
            }

            // Filter out the less useful options
            bool ok =
                ((o.entire || min_total >= params_ptr->parallelism) &&
                    (max_total <= params_ptr->parallelism * 16));

            if (!ok) {
                continue;
            }

            options.emplace_back(std::move(o));
        }
        std::sort(options.begin(), options.end());

        // If none of the options were acceptable, don't
        // parallelize. This tends to happen for things like
        // compute_root color matrices.
        if (options.empty()) {
            actions.push_back(CPU_Action(CPU_ScheduleAction::ComputeRoot, root));
            return actions;
        }

        for (const auto &o : options) {
            if (actions.size() >= 1 && (o.idle_core_wastage > 1.2 || !may_subtile())) {
                // We have considered several options, and the
                // remaining ones leave lots of cores idle.
                break;
            }

            // TODO(rootjalex): Can we delay these calculations until an action is chosen?

            LoopNest *new_root = new LoopNest;
            new_root->copy_from(*root);
            for (auto &c : new_root->children) {
                if (c->node == node) {
                    if (may_subtile()) {
                        c = c->parallelize_in_tiles(*params_ptr, o.tiling, new_root);
                    } else {
                        // We're emulating the old
                        // autoscheduler for an ablation, so
                        // emulate its parallelism strategy:
                        // just keep parallelizing outer loops
                        // until enough are parallel.
                        vector<int64_t> tiling = c->size;
                        int64_t total = 1;
                        for (size_t i = c->size.size(); i > 0; i--) {
                            if (!c->stage->loop[i - 1].pure || total >= params_ptr->parallelism) {
                                tiling[i - 1] = 1;
                            }
                            while (tiling[i - 1] > 1 &&
                                    total * tiling[i - 1] > params_ptr->parallelism * 8) {
                                tiling[i - 1] /= 2;
                            }
                            total *= tiling[i - 1];
                        }
                        c = c->parallelize_in_tiles(*params_ptr, tiling, new_root);
                    }
                }
            }

            // TODO(rootjalex): Once again, should we prune here? Or is it fine to delay?
            actions.push_back(CPU_Action(CPU_ScheduleAction::Tile, new_root));
        }
    }

    return actions;
}

CPU_State CPU_State::take_action(const CPU_Action &action) const {
    CPU_State next_state(dag_ptr, params_ptr, model_ptr, action.root, n_decisions_made + 1, memory_limit);
    // TODO(rootjalex): Some delayed calculations may need to be inserted here.
    if (action.schedule_action == CPU_ScheduleAction::Inline) {
        next_state.prepruned = true;
    }
    return next_state; // for now, simply return.
}

double CPU_State::get_value() const {
    return minimum_cost;
}

bool CPU_State::is_terminal() const {
    // TODO(rootjalex): Save this as a value, node size can't change afaik.
    // TODO(rootjalex): Are there other teminal conditions?
    //                  What if we run out of actions?
    return (n_decisions_made == (2 * dag_ptr->nodes.size()));
}

double CPU_State::calculate_cost() const {
    if (!prepruned && prunable(dag_ptr, params_ptr, root.get(), memory_limit)) {
        return std::numeric_limits<double>::max();
    }
    StageMap<ScheduleFeatures> features;
    double cost = 0.0f;
    compute_featurization(dag_ptr, params_ptr, &features);

    // TODO(rootjalex): not batching might be slow, but is there any better way?
    model_ptr->enqueue(*dag_ptr, features, &cost);
    model_ptr->evaluate_costs();
    return cost;
}

bool CPU_State::update(double &cost_value) {
    // We want the minimum cost of any child node.
    if (minimum_cost > cost_value) {
        minimum_cost = cost_value;
        return true;
    }
    // No update, so no need to continue back prop.
    return false;
}

double CPU_State::get_exploitation_value(uint32_t num_visits) {
    // We are not using an average, we are using a minimum cost.
    // Exploitation cost should be higher when this is better to explore.
    return -minimum_cost;
}

void CPU_State::dump() const {
    std::cerr << "root:" << root.get()
                << "\nn_decisions_made:" << n_decisions_made
                << "\nminimum_cost:" << minimum_cost
                << "\ndag_ptr:" << dag_ptr
                << "\nparams_ptr:" << params_ptr
                << "\nmodel_ptr:" << model_ptr << "\n";
}

// This code is taken from the State::calculate_cost() code in the adams2019 autoscheduler.
bool prunable(const FunctionDAG *dag_ptr, const MachineParams *params_ptr, const LoopNest *root_ptr, int64_t memory_limit) {
    StageMap<ScheduleFeatures> features;
    compute_featurization(dag_ptr, params_ptr, root_ptr, &features);

    // TODO(rootjalex): add a verbose dump

    for (auto it = features.begin(); it != features.end(); it++) {
        if (!it.key()->node->is_wrapper) {
            // It's OK to repeatedly stage data
            auto &feat = it.value();
            // TODO(rootjalex): Is this arbitrary?
            if (feat.points_computed_total + feat.inlined_calls > 8 * feat.points_computed_minimum) {
                return true;
            }
        }
    }

    // Avoid code size explosion from recursive inlining.
    if (root_ptr->max_inlined_calls() >= 256) {
        return true;
    }

    // Apply the hard limit on memory use
    if (memory_limit >= 0) {
        int64_t mem_used = (int64_t)features.begin().value().working_set_at_root;
        for (auto it = features.begin(); it != features.end(); it++) {
            if (it.key()->node->is_output ||
                it.key()->node->is_input) {
                // Not allocated by this pipeline
                mem_used -= it.value().bytes_at_production;
            }
        }
        if (mem_used > memory_limit) {
            return true;
        }
    }
    return false;
}

// This is directly taken from State::compute_featurization.
void compute_featurization(const FunctionDAG *dag_ptr, const MachineParams *params_ptr, const LoopNest *root_ptr, StageMap<ScheduleFeatures> *features) {
    StageMap<LoopNest::Sites> sites;
    sites.make_large(dag_ptr->nodes[0].stages[0].max_id);
    features->make_large(dag_ptr->nodes[0].stages[0].max_id);
    internal_assert(root_ptr) << "compute_featurization received a nullptr root\n";
    root_ptr->get_sites(sites);

    // For the input nodes and unscheduled outputs, the compute
    // and store sites are root, and the produce and innermost
    // sites are unset (nullptr)
    for (const auto &n : dag_ptr->nodes) {
        if (n.is_input || n.is_output) {
            for (const auto &stage : n.stages) {
                auto &s = sites.get_or_create(&stage);
                if (s.compute == nullptr) {
                    s.compute = root_ptr;
                    s.store = root_ptr;
                }
            }
        }
    }

    // For the unscheduled nodes, give them sites as deep as they
    // could possibly be. We'll ignore the possibility of inlining
    // them for now.
    map<const LoopNest *, pair<const LoopNest *, int>> parent;
    compute_loop_nest_parents(parent, root_ptr, 0);
    for (const auto &n : dag_ptr->nodes) {
        if (sites.contains(&(n.stages[0]))) {
            continue;
        }
        const LoopNest *loop = nullptr;
        for (const auto *e : n.outgoing_edges) {
            const auto &consumer_site = sites.get(e->consumer);
            const LoopNest *l = consumer_site.innermost;
            if (!l) {
                l = consumer_site.compute;
            }
            if (!l) {
                internal_error << e->producer->func.name() << " -> " << e->consumer->name << "\n";
            }
            if (loop) {
                loop = deepest_common_ancestor(parent, l, loop);
            } else {
                loop = l;
            }
        }
        internal_assert(loop)
            << "Could not compute plausible site for unscheduled Func: "
            << n.func.name() << "\n";
        for (const auto &stage : n.stages) {
            auto &site = sites.get_or_create(&stage);
            site.compute = loop;
            site.store = loop;
        }
    }

    root_ptr->compute_features(*dag_ptr, *params_ptr, sites,
                               /* instances */ 1, /* parallelism */ 1,
                               /* parent */ nullptr, /* grandparent */ nullptr,
                               *root_ptr, /* working_set ptr */ nullptr, features);

    for (const auto &n : dag_ptr->nodes) {
        if (sites.get(&(n.stages[0])).produce == nullptr) {
            internal_assert(!features->contains(&(n.stages[0])))
                << "Somehow an input or unscheduled node ended up in the featurization: "
                << n.func.name() << "\n";
        }
    }
}

// Also taken directly from State::compute_loop_nest_parents
void compute_loop_nest_parents(map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                const LoopNest *here, int depth) {
    for (const auto &child : here->children) {
        parent.emplace(child.get(), pair<const LoopNest *, int>{here, depth});
        compute_loop_nest_parents(parent, child.get(), depth + 1);
    }
}

// Also taken directly from State::deepest_common_ancestor.
const LoopNest *deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                        const LoopNest *a, const LoopNest *b) {
    if (a->is_root()) {
        return a;
    }
    if (b->is_root()) {
        return b;
    }
    if (a == b) {
        return a;
    }

    // Walk the deeper one up until they're at the same depth
    auto it_a = parent.find(a);
    auto it_b = parent.find(b);
    internal_assert(it_a != parent.end() && it_b != parent.end());
    while (it_a->second.second > it_b->second.second) {
        a = it_a->second.first;
        it_a = parent.find(a);
    }
    while (it_b->second.second > it_a->second.second) {
        b = it_b->second.first;
        it_b = parent.find(b);
    }

    while (true) {
        // Walk each up one
        a = it_a->second.first;
        b = it_b->second.first;
        if (a == b) {
            return a;
        }
        it_a = parent.find(a);
        it_b = parent.find(b);
        internal_assert(it_a != parent.end() && it_b != parent.end());
    }

    // unreachable
    return nullptr;
}


}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide