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

        // The memory cost coefficient of loading a region of the Func. Multiply it by the number of points loaded squared.
        double memory;

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
            Scope<Interval> scope;
            node.func = consumer;
            for (int i = 0; i < consumer.dimensions(); i++) {
                Expr min_var = Variable::make(Int(32), consumer.name() + "." + std::to_string(i) + ".min");
                Expr max_var = Variable::make(Int(32), consumer.name() + "." + std::to_string(i) + ".max");
                Expr extent = max_var - min_var + 1;
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
            int bytes_per_element = 0;
            for (const auto &e : exprs_vector) {
                bytes_per_element += e.type().bytes();
            }
            node.memory = bytes_per_element * bytes_per_element;
            node.memory *= params.balance;
            node.memory /= params.last_level_cache_size;

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

vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        auto v = generate_tilings(s, d - 1);
        for (auto t : v) {
            bool is_full = false, is_one = false;
            // Skip trivial tilings
            if ((size_t)d == s.size() - 1) {
                is_one = is_full = true;
                for (int i = 0; i < d; i++) {
                    is_one &= (t[i] == 1);
                    is_full &= (t[i] == s[i]);
                }
            }
            t.push_back(s[d]);
            if (!is_full) {
                result.push_back(t);
            }
            for (int i = 1; i < 32 && i < s[d]; i *= 2) {
                if (is_one && i == 1) continue;
                t.back() = i;
                result.push_back(t);
            }
        }
    }
    return result;
}

// We're going to do a tree search over possible schedules to find an
// optimal one. A tree search requires a state, and a function that
// gives you children of the state (with costs). The following struct
// represents the state, which is a partial schedule.
//
// A partial schedule is a tree. Each node is some portion of the for
// loop nest of some Func. If there are no children, it's the
// innermost set of loops. If there are children, it's a loop over
// tiles of that Func.
struct PartialScheduleNode {
    Function func;

    // Is this the innermost loop of this func?
    bool innermost = false;

    // The extents of the loops
    vector<int64_t> size;

    // The nodes inside the loop body
    vector<std::shared_ptr<PartialScheduleNode>> children;

    // Funcs inlined into this inner loop, and the number of times they are called. Only valid if children is empty.
    map<Function, int64_t, Function::Compare> inlined;

    double cost(const FunctionDAG &dag, std::set<Function, Function::Compare> in_realization, int64_t instances, const PartialScheduleNode *parent) {
        double result = 0;

        const FunctionDAG::Node *node = is_root() ? nullptr : dag.node_map.at(func);

        int64_t subinstances = instances;
        for (auto i : size) {
            subinstances *= i;
        }
        if (innermost) {
            // Apply the compute cost
            result += node->compute * subinstances;

            // debug(0) << "Compute cost for " << func.name() << ": " << result << "\n";

            // Apply the compute cost of any inlined functions
            for (auto p : inlined) {
                result += dag.node_map.at(p.first)->compute_if_inlined * subinstances * p.second;
            }

            // Add some loop overhead to encourage larger inner
            // loops. Count the number of times we run the innermost
            // loop.
            int64_t loop_overhead = instances;
            for (size_t i = 1; i < size.size(); i++) {
                loop_overhead *= size[i];
            }
            result += loop_overhead;
        }

        if (!is_root() &&
            !in_realization.count(func)) {
            in_realization.insert(func);
            for (auto c : children) {
                result += c->cost(dag, in_realization, subinstances, this);
            }
            in_realization.erase(func);

            // Apply the memory cost
            int64_t points = 1;
            // debug(0) << "Region of " << func.name() << ":\n";
            for (auto p : parent->get_bounds(func, dag).region) {
                points *= p.second - p.first + 1;
                // debug(0) << "  " << p.first << ", " << p.second << "\n";
            }
            double mem_cost = node->memory * instances * points * points * dag.outgoing_edges.at(func).size();
            result += mem_cost;
            // debug(0) << "Memory cost for " << func.name() << ": " << mem_cost << "\n";
        } else {
            for (auto c : children) {
                result += c->cost(dag, in_realization, subinstances, this);
            }
        }

        return result;
    }

    bool is_root() const {
        return !func.get_contents().defined();
    }

    struct Bound {
        // The box over which something is touched
        vector<pair<int64_t, int64_t>> region;
        // The minimum number of points which must be evaluated
        int64_t min_points;
    };

    // The total bounds required of the given Func for one representative iteration of this loop. Computed lazily and cached.
    mutable map<Function, Bound, Function::Compare> bounds;
    const Bound &get_bounds(Function f, const FunctionDAG &dag) const {
        auto it = bounds.find(f);
        if (it != bounds.end()) {
            return it->second;
        }
        Bound bound;
        if (dag.outgoing_edges.at(f).empty() && is_root()) {
            // Use the bounds estimate
            bound.min_points = 1;
            map<string, pair<int64_t, int64_t>> estimates;
            for (auto b : f.schedule().estimates()) {
                int64_t i_min = *as_const_int(b.min);
                int64_t i_extent = *as_const_int(b.extent);
                estimates[b.var] = {i_min, i_min + i_extent - 1};
                bound.min_points *= i_extent;
            }
            // Set the bounds using the estimates
            for (int i = 0; i < f.dimensions(); i++) {
                auto it = estimates.find(f.args()[i]);
                user_assert(it != estimates.end())
                    << "Need an estimate on dimension " << i << " of \"" << f.name() << "\"";
                bound.region.push_back(it->second);
            }
        } else {
            internal_assert(!dag.outgoing_edges.at(f).empty())
                << "No consumers of " << f.name()
                << " at loop over " << (is_root() ? "root" : func.name()) << "\n";
            int64_t calls_if_inlined = 0;
            for (const auto *e : dag.outgoing_edges.at(f)) {
                const auto &c_bounds = get_bounds(e->consumer, dag);
                // expand bounds to satisfy consumer
                map<string, Expr> s;
                int i = 0;
                for (auto p : c_bounds.region) {
                    s[e->consumer.name() + "." + std::to_string(i) + ".min"] = (int)p.first;
                    s[e->consumer.name() + "." + std::to_string(i) + ".max"] = (int)p.second;
                    i++;
                }
                calls_if_inlined += c_bounds.min_points * e->calls;
                for (int i = 0; i < f.dimensions(); i++) {
                    Interval in = e->bounds[i];
                    in.min = simplify(substitute(s, in.min));
                    in.max = simplify(substitute(s, in.max));
                    const int64_t *imin = as_const_int(in.min);
                    const int64_t *imax = as_const_int(in.max);
                    internal_assert(imin && imax) << in.min << ", " << in.max << "\n";
                    if ((size_t)i >= bound.region.size()) {
                        bound.region.push_back({*imin, *imax});
                    } else {
                        bound.region[i].first = std::min(bound.region[i].first, *imin);
                        bound.region[i].second = std::min(bound.region[i].second, *imax);
                    }
                }
            }
            int64_t points_if_realized = 1;
            for (int i = 0; i < f.dimensions(); i++) {
                points_if_realized *= (bound.region[i].second - bound.region[i].first + 1);
            }
            bound.min_points = std::min(points_if_realized, calls_if_inlined);
            internal_assert(!bound.region.empty()) << is_root() << " " << f.name() << "\n";
        }
        bounds[f] = std::move(bound);
        return bounds[f];
    }

    void dump(string prefix) const {
        debug(0) << prefix << (is_root() ? "root" : func.name());
        for (auto s : size) {
            debug(0) << " " << s;
        }
        if (innermost) {
            debug(0) << " *\n";
        } else {
            debug(0) << "\n";
        }
        prefix += " ";
        for (auto c : children) {
            c->dump(prefix);
        }
        for (auto p : inlined) {
            debug(0) << prefix << " inlined: " << p.first.name() << "\n";
        }
    }

    bool calls(Function f, const FunctionDAG &dag) const {
        for (const auto &c : children) {
            if (c->calls(f, dag)) return true;
        }
        for (const auto *e : dag.outgoing_edges.at(f)) {
            if (e->consumer.same_as(func)) return true;
            if (inlined.count(e->consumer)) return true;
        }
        return false;
    }

    bool computes(Function f) const {
        if (!is_root() && f.same_as(func)) {
            return true;
        }
        if (inlined.count(f)) {
            return true;
        }
        for (const auto &c : children) {
            if (c->computes(f)) return true;
        }
        return false;
    }

    // Make a copy of the tree with the given func inlined.
    PartialScheduleNode inline_func(Function f, const FunctionDAG &dag) const {
        PartialScheduleNode result = *this;

        // Inline it into the children
        for (size_t i = 0; i < result.children.size(); i++) {
            if (children[i]->calls(f, dag)) {
                result.children[i] = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode(children[i]->inline_func(f, dag)));
            }
        }

        // Inline it here if there are any direct calls
        if (innermost) {
            int64_t calls = 0;
            for (const auto *e : dag.outgoing_edges.at(f)) {
                auto it = inlined.find(e->consumer);
                if (it != inlined.end()) {
                    calls += it->second * e->calls;
                }
                if (e->consumer.same_as(func)) {
                    calls += e->calls;
                }
            }
            if (calls) {
                result.inlined[f] = calls;
            }
        }
        return result;
    }

    void compute_here(Function f, const FunctionDAG &dag) {
        auto bounds = get_bounds(f, dag);
        auto node = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode);
        node->func = f;
        node->innermost = true;
        Bound single_point;
        single_point.min_points = 1;
        for (int i = 0; i < f.dimensions(); i++) {
            // Initialize the loop nest to cover the desired bounds
            node->size.push_back(bounds.region[i].second - bounds.region[i].first + 1);
            single_point.region.push_back({bounds.region[i].first, bounds.region[i].first});
        }
        node->bounds[f] = single_point;
        children.emplace_back(std::move(node));
    }

    // Return all possible ways to compute f in tiles.
    vector<PartialScheduleNode> compute_in_tiles(Function f, const FunctionDAG &dag, const PartialScheduleNode *parent) const {
        vector<PartialScheduleNode> result;

        // Figure out which child we can fuse this into
        int child = -1;
        bool called_by_multiple_children = false;
        for (int i = 0; i < (int)children.size(); i++) {
            if (children[i]->calls(f, dag)) {
                if (child != -1) {
                    called_by_multiple_children = true;
                }
                child = i;
            }
        }

        {
            // Place the computation inside this loop
            PartialScheduleNode r = *this;
            r.compute_here(f, dag);
            result.emplace_back(std::move(r));
        }

        if (dag.outgoing_edges.at(f).empty()) {
            // Can't tile outputs
            return result;
        }

        if (!is_root()) {
            // Generate a list of tile sizes to try
            auto tilings = generate_tilings(size, (int)(size.size() - 1));

            for (auto t : tilings) {
                // Tile this loop and place the computation at some coarser granularity
                PartialScheduleNode outer = *this;

                // First make an inner loop representing a 1x1x1... tile
                auto inner = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode);
                inner->size.resize(outer.size.size(), 1);
                inner->func = func;
                inner->innermost = innermost;

                // Move the existing children and their bounds to the inner loop
                std::swap(inner->children, outer.children);
                std::swap(inner->inlined, outer.inlined);
                std::swap(inner->bounds, outer.bounds);

                outer.bounds[func] = inner->bounds[func];
                outer.innermost = false;
                // Then move factors from the outer loop to the inner loop

                auto parent_bounds = parent->get_bounds(func, dag);
                for (size_t i = 0; i < t.size(); i++) {
                    int factor = t[i];
                    inner->size[i] = (outer.size[i] + factor - 1) / factor;
                    outer.size[i] = factor;
                    int64_t min = parent_bounds.region[i].first;
                    int64_t extent = parent_bounds.region[i].second - min + 1;
                    extent = (extent + factor - 1) / factor;
                    outer.bounds[func].region[i] = {min, min + extent - 1};
                    // TODO: min_points?
                }

                outer.children.push_back(inner);

                // Site the computation inside the outer loop
                outer.compute_here(f, dag);
                result.emplace_back(std::move(outer));
            }
        }

        if (child >= 0 && !called_by_multiple_children) {
            auto v = children[child]->compute_in_tiles(f, dag, this);
            for (PartialScheduleNode n : v) {
                // (Only valid if one child calls f) Push the computation into the child.
                PartialScheduleNode r = *this;
                r.children[child] = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode(n));
                result.emplace_back(std::move(r));
            }
        }

        return result;
    }

};

struct State {
    PartialScheduleNode root;

    double cost = 0;

    int num_funcs_scheduled = 0;

    void calculate_cost(const FunctionDAG &dag) {
        std::set<Function, Function::Compare> in_realization;
        cost = root.cost(dag, in_realization, 1, nullptr);
    }

    void generate_children(const FunctionDAG &dag, std::function<void(State *)> &accept_child) {
        internal_assert(root.is_root());

        if (num_funcs_scheduled == (int)dag.nodes.size()) {
            return;
        }

        // Enumerate all legal ways to schedule the next Func
        Function f = dag.nodes[num_funcs_scheduled].func;
        for (const auto *e : dag.outgoing_edges.at(f)) {
            internal_assert(root.computes(e->consumer))
                << "Partially scheduled code doesn't compute " << e->consumer.name()
                << ", which is one of the consumers of " << f.name();
        }

        debug(0) << "Scheduling " << f.name() << "\n";

        // 1) Inline it
        if (!dag.outgoing_edges.at(f).empty()) {
            auto child = new State(*this);
            child->root = child->root.inline_func(f, dag);
            child->num_funcs_scheduled++;
            child->calculate_cost(dag);
            internal_assert(child->root.computes(f)) << "Failed to inline " << f.name() << "\n";
            accept_child(child);
        }

        // 2) Realize it somewhere
        auto tile_options = root.compute_in_tiles(f, dag, nullptr);
        for (PartialScheduleNode n : tile_options) {
            auto child = new State(*this);
            child->root = std::move(n);
            child->num_funcs_scheduled++;
            child->calculate_cost(dag);
            internal_assert(child->root.computes(f)) << "Failed to inject realization of " << f.name() << "\n";
            accept_child(child);
        }
    }

    void dump() const {
        debug(0) << "State with cost " << cost << ":\n";
        root.dump(" ");
    }
};


struct CompareStates {
    bool operator()(const std::shared_ptr<State> &a, const std::shared_ptr<State> &b) const {
        return a->cost > b->cost;
    }
};

State optimal_schedule(FunctionDAG &dag, vector<Function> outputs, const MachineParams &params, int beam_size) {
    std::priority_queue<std::shared_ptr<State>,
                        std::vector<std::shared_ptr<State>>,
                        CompareStates> q;

    q.emplace(new State);

    std::function<void(State *)> enqueue_new_children = [&](State *s) {
        // debug(0) << "Generated child: ";
        // s->dump();
        q.emplace(std::shared_ptr<State>(s));
    };

    for (int i = 0; ; i++) {

        if (q.size() > (size_t)beam_size) {
            decltype(q) trimmed;
            for (int i = 0; i < beam_size; i++) {
                trimmed.push(q.top());
                q.pop();
            }
            q.swap(trimmed);
        }

        internal_assert(!q.empty());
        auto state = q.top();
        q.pop();


        if (true || i % 1000 == 0) {
            debug(0) << "** Queue top: ";
            state->dump();
        }

        if (state->num_funcs_scheduled == (int)dag.nodes.size()) {
            return *state;
        }

        state->generate_children(dag, enqueue_new_children);
    }
}

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
            if (beam_size == 1 || s.cost < optimal.cost) {
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


    return "";
}

void autoschedule_test() {
    MachineParams params(8, 16 * 1024 * 1024, 40);
    size_t beam_size = 1000000;
    Target target("host");

    Var x, y;

    {
        // In a point-wise pipeline, everything should be fully fused.
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);
        g(x, y) = f(x, y) * 2 + 1;
        h(x, y) = g(x, y) * 2 + 1;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        optimal.dump();
        debug(0) << "\n";

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
        debug(0) << "\n";
    }

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
        debug(0) << "\n";
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
        debug(0) << "\n";
    }

    // A stencil chain
    {
        const int N = 32;
        Func f[N];
        f[0](x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        for (int i = 1; i < N; i++) {
            Expr e = 0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    e += f[i-1](x + dx, y + dy);
                }
            }
            f[i](x, y) = e;
        }
        f[N-1].estimate(x, 0, 2048).estimate(y, 0, 2048);
        vector<Function> outputs = {f[N-1].function()};
        FunctionDAG dag(outputs, params);
        State optimal = optimal_schedule(dag, outputs, params, 1);
        optimal.dump();
        debug(0) << "\n";

    }
}

}
}
