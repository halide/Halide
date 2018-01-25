#include "AutoScheduleTopDown.h"
#include "FindCalls.h"
#include "IRVisitor.h"
#include "IRMutator.h"
#include "OutputImageParam.h"
#include "RealizationOrder.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"

#include <set>
#include <queue>
#include <algorithm>
#include <fstream>
#include <chrono>

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

        // The amount of compute done per point evaluated, including the need to generate the call.
        double compute;

        // The amount of compute done per point evaluated if inlined.
        double compute_if_inlined;

        // The memory cost of loading all of this Func, in terms of the same symbolic region
        Expr memory;

        // The min/max variables used to denote a symbolic region of
        // this Func. Used in the cost above, and in the Edges below.
        vector<Interval> region;
    };

    struct Edge {
        Function producer, consumer;

        // The region required of producer in terms of a symbolic
        // region of the consumer
        vector<Interval> bounds;

        // The number of calls the consumer makes to the producer, per
        // point evaluated in the consumer.
        int calls;
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
            Expr memory_allocated = make_zero(Int(64));
            for (Expr e : consumer.values()) {
                memory_allocated += e.type().bytes();
            }
            Scope<Interval> scope;
            node.func = consumer;
            for (int i = 0; i < consumer.dimensions(); i++) {
                Expr min_var = Variable::make(Int(32), consumer.name() + "." + std::to_string(i) + ".min");
                Expr max_var = Variable::make(Int(32), consumer.name() + "." + std::to_string(i) + ".max");
                Expr extent = max_var - min_var + 1;
                memory_allocated *= extent;
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

            // This is where the cost model is encoded!
            node.compute = counter.leaves;
            node.compute_if_inlined = std::max(0, counter.leaves - consumer.dimensions());
            Expr balance = cast<double>(params.balance), llc = cast<double>(params.last_level_cache_size);
            Expr cost_per_load = (balance / llc) * min(memory_allocated, llc);
            node.memory = simplify(memory_allocated * cost_per_load);

            // Set parameter estimates (we could also do this in compute_bounds_and_costs)
            class ApplyParamEstimates : public IRMutator {
                using IRMutator::visit;

                void visit(const Variable *op) override {
                    if (op->param.defined()) {
                        if (!op->param.is_buffer()) {
                            expr = op->param.get_estimate();
                        } else {
                            for (int i = 0; i < op->param.dimensions(); i++) {
                                if (op->name == op->param.name() + ".min." + std::to_string(i)) {
                                    expr = op->param.min_constraint_estimate(i);
                                } else if (op->name == op->param.name() + ".extent." + std::to_string(i)) {
                                    expr = op->param.extent_constraint_estimate(i);
                                }
                            }
                        }
                    } else {
                        expr = op;
                    }
                    internal_assert(expr.defined()) << "Missing estimate for " << op->name << "\n";
                }
            } apply_param_estimates;

            // Now create the edges that lead to this func
            for (auto p : boxes_required(exprs, scope)) {
                auto it = env.find(p.first);
                if (it != env.end()) {
                    // Discard loads from input images
                    Edge edge;
                    edge.consumer = consumer;
                    edge.producer = env[p.first];
                    edge.bounds = p.second.bounds;
                    for (Interval &i : edge.bounds) {
                        i.max = simplify(apply_param_estimates.mutate(i.max));
                        i.min = simplify(apply_param_estimates.mutate(i.min));
                    }
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
            debug(0) << "  Arithmetic cost: " << n.compute << "\n";
            debug(0) << "  Inlined cost: " << n.compute_if_inlined << "\n";
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
        vector<std::pair<int, int>> store_bounds, compute_bounds;

        // The current compute cost, given these bounds
        double compute;

        // The current memory cost for loading from this func, given these bounds.
        double memory;

        bool is_output;

        // Outputs. Created as a side-effect of the search, but not
        // used in the search.

        // Which Func's loop nest are we stored within?
        Function parent;

        // and at what depth (how many loops of the parent are
        // outside the production of this Func)
        int store_depth, compute_depth;

        // The split factors for each dimension
        vector<vector<int>> splits;

        // The ordering of the outer dimensions peeled off this func's
        // loop nest, from outermost in.
        vector<int> loop_order;
    };

    // The number of times this group is evaluated at these
    // sizes. In the future this may need to track the actual outer
    // loops.
    int64_t compute_instances, store_instances;

    vector<FunctionState> funcs;
    float parallelism;

    bool may_partition;
    // Everything in this group is inlined into the sole output
    bool inlined;

    bool fully_scheduled() const {
        return inlined || funcs.size() == 1;
    }

    uint64_t hash() const {
        uint64_t result = inlined;
        for (const auto &fs : funcs) {
            result *= 37;
            result ^= uintptr_t(fs.func.get_contents().get());
            result *= 37;
            result ^= fs.compute_depth;
            if (fs.compute_depth) {
                result *= 37;
                result ^= uintptr_t(fs.parent.get_contents().get());
            }
            for (const auto &s : fs.splits) {
                for (int i : s) {
                    result *= 37;
                    result ^= i;
                }
            }
            for (int i : fs.loop_order) {
                result *= 37;
                result ^= i;
            }
        }
        return result;
    }

    int64_t eval_bound(Expr e, const map<string, Expr> &concrete_bounds) {
        if (const int64_t *i = as_const_int(e)) {
            return *i;
        } else if (const Variable *v = e.as<Variable>()) {
            return eval_bound(concrete_bounds.at(v->name), concrete_bounds);
        } else if (const Add *op = e.as<Add>()) {
            return eval_bound(op->a, concrete_bounds) + eval_bound(op->b, concrete_bounds);
        } else if (const Sub *op = e.as<Sub>()) {
            return eval_bound(op->a, concrete_bounds) - eval_bound(op->b, concrete_bounds);
        } else if (const Mul *op = e.as<Mul>()) {
            return eval_bound(op->a, concrete_bounds) * eval_bound(op->b, concrete_bounds);
        } else if (const Max *op = e.as<Max>()) {
            return std::max(eval_bound(op->a, concrete_bounds), eval_bound(op->b, concrete_bounds));
        } else if (const Min *op = e.as<Min>()) {
            return std::min(eval_bound(op->a, concrete_bounds), eval_bound(op->b, concrete_bounds));
        } else {
            return eval_bound(simplify(substitute(concrete_bounds, e)), concrete_bounds);
        }
    }

    void compute_bounds_and_costs(const FunctionDAG &dag) {
        map<Function, size_t, Function::Compare> group_members;
        set<FunctionState *> outputs;
        for (size_t i = 0; i < funcs.size(); i++) {
            group_members[funcs[i].func] = i;
            funcs[i].compute = 0;
            funcs[i].memory = 0;
            if (!funcs[i].is_output) {
                funcs[i].compute_bounds.clear();
            } else {
                outputs.insert(&funcs[i]);
            }
        }

        // Compute the support region for each Func
        map<string, Expr> concrete_bounds;

        for (auto &consumer : funcs) {
            internal_assert(dag.incoming_edges.count(consumer.func));

            const auto *node = dag.node_map.at(consumer.func);
            internal_assert(node->region.size() == (size_t)consumer.func.dimensions());
            internal_assert(consumer.compute_bounds.size() == (size_t)consumer.func.dimensions());
            for (int i = 0; i < consumer.func.dimensions(); i++) {
                concrete_bounds[node->region[i].min.as<Variable>()->name] = consumer.compute_bounds[i].first;
                concrete_bounds[node->region[i].max.as<Variable>()->name] = consumer.compute_bounds[i].second;
            }

            for (const auto *edge : dag.incoming_edges.at(consumer.func)) {
                if (!group_members.count(edge->producer)) continue; // The producer is in another group.
                // Find the producer
                FunctionState &producer = funcs[group_members.at(edge->producer)];

                internal_assert(edge->consumer.same_as(consumer.func));
                internal_assert(edge->producer.same_as(producer.func));
                internal_assert(!producer.is_output);

                bool first_consumer = producer.compute_bounds.empty();
                if (first_consumer) {
                    producer.compute_bounds.resize(producer.func.dimensions());
                }

                // Expand the bounds of the producer using the bounds
                // relationship encoded in the edge.
                for (int i = 0; i < producer.func.dimensions(); i++) {
                    Interval interval = edge->bounds[i];
                    int64_t min = eval_bound(interval.min, concrete_bounds);
                    int64_t max = eval_bound(interval.max, concrete_bounds);
                    if (first_consumer) {
                        producer.compute_bounds[i] = {(int)(min), (int)(max)};
                    } else {
                        producer.compute_bounds[i].first = std::min(producer.compute_bounds[i].first, (int)(min));
                        producer.compute_bounds[i].second = std::max(producer.compute_bounds[i].second, (int)(max));
                    }
                }
            }
        }

        // Use these bounds and number-of-call relationships to
        // figure out the minimum number of points computed for
        // each producer, and thus best-case compute arithmetic cost.
        map<Function, double, Function::Compare> points_computed;
        for (auto &f : funcs) {
            double calls = 1;
            double cost = 0;
            const auto *node = dag.node_map.at(f.func);
            if (f.is_output || !inlined) {
                // Consider explicitly realizing it
                for (int i = 0; i < f.func.dimensions(); i++) {
                    int extent = f.compute_bounds[i].second - f.compute_bounds[i].first + 1;
                    if (i == 0) {
                        // Assume vectorization by 16
                        calls *= ((extent + 15) / 16) * 16;
                    } else {
                        calls *= extent;
                    }
                }
                cost = calls * node->compute;
            }

            // Now consider inlining it
            if (!f.is_output) {
                double calls_if_inlined = 0;
                for (const auto *edge : dag.outgoing_edges.at(f.func)) {
                    calls_if_inlined += points_computed[edge->consumer] * edge->calls;
                }
                // Apply a compute discount if inlined for skipping the call node.
                double cost_if_inlined = calls_if_inlined * node->compute_if_inlined;
                // debug(0) << f.func.name() << ": " << calls << ", " << calls_if_inlined << ", " << cost << ", " << cost_if_inlined << "\n";
                if (inlined) {
                    calls = calls_if_inlined;
                    cost = cost_if_inlined;
                } else {
                    calls = std::min(calls, calls_if_inlined);
                    cost = std::min(cost, cost_if_inlined);
                }
            }

            points_computed[f.func] = calls;
            f.compute = cost;
        }

        if (!inlined) {
            // Recompute memory costs using the new bounds.
            for (auto &func : funcs) {
                internal_assert(dag.node_map.count(func.func));
                Expr memory = simplify(substitute(concrete_bounds, dag.node_map.at(func.func)->memory));
                const double *i_memory = as_const_float(memory);
                internal_assert(i_memory) << memory;
                func.memory = *i_memory;
            }
        }
    }
};

struct State {
    vector<std::shared_ptr<Group>> groups;
    double redundant_compute_cost, essential_compute_cost, memory_cost;

    bool last_action_was_fuse;
    int last_fuse_dim;

    bool cheaper_than(const State &other) {
        return (redundant_compute_cost + memory_cost) < (other.redundant_compute_cost + other.memory_cost);
    }

    uint64_t hash() const {
        uint64_t result = groups.size();
        for (auto &g : groups) {
            result = (result * 1234567) ^ g->hash();
        }
        return result;
    }

    void dump_to_postscript(const std::string &filename) const {
        std::ofstream out(filename);
        size_t y = 0, x = 0;
        out << "(Courier) findfont 12 scalefont setfont\n"
            << "<</PageSize [800 800] >> setpagedevice\n";

        struct ContainingLoop {
            string func;
            int depth;
            int start_y;
            int width;
            void end(std::ofstream &out, int x, int end_y) {
                int start_x = x - width/2;
                int end_x = x + width/2;
                out << start_x << " " << start_y << " moveto\n"
                    << start_x << " " << end_y << " lineto\n"
                    << end_x << " " << end_y << " lineto\n"
                    << end_x << " " << start_y << " lineto\n"
                    << start_x << " " << start_y << " lineto\n";
            }
        };

        vector<ContainingLoop> containing_loops;

        y += 10;
        x += 110;

        for (const auto &g : groups) {
            const auto &f = g->funcs[0];
            if (f.compute_depth == 0) {
                containing_loops.clear();
            } else {
                while (containing_loops.back().func != f.parent.name()) {
                    containing_loops.back().end(out, x, y);
                    containing_loops.pop_back();
                    y += 10;
                }
                while (containing_loops.back().depth > f.compute_depth) {
                    containing_loops.back().end(out, x, y);
                    containing_loops.pop_back();
                    y += 10;
                }
                while (containing_loops.back().depth < f.compute_depth) {
                    ContainingLoop l = containing_loops.back();
                    l.depth++;
                    l.start_y = y;
                    l.width = 200 - containing_loops.size() * 20;
                    y += 10;
                    containing_loops.push_back(l);
                }
            }

            for (int i = 0; i < (int)f.loop_order.size() + (g->funcs.size() > 1); i++) {
                ContainingLoop l;
                l.func = f.func.name();
                l.depth = i + 1;
                l.start_y = y;
                l.width = 200 - containing_loops.size() * 20;
                y += 10;
                containing_loops.push_back(l);
            }

            for (const auto &f : g->funcs) {
                y += 5;
                int left = x - (f.func.name().size() * 7) / 2;
                out << left << " " << y << " moveto\n"
                    << '(' << f.func.name() << ") show\n";
                y += 20;
            }
        }

        while (!containing_loops.empty()) {
            containing_loops.back().end(out, x, y);
            containing_loops.pop_back();
            y += 10;
        }

        out << "stroke showpage\n";
        out.close();
    }

    void dump() const {
        double total_compute = 0;
        for (const auto &g : groups) {
            for (const auto &f : g->funcs) {
                total_compute += f.compute * g->compute_instances;
            }
        }
        debug(0) << "State: \n";
        int counter = 0;
        debug(0) << "  memory cost: " << memory_cost << "\n";
        debug(0) << "  total compute: " << total_compute << "\n";
        debug(0) << "  essential compute: " << (total_compute - redundant_compute_cost) << "\n";
        debug(0) << "  redundant compute: " << redundant_compute_cost << "\n";
        debug(0) << "  total cost: " << (redundant_compute_cost + memory_cost) << "\n";
        debug(0) << "  compute multiplier: " << (1 + redundant_compute_cost / (total_compute - redundant_compute_cost)) << "\n";
        for (const auto &g : groups) {
            debug(0) << "  Group " << counter++ << ":\n"
                     << "    parallelism: " << g->parallelism << "\n"
                     << "    compute instances: " << g->compute_instances << "\n"
                     << "    store instances: " << g->store_instances << "\n"
                     << "    inlined: " << g->inlined << "\n";
            for (const auto &f : g->funcs) {
                string parent = "none";
                if (f.compute_depth) parent = f.parent.name();
                debug(0) << "    Function: " << f.func.name() << "\n"
                         << "      compute cost: " << f.compute << "\n"
                         << "      memory cost: " << f.memory << "\n"
                         << "      is_output: " << f.is_output << "\n"
                         << "      parent " << parent << "\n"
                         << "      compute_depth: " << f.compute_depth << "\n"
                         << "      store_depth: " << f.store_depth << "\n"
                         << "      compute bounds: ";
                for (const auto &i : f.compute_bounds) {
                    debug(0) << "[" << i.first << ", " << i.second << "] ";
                }
                debug(0) << "\n      store bounds: ";
                for (const auto &i : f.store_bounds) {
                    debug(0) << "[" << i.first << ", " << i.second << "] ";
                }
                debug(0) << "\n      loop order: ";
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
            g->compute_bounds_and_costs(dag);
        }
    }

    bool fully_scheduled() const {
        for (auto &g : groups) {
            if (!g->fully_scheduled()) return false;
        }
        return true;
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
        const Group &g = *(state.groups[i]);
        if (g.fully_scheduled()) continue;
        if (group_idx == -1 ||
            g.funcs.size() > state.groups[group_idx]->funcs.size()) {
            group_idx = i;
        }
    }

    if (group_idx == -1) {
        // This state is fully scheduled
        debug(0) << "  All groups fully scheduled. No children\n";
        return result;
    }

    const Group &group = *(state.groups[group_idx]);

    // Try partitioning the group
    if (group.funcs.size() > 1) {

        int num_outputs = 0;
        for (const auto &fs : group.funcs) {
            num_outputs += fs.is_output;
        }


        // Try inlining the group
        if (num_outputs == 1) {
            std::shared_ptr<Group> new_group(new Group(group));
            new_group->inlined = true;
            new_group->may_partition = false;
            new_group->compute_bounds_and_costs(dag);

            // TODO: record the total compute for a group somewhere to avoid recomputing it everywhere
            double old_compute = 0;
            for (auto &fs : group.funcs) {
                old_compute += fs.compute;
            }
            old_compute *= new_group->compute_instances;

            double new_compute = 0;
            for (auto &fs : new_group->funcs) {
                new_compute += fs.compute;
            }
            new_compute *= new_group->compute_instances;

            // Inlining may often be absurdly bad. Don't clutter the beam.
            if (true || new_compute < 2*old_compute) {
                std::shared_ptr<State> child(new State(state));
                child->redundant_compute_cost += new_compute - old_compute;
                child->groups[group_idx] = new_group;
                child->last_action_was_fuse = false;
                result.emplace_back(std::move(child));

                if (new_group->funcs.size() == 2 &&
                    new_group->funcs[0].func.name() == "output") {
                    // if (new_compute < old_compute) {
                    debug(0) << "OLD:\n";
                    state.dump();
                    debug(0) << "NEW:\n";
                    result.back()->dump();
                }
                internal_assert(new_compute >= old_compute) << "Inlining reduced total amount of work: " << old_compute << " -> " << new_compute << "\n";
            }

            // If inlining was free, don't bother generating other children
            if (new_compute == old_compute) {
                return result;
            }
        }

        // Try fusing the group under an outer parallel loop
        do {
            // debug(0) << "  Considering fusing\n";
            // Find the group outputs
            set<Function, Function::Compare> outputs;
            set<Function, Function::Compare> group_members;
            bool can_parallelize = true;
            const Group::FunctionState *old_output_state = nullptr;
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
                if (state.last_action_was_fuse && dim >= state.last_fuse_dim) {
                    // We can reach this child state in a simpler way, so prune.
                    continue;
                }

                int extent = -1;
                for (auto &fs : group.funcs) {
                    if (fs.func.same_as(output)) {
                        extent = fs.compute_bounds[dim].second - fs.compute_bounds[dim].first + 1;
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
                        if (factor < 32) {
                            factor = 32; // Set to 32 to make camera pipe work, can be removed once we correctly cost unused vector lanes
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

                    std::shared_ptr<Group> new_group(new Group(group));
                    new_group->may_partition = true;
                    Group::FunctionState *output_state = nullptr;
                    double old_compute = 0;
                    for (auto &fs : new_group->funcs) {
                        if (fs.func.same_as(output)) {
                            output_state = &fs;
                        }
                        old_compute += fs.compute;
                    }
                    old_compute *= new_group->compute_instances;

                    internal_assert(output_state);

                    int &min = output_state->compute_bounds[dim].first;
                    int &max = output_state->compute_bounds[dim].second;

                    int outer_loop_size = (extent + split - 1) / split;
                    new_group->compute_instances *= outer_loop_size;

                    max = min + split - 1;

                    // Recompute bounds and arithmetic cost up the pipeline
                    new_group->compute_bounds_and_costs(dag);

                    // Should not have updated the bounds of the group output
                    internal_assert(max == min + split - 1);

                    double new_compute = 0;
                    for (auto &fs : new_group->funcs) {
                        new_compute += fs.compute;
                    }
                    new_compute *= new_group->compute_instances;

                    // Reject out-of-hand anything particularly dumb
                    // (e.g. that causes 2x redundant work or
                    // more). Sometimes a painful partitioning of the
                    // pipeline must occur. Beam search with a large
                    // beam will eventually decide to try it. Beam
                    // search with a small beam can avoid it by
                    // flooding the beam with slightly less painful
                    // incremental fuses that lead to worse overall
                    // outcomes.
                    //if (new_compute > 2*old_compute) {
                    //    continue;
                    //}

                    // Record what we did
                    for (auto &fs : new_group->funcs) {
                        if (fs.func.same_as(output)) {
                            fs.splits[dim].push_back(split);
                            fs.loop_order.push_back(dim);
                        } else {
                            if (fs.parent.same_as(output)) {
                                fs.store_depth++;
                                fs.compute_depth++;
                            } else {
                                fs.store_depth = fs.compute_depth = 1;
                            }
                            fs.parent = output;
                        }
                    }

                    std::shared_ptr<State> child(new State(state));
                    //internal_assert(new_compute >= old_compute) << "Fusing reduced total amount of work: " << old_compute << " -> " << new_compute << "\n";
                    child->redundant_compute_cost += new_compute - old_compute;
                    child->groups[group_idx] = new_group;
                    child->last_action_was_fuse = true;
                    child->last_fuse_dim = dim;

                    if (0) {
                        debug(0) << "Considering fusing group along output dimension " << dim << " with factor " << split << "\n";
                        debug(0) << "Old group:\n";
                        state.dump();
                        debug(0) << "New group:\n";
                        child->dump();
                    }
                    result.emplace_back(std::move(child));
                }
            }
        } while (0);

        // Try partitioning the group into smaller groups
        int num_partitions = 0;
        for (size_t i = 1; group.may_partition && i < group.funcs.size(); i++) {
            if (dag.node_map.at(group.funcs[i].func)->compute_if_inlined == 0) {
                // It's dumb to partition here.
                //continue;
            }

            // debug(0) << "  Generating partition...\n";
            // TODO: If it's possible for a partioning to be
            // non-contiguous in the realization order, then this is a
            // subset of interesting partitionings. A better choice
            // would be to pick one Func, and then put everything
            // upstream of it in group A.
            std::shared_ptr<State> child(new State(state));
            map<Function, size_t, Function::Compare> funcs_in_A, funcs_in_B;
            std::shared_ptr<Group> A(new Group(group)), B(new Group(group));
            // Don't explore states that just partition A again - we
            // could equally have just made a smaller partition here.
            // TODO: The following rules out good states. Not sure why.
            A->funcs.clear();
            B->funcs.clear();
            set<Function, Function::Compare> in_A;
            vector<Function> pending = {group.funcs[i].func};

            // find everything upstream and place it in A
            while (!pending.empty()) {
                Function f = pending.back();
                pending.pop_back();
                in_A.insert(f);
                for (const auto *edge : dag.incoming_edges.at(f)) {
                    pending.push_back(edge->producer);
                }
            }

            for (size_t j = 0; j < group.funcs.size(); j++) {
                if (in_A.count(group.funcs[j].func)) {
                    funcs_in_A[group.funcs[j].func] = A->funcs.size();
                    A->funcs.push_back(group.funcs[j]);
                    // debug(0) << "A: " << group.funcs[j].func.name() << "\n";
                } else {
                    funcs_in_B[group.funcs[j].func] = B->funcs.size();
                    B->funcs.push_back(group.funcs[j]);
                    // debug(0) << "B: " << group.funcs[j].func.name() << "\n";
                }
            }

            internal_assert(funcs_in_A.size() && funcs_in_B.size());

            // Find the broken edges
            for (const auto &edge : dag.edges) {
                auto producer_in_A = funcs_in_A.find(edge.producer);
                auto consumer_in_B = funcs_in_B.find(edge.consumer);
                if (producer_in_A != funcs_in_A.end() &&
                    consumer_in_B != funcs_in_B.end()) {
                    // This is a broken edge

                    // Incur a memory cost
                    child->memory_cost += A->funcs[producer_in_A->second].memory * A->compute_instances;

                    // Update the output flags
                    A->funcs[producer_in_A->second].is_output = true;
                }
            }

            // If there are multiple outputs, our only possible next
            // move is a partition, so don't bother - we could have
            // just made a smaller partition.
            int num_outputs = 0;
            for (const auto &fs : A->funcs) {
                num_outputs += fs.is_output;
            }

            if (num_outputs == 1) {
                // Keep the groups in topological order, to make debugging easier.
                child->groups[group_idx] = std::move(A);
                child->groups.emplace(child->groups.begin() + group_idx, std::move(B));
                child->last_action_was_fuse = false;
                result.emplace_back(std::move(child));
                num_partitions++;
            }
        }
        internal_assert(group.may_partition);

        if (group.may_partition && !num_partitions) {
            debug(0) << "Unpartitionable group " << group_idx << "!\n";
            state.dump();
        }

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
    // cost, unused vector lanes costs?


    return result;
}

// Returns optimal schedule
State optimal_schedule(const FunctionDAG &dag, vector<Function> outputs, const MachineParams &params, size_t beam_size) {
    std::set<uint64_t> visited;
    State initial;
    {
        initial.redundant_compute_cost = 0;
        initial.memory_cost = 0;
        initial.last_action_was_fuse = false;
        initial.last_fuse_dim = 0;

        std::shared_ptr<Group> group(new Group);
        group->parallelism = params.parallelism;
        group->compute_instances = 1;
        group->store_instances = 1;
        group->inlined = false;
        group->may_partition = true;
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
                    f.compute_bounds.push_back(it->second);
                    f.store_bounds.push_back(it->second);
                }
            }

            f.splits.resize(f.func.dimensions());
            f.store_depth = 0;
            f.compute_depth = 0;

            group->funcs.emplace_back(std::move(f));
        }

        initial.groups.emplace_back(std::move(group));

        initial.compute_bounds_and_costs(dag);

        // Record our initial underestimate of compute cost
        initial.essential_compute_cost = 0;
        for (auto &g : initial.groups) {
            for (auto &f : g->funcs) {
                initial.essential_compute_cost += f.compute;
            }
        }
    }

    for (auto &g : initial.groups) {
        for (auto &fs : g->funcs) {
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
            // Bucket the states by number of groups
            map<size_t, decltype(queue)> queues;
            /*
            while (!queue.empty()) {
                queues[queue.top()->groups.size()].emplace(std::move(queue.top()));
                queue.pop();
            }
            */
            queues[0].swap(queue);

            // Take the top K from each bucket
            decltype(queue) trimmed;
            for (auto p : queues) {
                for (size_t i = 0; i < beam_size && !p.second.empty(); i++) {
                    trimmed.emplace(std::move(p.second.top()));
                    p.second.pop();
                }
            }
            queue.swap(trimmed);
        }

        internal_assert(!queue.empty());

        auto state = queue.top();
        queue.pop();

        if (state->fully_scheduled()) {
            // This is best-first search, and we hit a leaf, so it must be the best leaf.
            debug(0) << "Found completed schedule with cost " << state->memory_cost << " " << state->redundant_compute_cost << " " << state->essential_compute_cost << "\n";
            return *state;
        }

        if (report) debug(0) << "Considering partial schedule with cost " << state->groups.size() << " " << state->memory_cost << " " << state->redundant_compute_cost << " " << state->essential_compute_cost << "\n";
        //state.dump();

        auto children = make_children(*state, dag);
        for (auto &c : children) {
            uint64_t h = c->hash();
            if (visited.count(h)) {
                //debug(0) << "Already visited this state. Skipping\n";
                continue;
            }
            visited.insert(h);
            if (c->redundant_compute_cost * 10 > c->essential_compute_cost) {
                //debug(0) << "Rejecting child with costs: " << c->groups.size() << " " << c->memory_cost << " " << c->redundant_compute_cost << " " << c->essential_compute_cost << "\n";
            } else {
                //debug(0) << "Accepting child with costs: " << c->groups.size() << " " << c->memory_cost << " " << c->redundant_compute_cost << " " << c->essential_compute_cost << "\n";
                queue.emplace(std::move(c));
            }
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
        internal_assert(g->funcs.size() == 1 || g->inlined);
        auto &fs = g->funcs[0];
        internal_assert(fs.is_output);

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
        if (fs.compute_bounds[0].second - fs.compute_bounds[0].first + 1 >= 16) {
            Func(fs.func).vectorize(loops[0], 16);
        }
        if (fs.compute_depth == 0) {
            Func(fs.func).parallel(loops.back());
        }
    }

    // Then set the compute and store levels
    for (auto &g : optimal.groups) {
        internal_assert(g->funcs.size() == 1 || g->inlined);
        auto &fs = g->funcs[0];
        internal_assert(fs.is_output);

        if (fs.compute_depth == 0) {
            fs.func.schedule().compute_level() = LoopLevel::root();
        } else {
            const vector<Dim> &dims = fs.parent.definition().schedule().dims();
            Var v(dims[dims.size() - fs.compute_depth - 1].var); // -1 due to Var::outermost()
            fs.func.schedule().compute_level() = LoopLevel(fs.parent, v);
        }

        if (fs.store_depth == 0) {
            fs.func.schedule().store_level() = LoopLevel::root();
        } else {
            const vector<Dim> &dims = fs.parent.definition().schedule().dims();
            Var v(dims[dims.size() - fs.store_depth - 1].var); // -1 due to Var::outermost()
            fs.func.schedule().store_level() = LoopLevel(fs.parent, v);
        }
    }

    Func output(optimal.groups[0]->funcs[0].func);

    optimal.dump_to_postscript(output.name() + ".ps");

    return "";
}

void autoschedule_test() {
    MachineParams params(8, 16 * 1024 * 1024, 40);
    size_t beam_size = 1000;
    Target target("host");

    Var x, y;

    {
        // In a point-wise pipeline, everything should be fully fused.
        Func f, g, h;
        f(x, y) = (x + y) * (x + y);
        g(x, y) = f(x, y) * 2 + 1;
        h(x, y) = g(x, y) * 2 + 1;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        internal_assert(optimal.redundant_compute_cost == 0); // No redundant recompute

        internal_assert(optimal.groups.size() == 1); // This should have been inlined into a single group
        for (const auto &g : optimal.groups) {
            internal_assert(g->inlined);
            internal_assert(g->funcs.size() == 3); // There should be one entry per func
            const auto &fs = g->funcs[0];
            internal_assert(fs.is_output); // The first group element is the output
            for (const auto &fs : g->funcs) {
                internal_assert(fs.splits.size() == 2); // There is always one split list per dimension
                internal_assert(g->compute_instances == 1); // Full fusion with no tiling
                internal_assert(fs.memory == 0); // Memory costs should be zero
                internal_assert(fs.compute > 1000); // Arithmetic costs should be large
                internal_assert(fs.splits[0].empty()); // No splits
                internal_assert(fs.splits[1].empty());
            }
        }
    }

    {
        // In a pipeline with huge expensive stencils and low memory costs, nothing should be fused
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y) * (x + 4*y) * (x + 5*y);
        Expr e = 0;
        for (int i = 0; i < 100; i++) {
            e += f(x + i*10, y + i*10);
        }
        g(x, y) = e;
        e = 0;
        for (int i = 0; i < 100; i++) {
            e += g(x + i*10, y + i*10);
        }
        h(x, y) = e;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        MachineParams cheap_memory = params;
        cheap_memory.balance = 1;

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, cheap_memory);
        State optimal = optimal_schedule(dag, outputs, cheap_memory, beam_size);

        optimal.dump();

        internal_assert(optimal.redundant_compute_cost == 0); // No redundant recompute
        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g->funcs.size() == 1);
            const auto &fs = g->funcs[0];
            internal_assert(fs.splits.size() == 2);
            internal_assert(fs.is_output);

            internal_assert(g->compute_instances == 1); // No fusion means a single instance of each func
            internal_assert(fs.memory > 1000); // Memory costs should be high
            internal_assert(fs.compute > 1000); // Arithmetic costs should be high too

            internal_assert(fs.compute_depth == 0); // Computed at root
            internal_assert(fs.splits[0].empty()); // No splits
            internal_assert(fs.splits[1].empty());
        }
    }

    int tile_x = 0, tile_y = 0;
    {
        // In a pipeline with moderate isotropic stencils, there should be some square tiling
        Func f("f"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        h(x, y) = f(x-9, y-9) + f(x+9, y+9) + f(x-9, y+9) + f(x+9, y-9);

        h.estimate(x, 0, 2048).estimate(y, 0, 2048);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        double total_compute = 0;
        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g->funcs.size() == 1);
            const auto &fs = g->funcs[0];
            internal_assert(fs.is_output);
            internal_assert(fs.splits.size() == 2);

            total_compute += g->compute_instances * fs.compute;

            internal_assert(g->compute_instances > 1 && g->compute_instances < 500); // There should be some modest number of tiles

            if (!fs.func.same_as(h.function())) {
                internal_assert(fs.compute_depth == 2); // Computed inside two loops of the consumer
                internal_assert(fs.parent.same_as(h.function()));
                internal_assert(fs.splits[0].empty());
                internal_assert(fs.splits[1].empty());
            } else {
                // For the output, moderately-sized squarish
                // tiles. The tiles may be wider than tall due to
                // vectorization.
                internal_assert(fs.splits[0].size() == 1);
                internal_assert(fs.splits[1].size() == 1);
                tile_x = fs.splits[0][0];
                tile_y = fs.splits[1][0];
                internal_assert(tile_x > 30 && tile_x < 600);
                internal_assert(tile_y > 30 && tile_y < 600);
                internal_assert(tile_x >= tile_y && tile_y * 4 >= tile_x);
            }

            internal_assert(fs.memory > 100 && fs.memory < 100000); // Memory costs should be moderate
            internal_assert(fs.compute > 1000); // Arithmetic costs should be high
        }

        // Some redundant recompute, but small as a fraction of the total amount of work
        internal_assert(optimal.redundant_compute_cost > 0);
        internal_assert(optimal.redundant_compute_cost < 0.1 * total_compute);

    }

    // Smaller footprint stencil -> smaller tiles
    {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        h(x, y) = f(x, y) + f(x+1, y+1) + f(x, y+1) + f(x+1, y);

        h.estimate(x, 0, 2048).estimate(y, 0, 2048);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();

        double total_compute = 0;
        for (const auto &g : optimal.groups) {
            // Check some invariants
            internal_assert(g->funcs.size() == 1);
            const auto &fs = g->funcs[0];
            internal_assert(fs.is_output);
            internal_assert(fs.splits.size() == 2);

            total_compute += g->compute_instances * fs.compute;

            internal_assert(g->compute_instances > 1 && g->compute_instances < 1000); // There should be some modest number of tiles

            if (!fs.func.same_as(h.function())) {
                internal_assert(fs.compute_depth == 2); // Computed inside two loops of the consumer
                internal_assert(fs.parent.same_as(h.function()));
                internal_assert(fs.splits[0].empty());
                internal_assert(fs.splits[1].empty());
            } else {
                internal_assert(fs.splits[0].size() == 1);
                internal_assert(fs.splits[1].size() == 1);
                internal_assert(tile_x >= fs.splits[0][0]);
                internal_assert(tile_y >= fs.splits[1][0]);
                tile_x = fs.splits[0][0];
                tile_y = fs.splits[1][0];
                internal_assert(tile_x > 15 && tile_x < 300);
                internal_assert(tile_y > 15 && tile_y < 300);
                internal_assert(tile_x >= tile_y && tile_y * 8 >= tile_x);
            }

            internal_assert(fs.memory > 100 && fs.memory < 100000); // Memory costs should be moderate
            internal_assert(fs.compute > 1000); // Arithmetic costs should be high
        }

        // Some redundant recompute, but small as a fraction of the total amount of work
        internal_assert(optimal.redundant_compute_cost > 0);
        internal_assert(optimal.redundant_compute_cost < 0.1 * total_compute);

    }

    //
}

}
}
