#include "AutoScheduleNew.h"
#include "FindCalls.h"
#include "IRVisitor.h"
#include "IRMutator.h"
#include "OutputImageParam.h"
#include "RealizationOrder.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"
#include "PartitionLoops.h"

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

// This should be a function f s.t
// f(0) = 0
// f(params.last_level_cache_size) = params.balance
double cost_of_cold_load(double buffer_size, const MachineParams &params) {
    return params.balance * std::sqrt(buffer_size / params.last_level_cache_size);
    //return params.balance * std::log2(1 + buffer_size / params.last_level_cache_size);
}

uint64_t get_dropout_threshold() {
    string random_dropout_str = get_env_variable("HL_RANDOM_DROPOUT");
    if (!random_dropout_str.empty()) {
        return atoi(random_dropout_str.c_str());
    } else {
        return 100;
    }
}

bool random_dropout() {
    static uint64_t threshold = get_dropout_threshold();
    uint64_t r = rand();
    bool drop_it = (r % 100) >= threshold;
    return drop_it;
}

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

        // The vectorization width that should be used.
        int vector_size;
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
    FunctionDAG(const vector<Function> &outputs, const MachineParams &params, const Target &target) {
        map<string, Function> env;
        for (Function o : outputs) {
            populate_environment(o, env);
        }

        // Compute a realization order
        vector<string> order = topological_order(outputs, env);

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
                bool likely = false;

                using IRVisitor::visit;
                void visit(const IntImm *op) override {
                    leaves++;
                    check_type(op->type);
                }

                void visit(const UIntImm *op) override {
                    leaves++;
                    check_type(op->type);
                }

                void visit(const FloatImm *op) override {
                    leaves++;
                    check_type(op->type);
                }

                void visit(const Variable *op) override {
                    leaves++;
                    check_type(op->type);
                }
                void visit(const Call *op) override {
                    IRVisitor::visit(op);
                    calls[op->name]++;
                    // There's a bunch of implied math in the
                    // addressing if it's a Halide or Image call, and
                    // in the actual function call if it's not.
                    leaves += op->args.size();
                    if (op->is_intrinsic(Call::likely) || op->is_intrinsic(Call::likely_if_innermost)) {
                        likely = true;
                    }
                    if (op->call_type == Call::PureExtern) {
                        // Assume it's an expensive floating point intrinsic like pow or sin
                        leaves += 100;
                    }
                    check_type(op->type);
                }

                bool visit_likely_pair(Expr a, Expr b) {
                    bool old_likely = likely;
                    int old_leaves = leaves;
                    likely = false;
                    leaves = 0;
                    a.accept(this);
                    int a_leaves = leaves;
                    int a_likely = likely;
                    likely = false;
                    leaves = 0;
                    b.accept(this);
                    int b_leaves = leaves;
                    int b_likely = likely;
                    if (a_likely) {
                        leaves = old_leaves + a_leaves;
                    } else if (b_likely) {
                        leaves = old_leaves + b_leaves;
                    } else {
                        leaves = old_leaves + a_leaves + b_leaves;
                    }
                    likely = old_likely;
                    return a_likely || b_likely;
                }

                void visit(const Select *op) override {
                    if (visit_likely_pair(op->true_value, op->false_value)) {
                        op->condition.accept(this);
                    }
                }

                void visit(const Min *op) override {
                    visit_likely_pair(op->a, op->b);
                }
                void visit(const Max *op) override {
                    visit_likely_pair(op->a, op->b);
                }
                void visit(const Cast *op) override {
                    IRVisitor::visit(op);
                    check_type(op->type);
                }
                void check_type(Type t) {
                    if (!narrowest_type.bits() ||
                        t.bits() < narrowest_type.bits()) {
                        narrowest_type = t;
                    }
                }
            public:
                int leaves = 0;
                Type narrowest_type;
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
            // Assume things vectorize OK, so bill more for wider types that have lower vector throughput
            node.compute *= bytes_per_element;
            node.compute_if_inlined *= bytes_per_element;
            node.memory = bytes_per_element;
            node.vector_size = target.natural_vector_size(counter.narrowest_type);

            // Set parameter estimates (we could also do this in compute_bounds_and_costs)
            class ApplyParamEstimates : public IRMutator {
                using IRMutator::visit;

                void visit(const Variable *op) override {
                    if (op->param.defined()) {
                        if (!op->param.is_buffer()) {
                            expr = op->param.estimate();
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

vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, bool allow_splits, int vector_size) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        auto v = generate_tilings(s, d - 1, allow_splits, vector_size);
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
            t.push_back(0);
            if (!allow_splits) {
                if (!is_one) {
                    t.back() = 1;
                    result.push_back(t);
                }
                if (s[d] != 1 && !is_full && is_one) {
                    t.back() = s[d];
                    result.push_back(t);
                }
            } else {
                for (int outer = 1; outer <= s[d]; outer *= 2) {
                    int inner = (s[d] + outer - 1) / outer;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    if (outer > inner || (d == 0 && inner < vector_size)) break;
                    t.back() = outer;
                    result.push_back(t);
                }
                for (int inner = 1; inner < s[d]; inner *= 2) {
                    int outer = (s[d] + inner - 1) / inner;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    if (inner >= outer) break;
                    t.back() = outer;
                    result.push_back(t);
                }
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

    // Are we permitted to tile this loop?
    bool tileable = false;

    // The extents of the loops
    vector<int64_t> size;

    // The nodes inside the loop body
    vector<std::shared_ptr<PartialScheduleNode>> children;

    // Funcs inlined into this inner loop, and the number of times they are called. Only valid if children is empty.
    map<Function, int64_t, Function::Compare> inlined;

    // Funcs realized inside this inner loop
    set<Function, Function::Compare> store_at;

    double cost(const FunctionDAG &dag,
                const MachineParams &params,
                map<Function, const PartialScheduleNode *, Function::Compare> &compute_site,
                map<Function, double, Function::Compare> &overcompute,
                int64_t instances,
                const PartialScheduleNode *parent,
                map<const FunctionDAG::Node *, double> *node_costs = nullptr,
                map<const FunctionDAG::Edge *, double> *edge_costs = nullptr,
                map<Function, double, Function::Compare> *inlined_costs = nullptr) {

        if (!is_root() && !compute_site.count(func)) {
            compute_site[func] = parent;
        }

        double result = 0;

        int64_t subinstances = instances;
        for (auto i : size) {
            subinstances *= i;
        }
        if (innermost) {
            if (size.empty()) {
                overcompute[func] = 1;
            } else {
                int vector_size = dag.node_map.at(func)->vector_size;
                int64_t ideal_subinstances = subinstances;
                subinstances /= size[0];
                subinstances *= ((size[0] + vector_size - 1) / vector_size) * vector_size;

                // Record overcompute due to vectorization
                double factor = double(subinstances) / ideal_subinstances;
                // Add some generic loop overhead for the operations at
                // the boundary of the inner loop, and for the pipeline
                // stall.  TODO: Expose the constant
                factor *= (size[0] + 100) / size[0];

                overcompute[func] = factor;
            }
        }

        for (auto c : children) {
            result += c->cost(dag, params, compute_site, overcompute, subinstances, this, node_costs, edge_costs, inlined_costs);
        }

        // Bill compute and memory costs for all Funcs realized within this loop
        for (Function f : store_at) {
            double points = 1;
            const auto &bounds_realized = get_bounds(f, dag);
            for (auto p : bounds_realized.region) {
                points *= p.second - p.first + 1;
            }
            const auto *node = dag.node_map.at(f);
            double compute_cost = node->compute * points * subinstances;

            // Most recompute occurs due to there being multiple
            // overlapping realizations of a Func. However, we
            // must also account for recompute within a single
            // realization due to vectorization of the innermost
            // loop. Assume all other potential recompute is
            // avoided by sliding.
            compute_cost *= overcompute[f];

            if (node_costs) {
                // TODO: Should this include inlined Funcs?
                (*node_costs)[node] = compute_cost;
            }

            result += compute_cost;

            // Compute a locality discount due to assumed storage folding.
            auto it = compute_site.find(f);
            internal_assert(it != compute_site.end());

            double discount = 1;
            if (it->second != this) {
                const auto &bounds_computed = it->second->get_bounds(f, dag);
                discount = 1.01;
                // > 1 to account for storage folding overhead. Only do it if it provides a benefit.
                // TODO: Make this another tunable param?
                for (size_t i = bounds_realized.region.size(); i > 0; i--) {
                    auto r = bounds_realized.region[i-1];
                    auto c = bounds_computed.region[i-1];
                    int64_t er = r.second - r.first + 1;
                    int64_t ec = c.second - c.first + 1;
                    if (ec == er) {
                        continue;
                    }
                    discount = double(ec) / er;
                    if (i == 1) {
                        // By folding on the innermost dimension, you just broke vectorization. Don't do that.
                        discount = 1e10;
                    }
                    break;
                }
                if (node_costs) {
                    debug(0) << "Folding discount for " << f.name() << ": " << discount << "\n";
                }
            }

            // The memory cost is the number of cold loads times (in bytes) times the
            // cost per cold load. The discount reduces the cost per
            // cold load, but not the number of cold loads.
            double bytes_per_point = node->memory;
            double allocation_size = bytes_per_point * points * discount;
            double bytes_cold_loaded = bytes_per_point * points;
            double mem_cost = subinstances * bytes_cold_loaded * cost_of_cold_load(allocation_size, params);

            for (const auto *e : dag.outgoing_edges.at(f)) {
                if (edge_costs) {
                    (*edge_costs)[e] = mem_cost;
                }
                result += mem_cost;
            }
        }

        // Bill compute cost for all Funcs inlined in this loop
        for (auto p : inlined) {
            double c = dag.node_map.at(p.first)->compute_if_inlined * subinstances * p.second;
            // debug(0) << "Inlined Func " << p.first.name() << " has compute cost " << c << "\n";
            result += c;
            if (inlined_costs) {
                (*inlined_costs)[p.first] += c;
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
        // The number of points in that box
        int64_t region_points;
        // The minimum possible number of points evaluated. May be
        // less than region_points if inlining is a good idea (e.g. a
        // sparsely-sampled Func with large bounds)
        int64_t min_points;
        // The minimum possible compute cost
        double min_cost;
    };

    // The total bounds required of the given Func for one representative iteration of this loop. Computed lazily and cached.
    mutable map<Function, Bound, Function::Compare> bounds;
    const Bound &get_bounds(Function f, const FunctionDAG &dag) const {
        // debug(0) << "get_bounds of " << f.name() << " in loop over " << (is_root() ? "root" : func.name()) << "\n";
        auto it = bounds.find(f);
        if (it != bounds.end()) {
            return it->second;
        }
        Bound bound;
        if (dag.outgoing_edges.at(f).empty() && is_root()) {
            // Use the bounds estimate
            bound.region_points = 1;
            map<string, pair<int64_t, int64_t>> estimates;
            for (auto b : f.schedule().estimates()) {
                int64_t i_min = *as_const_int(b.min);
                int64_t i_extent = *as_const_int(b.extent);
                estimates[b.var] = {i_min, i_min + i_extent - 1};
                bound.region_points *= i_extent;
            }
            // Set the bounds using the estimates
            for (int i = 0; i < f.dimensions(); i++) {
                auto it = estimates.find(f.args()[i]);
                user_assert(it != estimates.end())
                    << "Need an estimate on dimension " << i << " of \"" << f.name() << "\"";
                bound.region.push_back(it->second);
            }
            bound.min_cost = bound.region_points * dag.node_map.at(f)->compute;
            bound.min_points = bound.region_points;
        } else {
            internal_assert(!dag.outgoing_edges.at(f).empty())
                << "No consumers of " << f.name()
                << " at loop over " << (is_root() ? "root" : func.name()) << "\n";
            int64_t calls_if_inlined = 0;
            for (const auto *e : dag.outgoing_edges.at(f)) {
                if (!bounds.count(e->consumer) && !calls(e->consumer, dag)) {
                    // debug(0) << "Skipping over " << e->consumer.name() << "\n";
                    continue;
                }
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
                        bound.region[i].second = std::max(bound.region[i].second, *imax);
                    }
                }
            }
            internal_assert(bound.region.size() == (size_t)f.dimensions()) << is_root() << " " << f.name() << "\n";
            int64_t points_if_realized = 1;
            for (int i = 0; i < f.dimensions(); i++) {
                points_if_realized *= (bound.region[i].second - bound.region[i].first + 1);
            }
            bound.region_points = points_if_realized;
            bound.min_points = std::min(points_if_realized, calls_if_inlined);
            const auto *n = dag.node_map.at(f);
            bound.min_cost = std::min(points_if_realized * n->compute, calls_if_inlined * n->compute_if_inlined);
        }
        bounds[f] = std::move(bound);
        return bounds[f];
    }

    void dump(string prefix) const {
        if (!is_root()) {
            debug(0) << prefix << func.name();
            prefix += " ";
        }
        for (auto s : size) {
            debug(0) << " " << s;
        }
        if (tileable) {
            debug(0) << " t";
        }
        if (innermost) {
            debug(0) << " *\n";
        } else {
            debug(0) << "\n";
        }
        for (auto p : store_at) {
            debug(0) << prefix << "realize: " << p.name() << "\n";
        }
        for (size_t i = children.size(); i > 0; i--) {
            children[i-1]->dump(prefix);
        }
        for (auto p : inlined) {
            debug(0) << prefix << "inlined: " << p.first.name() << " " << p.second << "\n";
        }
        /*
        for (auto p : bounds) {
            debug(0) << prefix << "bounds: " << p.first.name();
            for (auto d : p.second.region) {
                debug(0) << " [" << d.first << ", " << d.second << "]";
            }
            debug(0) << "\n";
        }
        */
    }

    int64_t calls_per_instance(Function f, const FunctionDAG &dag) const {
        int64_t result = 0;
        for (const auto &c : children) {
            result += c->calls(f, dag);
        }
        for (const auto *e : dag.outgoing_edges.at(f)) {
            if (e->consumer.same_as(func)) {
                result += e->calls;
            }
            auto it = inlined.find(e->consumer);
            if (it != inlined.end()) {
                result += e->calls * it->second;
            }
        }
        return result;
    }

    int64_t calls(Function f, const FunctionDAG &dag) const {
        int result = calls_per_instance(f, dag);
        for (auto s : size) {
            result *= s;
        }
        return result;
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
        node->tileable = true;
        Bound single_point;
        single_point.min_points = 1;
        single_point.region_points = 1;
        single_point.min_cost = dag.node_map.at(f)->compute;
        for (int i = 0; i < f.dimensions(); i++) {
            // Initialize the loop nest to cover the desired bounds
            node->size.push_back(bounds.region[i].second - bounds.region[i].first + 1);
            single_point.region.push_back({bounds.region[i].first, bounds.region[i].first});
        }
        node->bounds[f] = single_point;
        children.emplace_back(std::move(node));
    }

    // Return all possible ways to compute f in tiles.
    vector<PartialScheduleNode> compute_in_tiles(Function f, const FunctionDAG &dag,
                                                 const PartialScheduleNode *parent,
                                                 const MachineParams &params,
                                                 bool in_realization) const {
        vector<PartialScheduleNode> result;

        // Is it worth descending into this loop? If the bounds don't shrink, it's pointless
        if (parent) {
            int64_t parent_points = parent->get_bounds(f, dag).region_points;
            int64_t in_loop_points = get_bounds(f, dag).region_points;
            if (parent_points <= in_loop_points) {
                return result;
            }
        }

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
            if (!in_realization) {
                r.store_at.insert(f);
            }
            result.emplace_back(std::move(r));
        }

        if (dag.outgoing_edges.at(f).empty()) {
            // Can't tile outputs
            return result;
        }

        int vector_size = dag.node_map.at(f)->vector_size;

        if (tileable) {
            // Generate a list of tile sizes to try
            auto tilings = generate_tilings(size, (int)(size.size() - 1), !in_realization, vector_size);

            for (auto t : tilings) {

                if (parent->is_root()) {
                    // Skip root-level tilings that provide insufficient parallelism to avoid nested parallelism
                    int total = 1;
                    for (auto s : t) {
                        total *= s;
                    }
                    if (total < params.parallelism) continue;
                }

                // Tile this loop and place the computation at some coarser granularity
                PartialScheduleNode outer = *this;

                // First make an inner loop representing a 1x1x1... tile
                auto inner = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode);
                inner->size.resize(outer.size.size(), 1);
                inner->func = func;
                inner->innermost = innermost;
                inner->tileable = tileable;

                // Move the existing children and their bounds to the inner loop
                std::swap(inner->children, outer.children);
                std::swap(inner->inlined, outer.inlined);
                std::swap(inner->bounds, outer.bounds);
                std::swap(inner->store_at, outer.store_at);

                outer.bounds[func] = inner->bounds[func];
                outer.innermost = false;

                // Then move factors from the outer loop to the inner loop

                auto parent_bounds = parent->get_bounds(func, dag);
                auto &b = outer.bounds[func];
                b.region_points = 1;
                for (size_t i = 0; i < t.size(); i++) {
                    int factor = t[i];
                    inner->size[i] = (outer.size[i] + factor - 1) / factor;
                    outer.size[i] = factor;
                    int64_t min = parent_bounds.region[i].first;
                    int64_t extent = parent_bounds.region[i].second - min + 1;
                    extent = (extent + factor - 1) / factor;
                    b.region[i] = {min, min + extent - 1};
                    b.region_points *= extent;
                    // TODO: min_points, min_compute?
                }

                outer.children.push_back(inner);

                // Site the computation inside the outer loop
                PartialScheduleNode compute_at_here = outer;
                compute_at_here.compute_here(f, dag);
                if (!in_realization) {
                    compute_at_here.store_at.insert(f);
                }
                result.emplace_back(std::move(compute_at_here));

                if (!in_realization) {
                    // Also consider just storing here, but computing
                    // further in. Currently don't have to worry about
                    // the constraints this places on parallelism, as
                    // we forced all the parallelism to the outer
                    // loop.
                    PartialScheduleNode store_at_here = std::move(outer);
                    store_at_here.store_at.insert(f);
                    auto v = inner->compute_in_tiles(f, dag, &store_at_here, params, true);
                    for (PartialScheduleNode n : v) {
                        // Once we're sliding a function over a loop,
                        // it's best not to tile it again, or Halide's
                        // analysis gets confused.
                        n.tileable = false;
                        store_at_here.children.pop_back();
                        store_at_here.children.emplace_back(new PartialScheduleNode(std::move(n)));
                        result.push_back(store_at_here);
                    }
                }
            }
        }

        if (child >= 0 && !called_by_multiple_children) {
            for (int store_here = 0; store_here < 2; store_here++) {
                if (store_here && (in_realization || is_root())) {
                    // is_root: We place all our parallel loops at the
                    // root level, so this would constrain
                    // parallelism.
                    // in_realization: We've already set the storage
                    // level to be further out.
                    continue;
                }
                auto v = children[child]->compute_in_tiles(f, dag, this, params, store_here);
                for (PartialScheduleNode n : v) {
                    // (Only valid if one child calls f) Push the
                    // computation into the child. Possibly leaving
                    // the storage out here.
                    PartialScheduleNode r = *this;
                    if (store_here) {
                        r.store_at.insert(f);
                    }
                    r.children[child] = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode(n));
                    result.emplace_back(std::move(r));
                }
            }
        }

        if (result.empty()) {
            // Place the computation inside this loop
            PartialScheduleNode r = *this;
            r.compute_here(f, dag);
            if (!in_realization) {
                r.store_at.insert(f);
            }
            result.emplace_back(std::move(r));
        }

        return result;
    }

    struct FuncVars {
        double num_cores = 0; // How much parallelism do we need to exploit with this Func?
        struct FuncVar {
            VarOrRVar var;
            int64_t extent = 0;
            bool outermost = false, parallel = false, exists = false;
            FuncVar() : var(Var()) {}
        };
        vector<FuncVar> vars; // In order from innermost to outermost. Each group of d is one tiling.
    };

    void apply(LoopLevel here, const FunctionDAG &dag,
               map<Function, FuncVars, Function::Compare> &vars_map,
               double num_cores,
               const PartialScheduleNode *parent) {
        if (is_root()) {
            for (auto &c : children) {
                Func(c->func).compute_root();
                c->apply(LoopLevel::root(), dag, vars_map, num_cores, this);
            }
        } else {
            auto it = vars_map.find(func);
            if (it == vars_map.end()) {
                const auto &parent_bounds = parent->get_bounds(func, dag);
                FuncVars vars;
                vars.num_cores = num_cores;
                for (int i = 0; i < func.dimensions(); i++) {
                    FuncVars::FuncVar fv;
                    fv.var = Var(func.args()[i]);
                    fv.extent = parent_bounds.region[i].second - parent_bounds.region[i].first + 1;
                    fv.outermost = true;
                    fv.parallel = false;
                    fv.exists = true;
                    vars.vars.push_back(fv);
                }
                vars_map[func] = vars;
            }
            auto &vars = vars_map[func];

            if (!size.empty()) {
                if (innermost) {
                    FuncVars::FuncVar v;
                    for (int i = 0; i < func.dimensions(); i++) {
                        if (vars.vars[i].exists) {
                            v = vars.vars[i];
                            break;
                        }
                    }
                    here = LoopLevel(func, v.var);
                    int vector_size = dag.node_map.at(func)->vector_size;
                    if (v.extent >= 2 * vector_size && (((v.extent + vector_size - 1) / vector_size) & 1) == 0) {
                        Func(func).vectorize(v.var, 2 * vector_size);
                    } else if (v.extent >= vector_size) {
                        Func(func).vectorize(v.var, vector_size);
                    } else if (v.extent >= 16) {
                        Func(func).vectorize(v.var, 16);
                    } else if (v.extent >= 8) {
                        Func(func).vectorize(v.var, 8);
                    } else if (v.extent >= 4) {
                        Func(func).vectorize(v.var, 4);
                    }
                } else {
                    // Do the implied splits
                    vector<FuncVars::FuncVar> new_inner;
                    for (int i = 0; i < func.dimensions(); i++) {
                        FuncVars::FuncVar v;
                        FuncVars::FuncVar &parent = vars.vars[i];
                        int64_t factor = (parent.extent + size[i] - 1) / size[i];
                        if (!parent.exists || parent.extent == 1 || factor == 1) {
                            v.exists = false;
                            v.extent = 1;
                        } else if (size[i] == 1) {
                            // Not split in this dimension
                            v = parent;
                            parent.exists = false;
                            parent.extent = 1;
                        } else {
                            Var outer(parent.var.name() + "o"), inner(parent.var.name() + "i");
                            if (parent.extent % factor == 0) {
                                // TODO: If the actual size doesn't match the estimates, this could make some bad assumptions.
                                Func(func).split(parent.var, outer, inner, (int)factor, TailStrategy::RoundUp);
                            } else {
                                Func(func).split(parent.var, outer, inner, (int)factor, TailStrategy::GuardWithIf);
                            }
                            v = parent;
                            parent.var = outer;
                            parent.extent = size[i];
                            v.var = inner;
                            v.extent = factor;
                        }
                        new_inner.push_back(v);
                    }
                    for (int i = 0; i < func.dimensions(); i++) {
                        if (!vars.vars[i].exists) continue;
                        here = LoopLevel(func, vars.vars[i].var);
                        break;
                    }
                    vars.vars.insert(vars.vars.begin(), new_inner.begin(), new_inner.end());
                }
            }
            for (auto f : store_at) {
                Func(f).store_at(here);
            }
            for (auto s : size) {
                num_cores /= s;
            }
            for (auto &c : children) {
                if (!c->func.same_as(func)) {
                    Func(c->func).compute_at(here);
                }
                c->apply(here, dag, vars_map, num_cores, this);
            }
        }
    }

};

struct State {
    PartialScheduleNode root;

    double cost = 0;

    int num_funcs_scheduled = 0;

    void calculate_cost(const FunctionDAG &dag, const MachineParams &params) {
        map<Function, const PartialScheduleNode *, Function::Compare> compute_site;
        map<Function, double, Function::Compare> overcompute;
        cost = root.cost(dag, params, compute_site, overcompute, 1, nullptr);

        /*
        debug(0) << "Calculating cost for: \n";
        dump();
        debug(0) << "Total cost: " << cost << "\n";
        */

        // Subtract the essential compute cost of the funcs scheduled so far.
        for (int i = 0; i < num_funcs_scheduled; i++) {
            const FunctionDAG::Node &n = dag.nodes[i];
            double c = root.get_bounds(n.func, dag).min_cost;
            //debug(0) << "Func " << n.func.name() << " has minimum cost " << c << "\n";
            cost -= c;
        }

        // debug(0) << "Redundant cost: " << cost << "\n";
    }

    void generate_children(const FunctionDAG &dag,
                           const MachineParams &params,
                           std::function<void(State *)> &accept_child) {
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

        // 1) Inline it
        if (!dag.outgoing_edges.at(f).empty()) {
            auto child = new State(*this);
            child->root = child->root.inline_func(f, dag);
            child->num_funcs_scheduled++;
            child->calculate_cost(dag, params);
            internal_assert(child->root.computes(f)) << "Failed to inline " << f.name() << "\n";
            accept_child(child);
        }

        // 2) Realize it somewhere
        auto tile_options = root.compute_in_tiles(f, dag, nullptr, params, false);
        for (PartialScheduleNode n : tile_options) {
            auto child = new State(*this);
            child->root = std::move(n);
            child->num_funcs_scheduled++;
            child->calculate_cost(dag, params);
            internal_assert(child->root.computes(f)) << "Failed to inject realization of " << f.name() << "\n";
            accept_child(child);
        }
    }

    void dump() const {
        debug(0) << "State with cost " << cost << ":\n";
        root.dump("");
    }

    void apply_schedule(const FunctionDAG &dag, const MachineParams &params) {
        map<Function, PartialScheduleNode::FuncVars, Function::Compare> vars_map;
        root.apply(LoopLevel::root(), dag, vars_map, params.parallelism, nullptr);

        for (auto &p : vars_map) {
            Func f(p.first);
            // Do all the reorders
            vector<VarOrRVar> vars;
            for (auto &v : p.second.vars) {
                if (v.exists) vars.push_back(v.var);
            }
            f.reorder(vars);

            // Figure out which dimensions are parallel and fuse them
            // into a single parallel outer loop (TODO: What if
            // something is compute_at in between two parallel vars?
            // Can't currently happen because we enforce adequately
            // large outer loops).
            double num_cores = p.second.num_cores;
            VarOrRVar fused {Var()};
            bool any_parallel = false;
            for (int i = (int)p.second.vars.size() - 1; i >= 0 && num_cores > 1; i--) {
                auto &v = p.second.vars[i];
                if (!v.exists) continue;
                int64_t extent = v.extent;
                num_cores /= extent;
                if (num_cores < 0.125) {
                    // Should probably do another split and only mark the outer one as parallel
                    int task_size = std::floor(1 / num_cores);
                    debug(0) << "Task size for " << f.name() << ": " << task_size << "\n";
                    f.parallel(v.var, task_size);
                } else {
                    f.parallel(v.var);
                }
                if (!any_parallel) {
                    fused = v.var;
                    any_parallel = true;
                } else if (i > 1) {
                    // Use the inner name, to not invalidate any compute_ats
                    f.fuse(v.var, fused, v.var); // To consider: fuse may break loop partitioning. Check for likelies
                    fused = v.var;
                }
            }
        }
    }

    void print_predicted_runtimes(const FunctionDAG &dag, const MachineParams &params) {
        std::map<Function, double, Function::Compare> inlined_costs;
        std::map<const FunctionDAG::Node *, double> node_costs;
        std::map<const FunctionDAG::Edge *, double> edge_costs;
        std::map<Function, const PartialScheduleNode *, Function::Compare> compute_site;
        std::map<Function, double, Function::Compare> overcompute;
        root.cost(dag, params, compute_site, overcompute, 1, nullptr, &node_costs, &edge_costs, &inlined_costs);

        for (size_t i = dag.nodes.size(); i > 0; i--) {
            Function f = dag.nodes[i-1].func;
            if (inlined_costs.count(f)) {
                double c = 0;
                for (const auto *e1 : dag.incoming_edges.at(f)) {
                    c += edge_costs[e1];
                }
                for (const auto *e2 : dag.outgoing_edges.at(f)) {
                    edge_costs[e2] += c;
                }
            }
        }

        for (size_t i = dag.nodes.size(); i > 0; i--) {
            const FunctionDAG::Node *n = &dag.nodes[i-1];
            auto it = node_costs.find(n);
            double compute_cost = 0, mem_cost = 0;
            if (it != node_costs.end()) {
                compute_cost = it->second;
                for (const auto *e : dag.incoming_edges.at(n->func)) {
                    mem_cost += edge_costs[e];
                }
            } else {
                debug(0) << "Func " << n->func.name() << " is inlined\n";
                compute_cost = inlined_costs[n->func];
            }
            debug(0) << "Func " << n->func.name() << " has costs: "
                     << (compute_cost + mem_cost) << " = "
                     << compute_cost << " + " << mem_cost << "\n";
        }

        /*
        // Print it in a form that can be copy-pasted elsewhere
        debug(0) << "nodes = [";
        for (const auto &n : dag.nodes) {
            // double essential = root.get_bounds(n.func, dag).min_cost;
            auto it = node_costs.find(&n);
            if (it != node_costs.end()) {
                debug(0) << " " << it->second;
            } else {
                debug(0) << " " << inlined_costs[n.func];
            }
        }
        debug(0) << "]\n";

        debug(0) << "edges = [";
        for (const auto &n1 : dag.nodes) {
            debug(0) << "[";
            for (const auto &n2 : dag.nodes) {
                const FunctionDAG::Edge *edge = nullptr;
                for (const auto *e : dag.outgoing_edges.at(n1.func)) {
                    if (e->consumer.same_as(n2.func)) {
                        edge = e;
                    }
                }
                if (edge) {
                    debug(0) << " " << edge_costs[edge];
                } else {
                    debug(0) << " 0";
                }
            }
            debug(0) << "]\n";
        }
        debug(0) << "]\n";
        */


        for (const auto &n : dag.nodes) {
            auto it = node_costs.find(&n);
            if (it != node_costs.end()) {
                debug(0) << "XXXN " << it->second << "\n";
            } else {
                debug(0) << "XXXI " << inlined_costs[n.func] << "\n";
            }
        }
        for (const auto &e : dag.edges) {
            debug(0) << "XXXE " << edge_costs[&e] << "\n";
        }

    }
};


struct CompareStates {
    bool operator()(const std::shared_ptr<State> &a, const std::shared_ptr<State> &b) const {
        return a->cost > b->cost;
    }
};

State optimal_schedule(FunctionDAG &dag,
                       vector<Function> outputs,
                       const MachineParams &params,
                       int beam_size) {
    std::priority_queue<std::shared_ptr<State>,
                        std::vector<std::shared_ptr<State>>,
                        CompareStates> q;

    q.emplace(new State);

    // A progress bar.
    uint32_t counter = 0;
    auto tick = [&](double progress) {
        counter++;
        if (counter & 1023) return;
        progress *= 78;
        debug(0) << '[';
        for (int j = 0; j < 78; j++) {
            if (j < progress) {
                debug(0) << '.';
            } else if (j - 1 < progress) {
                debug(0) << "/-\\|"[(counter >> 10) % 4];
            } else {
                debug(0) << ' ';
            }
        }
        debug(0) << ']';
        for (int j = 0; j < 80; j++) {
            debug(0) << '\b';
        }
    };

    std::function<void(State *)> enqueue_new_children = [&](State *s) {
        // debug(0) << "\n** Generated child: ";
        // s->dump();
        // s->print_predicted_runtimes(dag, params);

        tick(double(s->num_funcs_scheduled) / dag.nodes.size());
        q.emplace(std::shared_ptr<State>(s));
    };

    for (int i = 0; ; i++) {

        if (q.size() > (size_t)beam_size) {
            decltype(q) trimmed;
            while (trimmed.size() < (size_t)beam_size && !q.empty()) {
                if ((q.size() == 1 && trimmed.empty()) || !random_dropout()) {
                    trimmed.push(q.top());
                }
                q.pop();
            }
            q.swap(trimmed);
        }

        decltype(q) pending;
        q.swap(pending);
        while (!pending.empty()) {
            auto state = pending.top();
            pending.pop();

            /*
              if (true || i % 1000 == 0) {
              debug(0) << "** Queue top: ";
              state->dump();
              }
            */

            if (state->num_funcs_scheduled == (int)dag.nodes.size()) {
                debug(0) << '\n';
                return *state;
            }

            state->generate_children(dag, params, enqueue_new_children);
        }
    }
}

}

std::string generate_schedules_new(const std::vector<Function> &outputs,
                                   const Target &target,
                                   const MachineParams &params) {

    string seed_str = get_env_variable("HL_SEED");
    int seed = (int)time(NULL);
    if (!seed_str.empty()) {
        seed = atoi(seed_str.c_str());
    }
    debug(0) << "Dropout seed = " << seed << "\n";
    srand(seed);

    string beam_size_str = get_env_variable("HL_BEAM_SIZE");
    size_t beam_size = 20;
    if (!beam_size_str.empty()) {
        beam_size = atoi(beam_size_str.c_str());
    }

    string time_limit_str = get_env_variable("HL_AUTO_SCHEDULE_TIME_LIMIT");
    double time_limit = 0;
    if (!time_limit_str.empty()) {
        time_limit = atof(time_limit_str.c_str());
    }

    FunctionDAG dag(outputs, params, target);

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

    debug(0) << "** Optimal schedule:\n";
    optimal.dump();

    // Just to get the debugging prints to fire
    optimal.calculate_cost(dag, params);

    // Apply the schedules
    optimal.apply_schedule(dag, params);

    // Print out the predicted runtime of each Func, so we can compare them to a profile
    optimal.print_predicted_runtimes(dag, params);


    return "";
}

void autoschedule_test() {
    MachineParams params(16, 16 * 1024 * 1024, 40);
    size_t beam_size = 1;
    // Use a fixed target for the analysis to get consistent results from this test.
    Target target("x86-64-linux-sse41-avx-avx2");

    Var x("x"), y("y");

    {
        // In a point-wise pipeline, everything should be fully fused.
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);
        g(x, y) = f(x, y) * 2 + 1;
        h(x, y) = g(x, y) * 2 + 1;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";

        optimal.apply_schedule(dag, params);
        h.realize(1000, 1000);

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
        FunctionDAG dag(outputs, cheap_memory, target);
        State optimal = optimal_schedule(dag, outputs, cheap_memory, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";

        optimal.apply_schedule(dag, params);
        h.realize(1000, 1000);
    }

    {
        // In a pipeline with moderate isotropic stencils, there should be some square tiling
        Func f("f"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        h(x, y) = (f(x-9, y-9) + f(x, y-9) + f(x+9, y-9) +
                   f(x-9, y  ) + f(x, y  ) + f(x+9, y  ) +
                   f(x-9, y+9) + f(x, y+9) + f(x+9, y-9));


        h.estimate(x, 0, 2048).estimate(y, 0, 2048);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";

        optimal.apply_schedule(dag, params);
        h.realize(2048, 2048);
    }

    // Smaller footprint stencil -> smaller tiles
    {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        h(x, y) = (f(x-1, y-1) + f(x, y-1) + f(x+1, y-1) +
                   f(x-1, y  ) + f(x, y  ) + f(x+1, y  ) +
                   f(x-1, y+1) + f(x, y+1) + f(x+1, y-1));

        h.estimate(x, 0, 2048).estimate(y, 0, 2048);

        vector<Function> outputs = {h.function()};
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";

        optimal.apply_schedule(dag, params);
        h.realize(2048, 2048);

        optimal.print_predicted_runtimes(dag, params);
    }

    // A stencil chain
    {
        const int N = 8;
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
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, 1);
        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";

        // optimal.apply_schedule(dag, params);
        // f[N-1].realize(2048, 2048);
    }

    // An outer product
    {
        Buffer<float> a(2048), b(2048);
        Func f;
        f(x, y) = a(x) * b(y);

        f.estimate(x, 0, 2048).estimate(y, 0, 2048);

        vector<Function> outputs = {f.function()};
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";
    }

    // A separable downsample that models the start of local_laplacian
    {
        Buffer<float> in(2048, 2048);
        Var k;
        Func orig("orig"), expensive("expensive"), downy("downy"), downx("downx");
        Expr e = 0;
        for (int i = 0; i < 100; i++) {
            e += 1;
            e *= e;
        }
        orig(x, y) = e;
        expensive(x, y, k) = orig(x, y) * orig(x, y) + (x + orig(x, y)) * (1 + orig(x, y)) + sqrt(k + orig(x, y));
        downy(x, y, k) = expensive(x, 2*y - 1, k) + expensive(x, 2*y, k) + expensive(x, 2*y+1, k) + expensive(x, 2*y + 2, k);
        downx(x, y, k) = downy(2*x-1, y, k) + downy(2*x, y, k) + downy(2*x + 1, y, k) + downy(2*x + 2, y, k);
        downx.estimate(x, 1, 1022).estimate(y, 1, 1022).estimate(k, 0, 256);

        vector<Function> outputs = {downx.function()};
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << "\n";

        optimal.print_predicted_runtimes(dag, params);
    }
}

}
}
