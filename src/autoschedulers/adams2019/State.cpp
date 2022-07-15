#include "State.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using std::map;
using std::pair;

uint64_t State::structural_hash(int depth) const {
    uint64_t h = num_decisions_made;
    internal_assert(root.defined());
    root->structural_hash(h, depth);
    return h;
}

void State::compute_featurization(const FunctionDAG &dag, const MachineParams &params,
                                  StageMap<ScheduleFeatures> *features, const CachingOptions &cache_options) {
    StageMap<LoopNest::Sites> sites;
    sites.make_large(dag.nodes[0].stages[0].max_id);
    features->make_large(dag.nodes[0].stages[0].max_id);
    internal_assert(root.defined());
    root->get_sites(sites);

    // For the input nodes and unscheduled outputs, the compute
    // and store sites are root, and the produce and innermost
    // sites are unset (nullptr)
    for (const auto &n : dag.nodes) {
        if (n.is_input || n.is_output) {
            for (const auto &stage : n.stages) {
                auto &s = sites.get_or_create(&stage);
                if (s.compute == nullptr) {
                    s.compute = root.get();
                    s.store = root.get();
                }
            }
        }
    }

    // For the unscheduled nodes, give them sites as deep as they
    // could possibly be. We'll ignore the possibility of inlining
    // them for now.
    map<const LoopNest *, pair<const LoopNest *, int>> parent;
    compute_loop_nest_parents(parent, root.get(), 0);
    for (const auto &n : dag.nodes) {
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
                std::ostringstream err;
                dump(err);
                err << e->producer->func.name() << " -> " << e->consumer->name << "\n";
                internal_error << err.str();
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

    if (cache_options.cache_features) {
        // Store unique hashes for each Site, to be used as keys into cache
        for (const auto &c : root->children) {
            sites.get(c->stage).hash_of_producers_stored_at_root = c->compute_hash_of_producers_stored_at_root(sites);
        }
    }

    root->compute_features(dag, params, sites, 1, 1, nullptr, nullptr, *root, nullptr, features, cache_options.cache_features);

    for (const auto &n : dag.nodes) {
        if (sites.get(&(n.stages[0])).produce == nullptr) {
            internal_assert(!features->contains(&(n.stages[0])))
                << "Somehow an input or unscheduled node ended up in the featurization: "
                << n.func.name() << "\n";
        }
    }
}

void State::save_featurization(const FunctionDAG &dag, const MachineParams &params,
                               const CachingOptions &cache_options, std::ostream &out) {
    StageMap<ScheduleFeatures> features;
    compute_featurization(dag, params, &features, cache_options);

    for (const auto &n : dag.nodes) {
        if (n.is_input) {
            continue;
        }
        for (size_t stage_idx = n.stages.size(); stage_idx > 0; stage_idx--) {
            const auto &s = n.stages[stage_idx - 1];
            const size_t num_schedule_features = ScheduleFeatures::num_features();
            const size_t num_pipeline_features = PipelineFeatures::num_features();
            const auto &sched_feat = features.get(&s);

            float buf[num_schedule_features + num_pipeline_features];
            // Save them as floats
            for (size_t i = 0; i < num_schedule_features; i++) {
                buf[i] = sched_feat[i];
            }

            for (size_t i = 0; i < num_pipeline_features; i++) {
                buf[i + num_schedule_features] = s.features[i];
            }

            out.write((const char *)buf, sizeof(buf));
        }
    }
}

bool State::calculate_cost(const FunctionDAG &dag, const MachineParams &params,
                           CostModel *cost_model, const CachingOptions &cache_options,
                           int64_t memory_limit, int verbosity) {
    StageMap<ScheduleFeatures> features;
    compute_featurization(dag, params, &features, cache_options);

    cost = 0.0f;

    if (verbosity <= aslog::aslog_level()) {
        for (auto it = features.begin(); it != features.end(); it++) {
            const auto &stage = *(it.key());
            const auto &feat = it.value();
            aslog(verbosity) << "Schedule features for " << stage.stage.name() << "\n";
            feat.dump(aslog(verbosity).get_ostream());
        }
    }

    internal_assert(cost_model) << "calculate_cost received nullptr for cost_model\n";

    // Perform some addition pruning before burdening the cost model with silly states
    for (auto it = features.begin(); it != features.end(); it++) {
        if (!it.key()->node->is_wrapper) {  // It's OK to repeatedly stage data
            auto &feat = it.value();
            if (feat.points_computed_total + feat.inlined_calls > 8 * feat.points_computed_minimum) {
                cost = 1e50;
                return false;
            }
        }
    }

    // Avoid code size explosion from recursive inlining.
    if (root->max_inlined_calls() >= 256) {
        cost = 1e50;
        return false;
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
            cost = 1e50;
            return false;
        }
    }

    // Tell the cost model about this state. It won't actually
    // evaluate it until we call evaluate_costs (or if it runs out
    // of internal buffer space), so that the evaluations can be
    // batched.
    cost_model->enqueue(dag, features, &cost);

    cost_calculations++;
    return true;
}

// Make a child copy of this state. The loop nest is const (we
// make mutated copies of it, rather than mutating it), so we can
// continue to point to the same one and so this is a cheap
// operation.
IntrusivePtr<State> State::make_child() const {
    State *s = new State;
    s->parent = this;
    s->root = root;
    s->cost = cost;
    s->num_decisions_made = num_decisions_made;
    return s;
}

// Generate the successor states to this state
void State::generate_children(const FunctionDAG &dag,
                              const MachineParams &params,
                              CostModel *cost_model,
                              int64_t memory_limit,
                              std::function<void(IntrusivePtr<State> &&)> &accept_child,
                              Cache *cache) const {

    internal_assert(root.defined() && root->is_root()) << "generate_children needs defined root\n";

    if (num_decisions_made == 2 * (int)dag.nodes.size()) {
        return;
    }

    int next_node = num_decisions_made / 2;
    int phase = num_decisions_made % 2;

    if (!may_subtile()) {
        // When emulating the older search space, we do all
        // parallelizing last, so that it is independent of the
        // tiling decisions.
        next_node = num_decisions_made % dag.nodes.size();
        phase = num_decisions_made / dag.nodes.size();
    }

    // Enumerate all legal ways to schedule the next Func
    const FunctionDAG::Node *node = &dag.nodes[next_node];
    for (const auto *e : node->outgoing_edges) {
        internal_assert(root->computes(e->consumer->node))
            << "Partially scheduled code doesn't compute " << e->consumer->name
            << ", which is one of the consumers of " << node->func.name();
    }

    if (node->is_input) {
        // We don't need to schedule nodes that represent inputs,
        // and there are no other decisions to be made about them
        // at this time.
        // aslog(1) << "Skipping over scheduling input node: " << node->func.name() << "\n";
        auto child = make_child();
        child->num_decisions_made++;
        accept_child(std::move(child));
        return;
    }

    if (!node->outgoing_edges.empty() && !root->calls(node)) {
        std::ostringstream err;
        err << "In state:\n";
        dump(err);
        err << node->func.name() << " is consumed by:\n";
        for (const auto *e : node->outgoing_edges) {
            err << e->consumer->name << "\n";
            err << "Which in turn consumes:\n";
            for (const auto *e2 : e->consumer->incoming_edges) {
                err << "  " << e2->producer->func.name() << "\n";
            }
        }
        err << "Pipeline so far doesn't use next Func: " << node->func.name() << "\n";
        internal_error << err.str();
    }

    int num_children = 0;

    if (phase == 0) {
        // Injecting realizations
        {
            // 1) Inline it
            if (node->stages.size() == 1 && !node->is_output) {
                auto child = make_child();
                LoopNest *new_root = new LoopNest;
                new_root->copy_from(*root);
                new_root->inline_func(node);
                child->root = new_root;
                child->num_decisions_made++;
                if (child->calculate_cost(dag, params, cost_model, cache->options, memory_limit)) {
                    num_children++;
                    accept_child(std::move(child));
                }
            }
        }

        // Some search-space pruning. If a node is pointwise, and
        // so are all its inputs and so is its sole output, and
        // inlining it is legal, just inline it. This saves time
        // on long chains of pointwise things.
        bool must_inline = (node->is_pointwise &&
                            (num_children > 0) &&
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
                return;
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

        // 2) Realize it somewhere
        for (int vector_dim : vector_dims) {
            auto tile_options = root->compute_in_tiles(node, nullptr, params, vector_dim, false);
            for (IntrusivePtr<const LoopNest> &n : tile_options) {
                auto child = make_child();
                child->root = std::move(n);
                child->num_decisions_made++;
                if (child->calculate_cost(dag, params, cost_model, cache->options, memory_limit)) {
                    num_children++;
                    accept_child(std::move(child));
                }
            }
        }
    } else {
        // We are parallelizing the loops of the func we just injected a realization for.

        bool should_parallelize = false;
        const vector<int64_t> *pure_size = nullptr;
        if (params.parallelism > 1) {
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
            num_children++;
            auto child = make_child();
            child->num_decisions_made++;
            accept_child(std::move(child));
        } else {
            internal_assert(pure_size);

            if (cache->add_memoized_blocks(this, accept_child, node, num_children, dag, params, cost_model, memory_limit)) {
                return;  // successfully added cached states.
            }

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
                        const double tasks_per_core = ((double)total) / params.parallelism;
                        o.idle_core_wastage = std::max(o.idle_core_wastage,
                                                       std::ceil(tasks_per_core) /
                                                           tasks_per_core);
                    }
                }

                // Filter out the less useful options
                bool ok =
                    ((o.entire || min_total >= params.parallelism) &&
                     (max_total <= params.parallelism * 16));

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
                num_children++;
                auto child = make_child();
                child->num_decisions_made++;
                accept_child(std::move(child));
                return;
            }

            for (const auto &o : options) {
                if (num_children >= 1 && (o.idle_core_wastage > 1.2 || !may_subtile())) {
                    // We have considered several options, and the
                    // remaining ones leave lots of cores idle.
                    break;
                }

                auto child = make_child();
                LoopNest *new_root = new LoopNest;
                new_root->copy_from(*root);
                for (auto &c : new_root->children) {
                    if (c->node == node) {
                        if (may_subtile()) {
                            c = c->parallelize_in_tiles(params, o.tiling, new_root);
                        } else {
                            // We're emulating the old
                            // autoscheduler for an ablation, so
                            // emulate its parallelism strategy:
                            // just keep parallelizing outer loops
                            // until enough are parallel.
                            vector<int64_t> tiling = c->size;
                            int64_t total = 1;
                            for (size_t i = c->size.size(); i > 0; i--) {
                                if (!c->stage->loop[i - 1].pure || total >= params.parallelism) {
                                    tiling[i - 1] = 1;
                                }
                                while (tiling[i - 1] > 1 &&
                                       total * tiling[i - 1] > params.parallelism * 8) {
                                    tiling[i - 1] /= 2;
                                }
                                total *= tiling[i - 1];
                            }
                            c = c->parallelize_in_tiles(params, tiling, new_root);
                        }
                    }
                }
                child->root = new_root;
                child->num_decisions_made++;
                if (child->calculate_cost(dag, params, cost_model, cache->options, memory_limit)) {
                    num_children++;
                    accept_child(std::move(child));
                    // Will early return if block caching is not enabled.
                    cache->memoize_blocks(node, new_root);
                }
            }
        }
    }

    if (num_children == 0) {
        aslog(1) << "Warning: Found no legal way to schedule "
                 << node->func.name() << " in the following State:\n";
        dump(aslog(1).get_ostream());
        // All our children died. Maybe other states have had
        // children. Carry on.
    }
}

void State::dump(std::ostream &os) const {
    os << "State with cost " << cost << ":\n";
    root->dump(os, "", nullptr);
    os << schedule_source;
}

// Apply the schedule represented by this state to a Halide
// Pipeline. Also generate source code for the schedule for the
// user to copy-paste to freeze this schedule as permanent artifact.
void State::apply_schedule(const FunctionDAG &dag, const MachineParams &params) {
    StageMap<std::unique_ptr<LoopNest::StageScheduleState>> state_map;
    root->apply(LoopLevel::root(), state_map, params.parallelism, 0, nullptr, nullptr);

    std::ostringstream src;

    // Print handles for all the Funcs
    int i = (int)(dag.nodes.size() - 1);
    for (const auto &n : dag.nodes) {
        if (!n.is_input) {
            src << "Func " << n.func.name() << " = pipeline.get_func(" << i << ");\n";
        }
        i--;
    }

    // Gather all Vars and RVars so that we can declare them in the emitted source
    map<string, string> vars, rvars;
    for (auto &p : state_map) {
        for (auto &v : p.second->vars) {
            if (v.exists) {
                if (v.var.is_rvar) {
                    rvars.emplace(v.var.name(), v.accessor);
                } else {
                    vars.emplace(v.var.name(), v.accessor);
                }
            }
        }
    }
    if (!vars.empty()) {
        for (const auto &p : vars) {
            if (p.second.empty()) {
                src << "Var " << p.first << "(\"" << p.first << "\");\n";
            } else {
                src << "Var " << p.first << "(" << p.second << ");\n";
            }
        }
    }
    if (!rvars.empty()) {
        for (const auto &p : rvars) {
            if (p.second.empty()) {
                src << "RVar " << p.first << "(\"" << p.first << "\");\n";
            } else {
                src << "RVar " << p.first << "(" << p.second << ");\n";
            }
        }
    }

    for (auto &p : state_map) {
        if (p.first->node->is_input) {
            continue;
        }

        Stage stage(p.first->stage);

        // Do all the reorders and pick which vars to
        // parallelize.
        vector<VarOrRVar> vars;
        vector<VarOrRVar> parallel_vars;
        bool any_parallel_vars = false, any_parallel_rvars = false;
        for (auto it = p.second->vars.rbegin(); it != p.second->vars.rend(); it++) {
            if (!it->exists || it->extent == 1) {
                continue;
            }
            if (!it->parallel) {
                break;
            }
            any_parallel_rvars |= it->var.is_rvar;
            any_parallel_vars |= !it->var.is_rvar;
            parallel_vars.push_back(it->var);
        }

        if (p.second->vars.size() > 1) {
            p.second->schedule_source << "\n    .reorder(";
            bool first = true;
            for (auto &v : p.second->vars) {
                if (v.exists) {
                    vars.push_back(v.var);
                    if (!first) {
                        p.second->schedule_source << ", ";
                    } else {
                        p.second->schedule_source << "{";
                    }
                    first = false;
                    p.second->schedule_source << v.var.name();
                }
            }
            p.second->schedule_source << "})";
            stage.reorder(vars);
        }

        // Halide doesn't let you fuse an RVar with a Var, even if
        // they are both pure.
        bool can_fuse = !(any_parallel_vars && any_parallel_rvars);
        if (can_fuse) {
            for (size_t i = 1; i < parallel_vars.size(); i++) {
                // Outermost, and next outermost. Preserve the inner
                // name to not invalidate any compute_ats.
                p.second->schedule_source << "\n    .fuse(" << parallel_vars[i].name()
                                          << ", " << parallel_vars[i - 1].name()
                                          << ", " << parallel_vars[i].name() << ")";
                stage.fuse(parallel_vars[i], parallel_vars[i - 1], parallel_vars[i]);
            }
            if (!parallel_vars.empty()) {
                p.second->schedule_source << "\n    .parallel(" << parallel_vars.back().name() << ")";
                stage.parallel(parallel_vars.back());
            }
        } else {
            for (const auto &v : parallel_vars) {
                p.second->schedule_source << "\n    .parallel(" << v.name() << ")";
                stage.parallel(v);
            }
        }

        // Reorder the vector dimension innermost
        if (p.first->index == 0 && p.second->vector_dim > 0) {
            vector<Var> storage_vars = Func(p.first->node->func).args();
            for (int i = p.second->vector_dim; i > 0; i--) {
                std::swap(storage_vars[i], storage_vars[i - 1]);
            }
            p.second->schedule_source << "\n    .reorder_storage(";
            bool first = true;
            for (const auto &v : storage_vars) {
                if (!first) {
                    p.second->schedule_source << ", ";
                }
                first = false;
                p.second->schedule_source << v.name();
            }
            p.second->schedule_source << ")";
            Func(p.first->node->func).reorder_storage(storage_vars);
        }

        // Dump the schedule source string
        src << p.first->name
            << p.second->schedule_source.str()
            << ";\n";
    }
    // Sanitize the names of things to make them legal source code.
    schedule_source = src.str();
    bool in_quotes = false;
    for (auto &c : schedule_source) {
        in_quotes ^= (c == '"');
        if (!in_quotes && c == '$') {
            c = '_';
        }
    }
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
