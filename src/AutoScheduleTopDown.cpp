#include "AutoScheduleTopDown.h"
#include "FindCalls.h"
#include "IRVisitor.h"
#include "OutputImageParam.h"
#include "RealizationOrder.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"

#include <set>
#include <queue>
#include <algorithm>

// TODO: overview of algorithm

namespace Halide {
namespace Internal {

namespace {

using std::string;
using std::vector;
using std::map;
using std::set;
using std::pair;


// A representation of the function DAG. The nodes and edges are both
// in reverse realization order, so if you want to walk backwards up
// the DAG, just iterate the nodes or edges in-order.
struct FunctionDAG {

    struct Node {
        Function func;

        // The amount of compute done in terms of a symbolic region of
        // this Func. Measured in vectors-of-compute.
        Expr compute;

        // The memory cost of loading all of this Func, in terms of the same symbolic region
        Expr memory;

        // The min/max variables used to denote a symbolic region of
        // this Func. Used in the cost above, and in the Edges below.
        vector<Interval> region;

        // The arithmetic complexity of a single call to this
        // Func. Measured in abstract number-of-ops.
        Expr arith;
    };

    struct Edge {
        Function producer, consumer;

        // The region required of producer in terms of a symbolic
        // region of the consumer
        vector<Interval> bounds;

        // The number of calls the consumer makes to the producer, in
        // terms of the same symbolic region. Use this for inlining.
        Expr calls;
    };

    vector<Node> nodes;
    vector<Edge> edges;

    // We're going to be querying this DAG a lot while searching for
    // an optimal schedule, so we'll also create a variety of
    // auxiliary data structures.
    map<Function, vector<const Edge *>, Function::Compare> outgoing_edges, incoming_edges;
    map<Function, const Node *, Function::Compare> node_map;

    // Create the function DAG, and do all the dependency and cost
    // analysis. This is done once up-front before the tree search.
    FunctionDAG(const vector<Function> &outputs, const MachineParams &params) {
        map<string, Function> env;
        for (Function o : outputs) {
            populate_environment(o, env);
        }

        // Compute a realization order
        vector<string> order = realization_order(outputs, env);

        for (size_t i = order.size(); i > 0; i--) {
            Function consumer = env[order[i-1]];

            internal_assert(consumer.updates().empty()) << "Update definitions not yet implemented\n";

            // Create a symbolic region for this Func.
            Node node;
            Expr vectors_computed = make_one(Int(64));
            Expr memory_allocated = make_zero(Int(64));
            for (Expr e : consumer.values()) {
                memory_allocated += e.type().bytes();
            }
            Expr inner_loop_instances = make_one(Int(64));
            Scope<Interval> scope;
            node.func = consumer;
            for (int i = 0; i < consumer.dimensions(); i++) {
                Expr min_var = Variable::make(Int(32), consumer.name() + "." + std::to_string(i) + ".min");
                Expr max_var = Variable::make(Int(32), consumer.name() + "." + std::to_string(i) + ".max");
                Expr extent = max_var - min_var + 1;
                if (i > 0) {
                    inner_loop_instances *= extent;
                    vectors_computed *= extent;
                    memory_allocated *= extent;
                } else {
                    // We measure number of points computed in whole
                    // vectors.
                    vectors_computed *= (extent + 7) / 8; // TODO: assumes uint16
                    memory_allocated *= extent;
                }
                Interval interval(min_var, max_var);
                scope.push(consumer.args()[i], interval);
                node.region.push_back(interval);
            }

            // Get all the expressions used in the consumer. For now
            // we just consider the RHS. Bundle them all into a single
            // Call node for convenience.
            vector<Expr> exprs_vector = consumer.values();
            Expr exprs = Call::make(Int(32), "dummy", exprs_vector, Call::Extern);

            // Do the cost analysis. Simplistic for now - just counts
            // leaf nodes in the expression trees.
            class LeafCounter : public IRVisitor {
                using IRVisitor::visit;
                void visit(const IntImm *) override {
                    leaves++;
                }

                void visit(const UIntImm *op) override {
                    leaves++;
                }

                void visit(const FloatImm *op) override {
                    leaves++;
                }

                void visit(const Variable *op) override {
                    leaves++;
                }
                void visit(const Call *op) override {
                    IRVisitor::visit(op);
                    calls[op->name]++;
                }
            public:
                int leaves = 0;
                map<string, int> calls;
            };
            LeafCounter counter;
            exprs.accept(&counter);

            call_counts.emplace(consumer.name(), std::move(counter.call_counts));

            // This is where the cost model is encoded!
            node.arith = counter.leaves;
            node.compute = simplify(cast<double>(vectors_computed) * node.arith + inner_loop_instances);
            Expr balance = cast<double>(params.balance), llc = cast<double>(params.last_level_cache_size);
            Expr cost_per_load = (balance / llc) * min(memory_allocated, llc);
            node.memory = simplify(memory_allocated * cost_per_load);

            // Now create the edges that lead to this func
            for (auto p : boxes_required(exprs, scope)) {
                auto it = env.find(p.first);
                if (it != env.end()) {
                    // Discard loads from input images
                    Edge edge;
                    edge.consumer = consumer;
                    edge.producer = env[p.first];
                    edge.bounds = p.second.bounds;
                    edge.calls = counter.calls[edge.producer.name()];
                    edges.emplace_back(std::move(edge));
                }
            }

            nodes.emplace_back(std::move(node));
        }

        for (size_t i = 0; i < nodes.size(); i++) {
            incoming_edges[nodes[i].func];
            outgoing_edges[nodes[i].func];
            node_map[nodes[i].func] = &nodes[i];
        }
        for (size_t i = 0; i < edges.size(); i++) {
            outgoing_edges[edges[i].producer].push_back(&(edges[i]));
            incoming_edges[edges[i].consumer].push_back(&(edges[i]));
        }
    }

    void dump() {
        for (const Node &n : nodes) {
            debug(0) << "Node: " << n.func.name() << "\n"
                     << "  Symbolic region: \n";
            for (const Interval &i : n.region) {
                debug(0) << "    " << i.min << ", " << i.max << "\n";
            }
            debug(0) << "  Arithmetic cost: " << n.arith << "\n";
        }
        for (const Edge &e : edges) {
            debug(0) << "Edge: " << e.producer.name() << " -> " << e.consumer.name() << "\n"
                     << "  Footprint: \n";
            int j = 0;
            for (const Interval &i : e.bounds) {
                debug(0) << "    Min " << j << ": " << i.min << "\n";
                debug(0) << "    Max " << j << ": " << i.max << "\n";
                j++;
            }

        }
    }

private:
    // The auxiliary data structures use internal pointers, so we'll hide the copy constructor
    FunctionDAG(const FunctionDAG &other) = delete;
    void operator=(const FunctionDAG &other) = delete;

};

// We're going to do a tree search over possible schedules to find an
// optimal one. A tree search requires a state, and a function that
// gives you children of the state (with costs). This struct
// represents the state, which is a partial schedule:

struct Group {
    struct FunctionState {
        Function func;

        // The current bounds
        vector<std::pair<int, int>> bounds;

        // The current arithmetic cost, given these bounds
        double arith;

        // The current memory cost for loading from this func, given these bounds.
        double memory;

        bool is_output;

        // Outputs. Created as a side-effect of the search, but not
        // used in the search.

        // Which Func's loop nest are we stored within?
        Function parent;

        // and at what depth (how many loops of the parent are
        // outside the production of this Func)
        int depth;

        // The split factors for each dimension
        vector<vector<int>> splits;

        // The ordering of the outer dimensions peeled off this func's
        // loop nest, from outermost in.
        vector<int> loop_order;
    };

    // The number of times this group is evaluated at these
    // sizes. In the future this may need to track the actual outer
    // loops.
    int64_t instances;

    vector<FunctionState> funcs;
    float parallelism;

    bool fully_scheduled() const {
        return funcs.size() == 1;
    }

    void compute_bounds_and_costs(const FunctionDAG &dag) {
        map<Function, size_t, Function::Compare> group_members;
        for (size_t i = 0; i < funcs.size(); i++) {
            group_members[funcs[i].func] = i;
            if (!funcs[i].is_output) {
                funcs[i].bounds.clear();
            }
        }

        map<string, Expr> concrete_bounds;

        for (const auto &consumer : funcs) {
            internal_assert(dag.incoming_edges.count(consumer.func));

            const auto *node = dag.node_map.at(consumer.func);
            internal_assert(node->region.size() == (size_t)consumer.func.dimensions());
            internal_assert(consumer.bounds.size() == (size_t)consumer.func.dimensions());
            for (int i = 0; i < consumer.func.dimensions(); i++) {
                concrete_bounds[node->region[i].min.as<Variable>()->name] = consumer.bounds[i].first;
                concrete_bounds[node->region[i].max.as<Variable>()->name] = consumer.bounds[i].second;
            }

            for (const auto *edge : dag.incoming_edges.at(consumer.func)) {
                if (!group_members.count(edge->producer)) continue; // The producer is in another group.
                // Find the producer
                FunctionState &producer = funcs[group_members.at(edge->producer)];

                internal_assert(edge->consumer.same_as(consumer.func));
                internal_assert(edge->producer.same_as(producer.func));
                internal_assert(!producer.is_output);

                bool first_consumer = producer.bounds.empty();
                if (first_consumer) {
                    producer.bounds.resize(producer.func.dimensions());
                }

                // Expand the bounds of the producer using the bounds
                // relationship encoded in the edge.
                for (int i = 0; i < producer.func.dimensions(); i++) {
                    Interval interval = edge->bounds[i];
                    interval.min = simplify(substitute(concrete_bounds, interval.min));
                    interval.max = simplify(substitute(concrete_bounds, interval.max));
                    const int64_t *i_max = as_const_int(interval.max);
                    const int64_t *i_min = as_const_int(interval.min);
                    internal_assert(i_min) << interval.min;
                    internal_assert(i_max) << interval.max;
                    if (first_consumer) {
                        producer.bounds[i] = {(int)(*i_min), (int)(*i_max)};
                    } else {
                        producer.bounds[i].first = std::min(producer.bounds[i].first, (int)(*i_min));
                        producer.bounds[i].second = std::max(producer.bounds[i].second, (int)(*i_max));
                    }
                }
            }
        }

        // Recompute arithmetic and memory costs using these new bounds.
        for (auto &func : funcs) {
            internal_assert(dag.node_map.count(func.func));
            Expr arith = simplify(substitute(concrete_bounds, dag.node_map.at(func.func)->arith));
            const double *i_arith = as_const_float(arith);
            internal_assert(i_arith) << arith;
            func.arith = *i_arith;
            Expr memory = simplify(substitute(concrete_bounds, dag.node_map.at(func.func)->memory));
            const double *i_memory = as_const_float(memory);
            internal_assert(i_memory) << memory;
            func.memory = *i_memory;
        }
    }
};

struct State {
    vector<Group> groups;
    double redundant_compute_cost, memory_cost;

    bool cheaper_than(const State &other) {
        return (redundant_compute_cost + memory_cost) < (other.redundant_compute_cost + other.memory_cost);
    }

    void dump() const {
        double total_arith = 0;
        for (const auto &g : groups) {
            for (const auto &f : g.funcs) {
                total_arith += f.arith * g.instances;
            }
        }
        debug(0) << "State: \n";
        int counter = 0;
        debug(0) << "  memory cost: " << memory_cost << "\n";
        debug(0) << "  total compute: " << total_arith << "\n";
        debug(0) << "  essential compute: " << (total_arith - redundant_compute_cost) << "\n";
        debug(0) << "  redundant compute: " << redundant_compute_cost << "\n";
        debug(0) << "  total cost: " << (redundant_compute_cost + memory_cost) << "\n";
        debug(0) << "  compute multiplier: " << (1 + redundant_compute_cost / (total_arith - redundant_compute_cost)) << "\n";
        for (const auto &g : groups) {
            debug(0) << "  Group " << counter++ << ":\n"
                     << "    parallelism: " << g.parallelism << "\n"
                     << "    instances: " << g.instances << "\n";
            for (const auto &f : g.funcs) {
                string parent = "none";
                if (f.depth) parent = f.parent.name();
                debug(0) << "    Function: " << f.func.name() << "\n"
                         << "      arithmetic cost: " << f.arith << "\n"
                         << "      memory cost: " << f.memory << "\n"
                         << "      is_output: " << f.is_output << "\n"
                         << "      parent " << parent << "\n"
                         << "      compute_depth: " << f.depth << "\n"
                         << "      bounds: ";
                for (const auto &i : f.bounds) {
                    debug(0) << "[" << i.first << ", " << i.second << "] ";
                }
                debug(0) << "\n"
                         << "      loop order: ";
                for (int l : f.loop_order) {
                    debug(0) << l << " ";
                }
                debug(0) << "\n";
                for (int dim = 0; dim < f.func.dimensions(); dim++) {
                    debug(0) << "      split factors " << dim << ": ";
                    for (int s : f.splits[dim]) {
                        debug(0) << s << " ";
                    }
                    debug(0) << "\n";
                }
            }
        }
    }

    // Set the bounds of the non-outputs in each group using the edges in the dag
    void compute_bounds_and_costs(const FunctionDAG &dag) {
        for (auto &g : groups) {
            g.compute_bounds_and_costs(dag);
        }
    }
};


// Get the children of a State to do tree search.
vector<std::shared_ptr<State>> make_children(const State &state, const FunctionDAG &dag) {

    // debug(0) << "Generating children of state at " << (void *)(&state) << "\n";

    vector<std::shared_ptr<State>> result;

    // Pick one of the unfinished groups to mutate. Because this
    // problem has optimal substructure, we don't have to generate
    // children for all of them, just one. The schedule within a group
    // has no impact on the cost of the other groups. We pick the
    // largest group, because working on it will probably incur the
    // largest marginal cost.
    int group_idx = -1;
    for (size_t i = 0; i < state.groups.size(); i++) {
        const Group &g = state.groups[i];
        if (g.fully_scheduled()) continue;
        if (group_idx == -1 ||
            g.funcs.size() > state.groups[group_idx].funcs.size()) {
            group_idx = i;
        }
    }

    if (group_idx == -1) {
        // This state is fully scheduled
        debug(0) << "  All groups fully scheduled. No children\n";
        return result;
    }

    const Group &group = state.groups[group_idx];

    // Try partitioning the group
    if (group.funcs.size() > 1) {
        for (size_t i = 1; i < group.funcs.size(); i++) {
            // debug(0) << "  Generating partition...\n";
            // TODO: If it's possible for a partioning to be
            // non-contiguous in the realization order, then this is a
            // subset of interesting partitionings. A better choice
            // would be to pick one Func, and then put everything
            // upstream of it in group A.
            std::shared_ptr<State> child(new State(state));
            map<Function, size_t, Function::Compare> funcs_in_A, funcs_in_B;
            Group A = group, B = group;
            A.funcs.clear();
            B.funcs.clear();
            for (size_t j = 0; j < group.funcs.size(); j++) {
                if (j >= i) {
                    funcs_in_A[group.funcs[j].func] = A.funcs.size();
                    A.funcs.push_back(group.funcs[j]);
                    // debug(0) << "A: " << group.funcs[j].func.name() << "\n";
                } else {
                    funcs_in_B[group.funcs[j].func] = B.funcs.size();
                    B.funcs.push_back(group.funcs[j]);
                    // debug(0) << "B: " << group.funcs[j].func.name() << "\n";
                }
            }

            // Find the broken edges
            for (const auto &edge : dag.edges) {
                auto producer_in_A = funcs_in_A.find(edge.producer);
                auto consumer_in_B = funcs_in_B.find(edge.consumer);
                if (producer_in_A != funcs_in_A.end() &&
                    consumer_in_B != funcs_in_B.end()) {
                    // This is a broken edge

                    // Incur a memory cost
                    child->memory_cost += A.funcs[producer_in_A->second].memory * A.instances;

                    // Update the output flags
                    A.funcs[producer_in_A->second].is_output = true;
                }
            }

            // Keep the groups in topological order, to make debugging easier.
            child->groups[group_idx] = std::move(A);
            child->groups.emplace(child->groups.begin() + group_idx, std::move(B));
            result.emplace_back(std::move(child));
        }

        // Try fusing the group under an outer parallel loop
        do {
            // debug(0) << "  Considering fusing\n";
            // Find the group outputs
            set<Function, Function::Compare> outputs;
            set<Function, Function::Compare> group_members;
            bool can_parallelize = true;
            const Group::FunctionState *old_output_state;
            for (auto &f : group.funcs) {
                // TODO: Check we don't already have split store_at compute_at within this group (compute depth == store depth)
                group_members.insert(f.func);
                // can_parallelize &= (f.compute_at == f.store_at);
                if (f.is_output) {
                    // debug(0) << "Output: " << f.func.name() << "\n";
                    outputs.insert(f.func);
                    old_output_state = &f;
                } else {
                    // debug(0) << "Non-output: " << f.func.name() << "\n";
                }
            }

            if (!can_parallelize) break;
            if (outputs.size() > 1) break;

            internal_assert(!outputs.empty());

            Function output = *outputs.begin();

            // Over all possible dimensions
            for (int dim = 0; dim < output.dimensions(); dim++) {
                int extent = -1;
                for (auto &fs : group.funcs) {
                    if (fs.func.same_as(output)) {
                        extent = fs.bounds[dim].second - fs.bounds[dim].first + 1;
                        break;
                    }
                }

                if (extent == 1) continue;

                // For now don't try to split any already-split dimension. This disallows subtiling.
                internal_assert(old_output_state);
                internal_assert(dim < (int)old_output_state->splits.size()) << output.name() << " " << old_output_state->splits.size() << " " << dim << "\n";
                if (old_output_state->splits[dim].size() > 1) continue;

                // Generate candidate split factors.
                set<int> splits;// = {8, 16, 32, 64, 128, 256};
                for (int s = 4; s < extent; s *= 2) {
                    // Divide the extent into 's' pieces
                    int factor = (extent + s - 1)/s;
                    if (dim == 0) {
                        // We have cache coherency and vectorization
                        // effects to worry about.
                        if (factor < 8) {
                            factor = 8;
                        } else if (factor < 64) {
                            // Quantize the factor to a power of two
                            factor--;
                            factor |= factor >> 1;
                            factor |= factor >> 2;
                            factor |= factor >> 4;
                            factor++;
                        } else {
                            // Quantize the factor to a multiple of 64
                            factor = ((factor + 63) / 64) * 64;
                        }
                    }
                    splits.insert(factor);
                }
                if (dim != 0) splits.insert(1);

                for (int split : splits) {
                    if (split >= extent) continue;
                    // debug(0) << "    Considering fusing group along output dimension " << dim << " with factor " << split << "\n";

                    // Use the first loop iteration to compute future
                    // costs and bounds relationships.  TODO: This
                    // assumes shift-invariance of everything!!! Would
                    // be more correct to introduce a symbolic
                    // variable for the loop index, and take the max
                    // over all values of it producer footprint later.

                    Group new_group = group;
                    Group::FunctionState *output_state = nullptr;
                    double old_arith = 0;
                    for (auto &fs : new_group.funcs) {
                        if (fs.func.same_as(output)) {
                            output_state = &fs;
                        }
                        old_arith += fs.arith;
                    }
                    old_arith *= new_group.instances;

                    internal_assert(output_state);

                    int &min = output_state->bounds[dim].first;
                    int &max = output_state->bounds[dim].second;

                    int outer_loop_size = (extent + split - 1) / split;
                    new_group.instances *= outer_loop_size;

                    max = min + split - 1;

                    // Recompute bounds and arithmetic cost up the pipeline
                    new_group.compute_bounds_and_costs(dag);

                    // Should not have updated the bounds of the group output
                    internal_assert(max == min + split - 1);

                    double new_arith = 0;
                    for (auto &fs : new_group.funcs) {
                        new_arith += fs.arith;
                    }
                    new_arith *= new_group.instances;

                    // Reject out-of-hand anything
                    // particularly dumb (e.g. that causes 3x
                    // redundant work or more). Sometimes a painful
                    // partitioning of the pipeline must occur. Beam
                    // search with a large beam will eventually decide
                    // to try it. Beam search with a small beam can
                    // avoid it by flooding the beam with slightly
                    // less painful incremental fuses that lead to
                    // worse overall outcomes.
                    //if (new_arith > 1.1*old_arith) {
                    //    continue;
                    //}

                    // Record what we did
                    for (auto &fs : new_group.funcs) {
                        if (fs.func.same_as(output)) {
                            fs.splits[dim].push_back(split);
                            fs.loop_order.push_back(dim);
                        } else {
                            if (fs.parent.same_as(output)) {
                                fs.depth++;
                            } else {
                                fs.depth = 1;
                            }
                            fs.parent = output;
                        }
                    }

                    std::shared_ptr<State> child(new State(state));
                    internal_assert(new_arith >= old_arith) << "Fusing reduced total amount of work!\n";
                    child->redundant_compute_cost += new_arith - old_arith;
                    child->groups[group_idx] = new_group;

                    /*
                    if (group.funcs.size() == 2 &&
                        group.funcs[0].func.name() == "output" &&
                        group.funcs[1].func.name() == "stage_7" &&
                        dim == 1) {
                        debug(0) << "Considering fusing group along output dimension " << dim << " with factor " << split << "\n";
                        debug(0) << "Old group:\n";
                        state.dump();
                        debug(0) << "New group:\n";
                        child->dump();
                    }
                    */

                    result.emplace_back(std::move(child));
                }
            }
        } while (0);

        // Try fusing the group under an outer serial loop
        {
            // What costs are incurred by doing this? For stencils,
            // none, but it's only available once (at least folding is
            // only available on one dimension). For non-stencils,
            // there may be redundant recompute introduced. Need some
            // sort of can_slide query, or maybe just detect stencils
            // and only do it in groups where everything is a stencil.
            // Probably want to incur some overhead for doing this
            // too, as the folding needs dynamic tracking.
        }
    }

    // Try doing scheduling operations to a single-Func
    // group. Parallelizing? Vectorizing? Incur an unused parallelism
    // cost?


    return result;
}

// Returns optimal schedule
State optimal_schedule(const FunctionDAG &dag, vector<Function> outputs, const MachineParams &params, size_t beam_size) {
    State initial;
    {
        initial.redundant_compute_cost = 0;
        initial.memory_cost = 0;

        Group group;
        group.parallelism = params.parallelism;
        group.instances = 1;
        for (const FunctionDAG::Node &n : dag.nodes) {
            Group::FunctionState f;
            f.func = n.func;
            f.is_output = false;
            for (auto o : outputs) {
                f.is_output |= f.func.same_as(o);
            }

            if (f.is_output) {
                // Get the estimates from the schedule
                map<string, pair<int, int>> estimates;
                for (auto b : f.func.schedule().estimates()) {
                    int64_t i_min = *as_const_int(b.min);
                    int64_t i_extent = *as_const_int(b.extent);
                    estimates[b.var] = {i_min, i_min + i_extent - 1};
                }
                // Set the bounds using the estimates
                auto buf = f.func.output_buffers()[0];
                for (int i = 0; i < f.func.dimensions(); i++) {
                    auto it = estimates.find(f.func.args()[i]);
                    user_assert(it != estimates.end())
                        << "Need an estimate on dimension " << i << " of \"" << f.func.name() << "\"";
                    f.bounds.push_back(it->second);
                }
            }

            f.splits.resize(f.func.dimensions());
            f.depth = 0;

            group.funcs.emplace_back(std::move(f));
        }

        initial.groups.emplace_back(std::move(group));

        initial.compute_bounds_and_costs(dag);
    }

    for (auto &g : initial.groups) {
        for (auto &fs : g.funcs) {
            internal_assert(fs.splits.size() == (size_t)fs.func.dimensions());
        }
    }

    // Note that we have constructed the problem such that zero is a
    // reasonable lower bound for all future costs.  We only track
    // redundant recompute and memory costs, so zero is the case in
    // which you successfully fuse everything entirely without
    // introducing redundant recompute. Our A* admissable heuristic
    // would be zero, so best-first search is the same as A*.
    //
    // TODO: We might be able to get tighter bound than zero by
    // reasoning about stencils - there's no way to get that cost down
    // to zero. There's a quadratic cost in the final tile/subtile
    // size that has a computable minimum over all tile/subtile sizes.

    struct CompareStates {
        bool operator()(const std::shared_ptr<State> &a, const std::shared_ptr<State> &b) const {
            return b->cheaper_than(*a);
        }
    };

    std::priority_queue<std::shared_ptr<State>,
                        std::vector<std::shared_ptr<State>>,
                        CompareStates> queue;
    queue.emplace(new State(std::move(initial)));

    for (int i = 0;; i++) {
        bool report = (i % 1024 == 0);

        // Limit the size of the queue
        if (queue.size() > beam_size) {
            decltype(queue) trimmed;
            for (size_t i = 0; i < beam_size; i++) {
                trimmed.emplace(std::move(queue.top()));
                queue.pop();
            }
            queue.swap(trimmed);
        }

        auto state = queue.top();
        queue.pop();

        if (report) debug(0) << "Considering partial schedule with cost " << state->memory_cost << " " << state->redundant_compute_cost << "\n";
        //state.dump();

        auto children = make_children(*state, dag);
        for (auto &c : children) {
            queue.emplace(std::move(c));
        }

        if (children.empty()) {
            // This is best-first search, and we hit a leaf, so it must be the best leaf.
            debug(0) << "Found completed schedule with cost " << state->memory_cost << " " << state->redundant_compute_cost << "\n";
            return *state;
        }
    }

    internal_error << "Unreachable\n";
    return initial;
};

}

std::string generate_schedules_top_down(const std::vector<Function> &outputs,
                                        const Target &target,
                                        const MachineParams &params) {
    string beam_size_str = get_env_variable("HL_BEAM_SIZE");
    size_t beam_size = 1000;
    if (!beam_size_str.empty()) {
        beam_size = atoi(beam_size_str.c_str());
    }

    string time_limit_str = get_env_variable("HL_AUTO_SCHEDULE_TIME_LIMIT");
    double time_limit = 0;
    if (!time_limit_str.empty()) {
        time_limit = atof(time_limit_str.c_str());
    }

    FunctionDAG dag(outputs, params);

    dag.dump();

    State optimal;

    if (time_limit) {
        // Use a fixed running time
        auto start = std::chrono::steady_clock::now();
        for (size_t beam_size = 1; ; beam_size *= 2) {
            State s = optimal_schedule(dag, outputs, params, beam_size);
            if (beam_size == 1 || s.cheaper_than(optimal)) {
                optimal = s;
            }
            auto t = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(t - start).count();
            if (elapsed > time_limit / 2) {
                break;
            }
        }
    } else {
        // Use a fixed beam size
        optimal = optimal_schedule(dag, outputs, params, beam_size);
    }

    debug(0) << "Optimal schedule:\n";
    optimal.dump();

    // Apply the schedules

    for (auto &g : optimal.groups) {
        internal_assert(g.funcs.size() == 1);
        auto &fs = g.funcs[0];

        // First do all the splits

        // Track the vars originating from each dimension, from outermost in.
        std::vector<std::vector<VarOrRVar>> dim_vars;
        for (int dim = 0; dim < fs.func.dimensions(); dim++) {
            std::vector<VarOrRVar> vars;
            Var v(fs.func.args()[dim]);
            for (int factor : fs.splits[dim]) {
                Var outer(v.name() + std::to_string(vars.size()));
                Func(fs.func).split(v, outer, v, factor);
                vars.push_back(outer);
            }
            std::reverse(vars.begin(), vars.end());
            dim_vars.emplace_back(std::move(vars));
        }

        // Then do one big reorder.

        // The inner loops
        std::vector<VarOrRVar> loops;
        for (int dim = 0; dim < fs.func.dimensions(); dim++) {
            loops.push_back(Var(fs.func.args()[dim]));
        }

        // The outer loops
        std::vector<VarOrRVar> outer_loops;
        for (int l : fs.loop_order) {
            internal_assert(!dim_vars[l].empty());
            outer_loops.push_back(dim_vars[l].back());
            dim_vars[l].pop_back();
        }
        // outer_loops is in order from outermost inwards, so reverse it
        std::reverse(outer_loops.begin(), outer_loops.end());
        // and stick in on the end of the loops vector
        loops.insert(loops.end(), outer_loops.begin(), outer_loops.end());

        Func(fs.func).reorder(loops);
        Func(fs.func).vectorize(loops[0], 8);
        if (fs.depth == 0) {
            Func(fs.func).parallel(loops.back());
        }
    }

    // Then set the compute and store levels
    for (auto &g : optimal.groups) {
        internal_assert(g.funcs.size() == 1);
        auto &fs = g.funcs[0];

        if (fs.depth == 0) {
            fs.func.schedule().store_level() = LoopLevel::root();
            fs.func.schedule().compute_level() = LoopLevel::root();
        } else {
            const vector<Dim> &dims = fs.parent.definition().schedule().dims();
            Var v(dims[dims.size() - fs.depth - 1].var); // -1 due to Var::outermost()
            fs.func.schedule().store_level() = LoopLevel(fs.parent, v);
            fs.func.schedule().compute_level() = LoopLevel(fs.parent, v);
        }
    }

    return "";
}

void autoschedule_test() {
    MachineParams params(8, 16 * 1024 * 1024, 40);
    size_t beam_size = 100;
    Target target("host");

    Var x, y;

    {
        // In a point-wise pipeline, everything should be fully fused.
        Func f, g, h;
        f(x, y) = x + y;
        g(x, y) = f(x, y) * 2;
        h(x, y) = g(x, y) * 2;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        internal_assert(optimal.redundant_compute_cost == 0); // No redundant recompute

        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g.funcs.size() == 1); // After scheduling, we should only have singleton groupos
            const auto &fs = g.funcs[0];
            internal_assert(fs.is_output); // After grouping, everything ends up as a group output.
            internal_assert(fs.splits.size() == 2); // There is always one split list per dimension

            // Check things that should be unique to this pipeline
            internal_assert(g.instances == 1000 * 1000); // Full fusion means a separate instance per output pixel
            internal_assert(fs.memory < 1); // Memory costs should be super cheap
            internal_assert(fs.arith < 5); // Arithmetic costs per instances are also minor, because each instance is one pixel

            if (!fs.func.same_as(h.function())) {
                internal_assert(fs.depth == 2); // Computed inside two loops of the consumer
                internal_assert(fs.parent.same_as(h.function()));
                internal_assert(fs.splits[0].empty()); // Not split
                internal_assert(fs.splits[1].empty());
            } else {
                // For the output, 1x1 tiles
                internal_assert(fs.splits[0].size() == 1);
                internal_assert(fs.splits[1].size() == 1);
                internal_assert(fs.splits[0][0] == 1);
                internal_assert(fs.splits[1][0] == 1);
            }
        }
    }

    {
        // In a pipeline with huge stencils, nothing should be fused
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y) * (x + y);
        g(x, y) = f(x-1000, y-2000) + f(x+1000, y+2000);
        h(x, y) = g(x-2000, y-1000) + g(x+2000, y+1000);

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        internal_assert(optimal.redundant_compute_cost == 0); // No redundant recompute
        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g.funcs.size() == 1);
            const auto &fs = g.funcs[0];
            internal_assert(fs.splits.size() == 2);
            internal_assert(fs.is_output);

            internal_assert(g.instances == 1); // No fusion means a single instance of each func
            internal_assert(fs.memory > 1000); // Memory costs should be high
            internal_assert(fs.arith > 1000); // Arithmetic costs should be high too

            internal_assert(fs.depth == 0); // Computed at root
            internal_assert(fs.splits[0].empty()); // No splits
            internal_assert(fs.splits[1].empty());
        }
    }

    int tile_x, tile_y;
    {
        // In a pipeline with moderate isotropic stencils, there should be some square tiling
        Func f("f"), h("h");
        f(x, y) = (x + y) * (x + y) * (x + y);
        h(x, y) = f(x-3, y-3) + f(x+3, y+3);

        h.estimate(x, 0, 1024).estimate(y, 0, 1024);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        double total_arith = 0;
        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g.funcs.size() == 1);
            const auto &fs = g.funcs[0];
            internal_assert(fs.is_output);
            internal_assert(fs.splits.size() == 2);

            total_arith += g.instances * fs.arith;

            internal_assert(g.instances > 1 && g.instances < 100); // There should be some modest number of tiles

            if (!fs.func.same_as(h.function())) {
                internal_assert(fs.depth == 2); // Computed inside two loops of the consumer
                internal_assert(fs.parent.same_as(h.function()));
                internal_assert(fs.splits[0].empty());
                internal_assert(fs.splits[1].empty());
            } else {
                // For the output, moderately-sized squarish tiles
                internal_assert(fs.splits[0].size() == 1);
                internal_assert(fs.splits[1].size() == 1);
                tile_x = fs.splits[0][0];
                tile_y = fs.splits[1][0];
                internal_assert(tile_x > 30 && tile_x < 600);
                internal_assert(tile_y > 30 && tile_y < 600);
                internal_assert(tile_x * 2 >= tile_y && tile_y * 2 >= tile_x);
            }

            internal_assert(fs.memory > 100 && fs.memory < 100000); // Memory costs should be moderate
            internal_assert(fs.arith > 1000); // Arithmetic costs should be high
        }

        // Some redundant recompute, but small as a fraction of the total amount of work
        internal_assert(optimal.redundant_compute_cost > 0);
        internal_assert(optimal.redundant_compute_cost < 0.1 * total_arith);

    }

    // Smaller footprint stencil -> smaller tiles
    {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y) * (x + y);
        h(x, y) = f(x-1, y-1) + f(x+1, y+1);

        h.estimate(x, 0, 1024).estimate(y, 0, 1024);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        double total_arith = 0;
        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g.funcs.size() == 1);
            const auto &fs = g.funcs[0];
            internal_assert(fs.is_output);
            internal_assert(fs.splits.size() == 2);

            total_arith += g.instances * fs.arith;

            internal_assert(g.instances > 1 && g.instances < 100); // There should be some modest number of tiles

            if (!fs.func.same_as(h.function())) {
                internal_assert(fs.depth == 2); // Computed inside two loops of the consumer
                internal_assert(fs.parent.same_as(h.function()));
                internal_assert(fs.splits[0].empty());
                internal_assert(fs.splits[1].empty());
            } else {
                internal_assert(fs.splits[0].size() == 1);
                internal_assert(fs.splits[1].size() == 1);
                internal_assert(tile_x > fs.splits[0][0]);
                internal_assert(tile_y > fs.splits[1][0]);
                tile_x = fs.splits[0][0];
                tile_y = fs.splits[1][0];
                internal_assert(tile_x > 15 && tile_x < 300);
                internal_assert(tile_y > 15 && tile_y < 300);
                internal_assert(tile_x * 2 >= tile_y && tile_y * 2 >= tile_x);
            }

            internal_assert(fs.memory > 100 && fs.memory < 100000); // Memory costs should be moderate
            internal_assert(fs.arith > 1000); // Arithmetic costs should be high
        }

        // Some redundant recompute, but small as a fraction of the total amount of work
        internal_assert(optimal.redundant_compute_cost > 0);
        internal_assert(optimal.redundant_compute_cost < 0.1 * total_arith);

    }

    //
}

}
}
