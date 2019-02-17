#ifndef FUNCTION_DAG_H
#define FUNCTION_DAG_H

#include <stdint.h>
#include <algorithm>
#include <vector>
#include <map>
#include <string>

#include "Halide.h"
#include "Errors.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using std::pair;
using std::vector;
using std::map;
using std::string;
using std::unique_ptr;

// An optional rational type used when analyzing memory dependencies
struct OptionalRational {
    bool exists = false;
    int64_t numerator = 0, denominator = 0;

    OptionalRational() = default;
    OptionalRational(bool e, int64_t n, int64_t d) : exists(e), numerator(n), denominator(d) {}

    void operator+=(const OptionalRational &other) {
        if (!exists || !other.exists) {
            exists = false;
            return;
        }
        if (denominator == other.denominator) {
            numerator += other.numerator;
            return;
        }

        int64_t l = lcm(denominator, other.denominator);
        numerator *= l / denominator;
        denominator = l;
        numerator += other.numerator * (l / other.denominator);
        int64_t g = gcd(numerator, denominator);
        numerator /= g;
        denominator /= g;
    }

    OptionalRational operator*(const OptionalRational &other) const {
        if ((*this) == 0) return *this;
        if (other == 0) return other;
        int64_t num = numerator * other.numerator;
        int64_t den = denominator * other.denominator;
        bool e = exists && other.exists;
        return OptionalRational {e, num, den};
    }

    bool operator<(int x) const {
        if (!exists) return false;
        if (denominator > 0) {
            return numerator < x * denominator;
        } else {
            return numerator > x * denominator;
        }
    }

    bool operator<=(int x) const {
        if (!exists) return false;
        if (denominator > 0) {
            return numerator <= x * denominator;
        } else {
            return numerator >= x * denominator;
        }
    }

    bool operator>(int x) const {
        if (!exists) return false;
        return !((*this) <= x);
    }

    bool operator>=(int x) const {
        if (!exists) return false;
        return !((*this) < x);
    }

    bool operator==(int x) const {
        return exists && (numerator == (x * denominator));
    }

    bool operator==(const OptionalRational &other) const {
        return (exists == other.exists) && (numerator * other.denominator == denominator * other.numerator);
    }
};

// Records the derivative of the coordinate accessed in some
// producer w.r.t the loops of the consumer.
class LoadJacobian {
    vector<vector<OptionalRational>> coeffs;
    int64_t c;

public:
    LoadJacobian(vector<vector<OptionalRational>> &&matrix, int64_t c = 1) :
        coeffs(matrix), c(c) {}

    size_t producer_storage_dims() const {
        return coeffs.size();
    }

    size_t consumer_loop_dims() const {
        if (coeffs.empty() || coeffs[0].empty()) {
            // The producer is scalar, and we don't know how
            // many consumer loops there are.
            return 0;
        }
        return coeffs[0].size();
    }

    OptionalRational operator()(int producer_storage_dim, int consumer_loop_dim) const {
        if (coeffs.empty()) {
            // The producer is scalar, so all strides are zero.
            return {true, 0, 1};
        }
        return coeffs[producer_storage_dim][consumer_loop_dim];
    }

    // To avoid redundantly re-recording copies of the same
    // load Jacobian, we keep a count of how many times a
    // load with this Jacobian occurs.
    int64_t count() const {return c;}

    // Try to merge another LoadJacobian into this one, increasing the
    // count if the coefficients match.
    bool merge(const LoadJacobian &other) {
        if (other.coeffs.size() != coeffs.size()) return false;
        for (size_t i = 0; i < coeffs.size(); i++) {
            if (other.coeffs[i].size() != coeffs[i].size()) return false;
            for (size_t j = 0; j < coeffs[i].size(); j++) {
                if (!(other.coeffs[i][j] == coeffs[i][j])) return false;
            }
        }
        c += other.count();
        return true;
    }

    LoadJacobian operator*(const LoadJacobian &other) const {
        vector<vector<OptionalRational>> matrix;
        internal_assert(consumer_loop_dims() == 0 || (consumer_loop_dims() == other.producer_storage_dims()));
        matrix.resize(producer_storage_dims());
        for (size_t i = 0; i < producer_storage_dims(); i++) {
            matrix[i].resize(other.consumer_loop_dims());
            for (size_t j = 0; j < other.consumer_loop_dims(); j++) {
                matrix[i][j] = OptionalRational {true, 0, 1};
                for (size_t k = 0; k < consumer_loop_dims(); k++) {
                    matrix[i][j] += (*this)(i, k) * other(k, j);
                }
            }
        }
        LoadJacobian result(std::move(matrix), count() * other.count());
        return result;
    }

    void dump(const char *prefix) const {
        if (count() > 1) {
            debug(0) << prefix << count() << " x\n";
        }
        for (size_t i = 0; i < producer_storage_dims(); i++) {
            debug(0) << prefix << "  [";

            for (size_t j = 0; j < consumer_loop_dims(); j++) {
                const auto &c = (*this)(i, j);
                if (!c.exists) {
                    debug(0) << " _  ";
                } else if (c.denominator == 1) {
                    debug(0) << " " << c.numerator << "  ";
                } else {
                    debug(0) << c.numerator << "/" << c.denominator << " ";
                }
            }
            debug(0) << "]\n";
        }
        debug(0) << "\n";
    }
};

// A concrete set of bounds for a Func. These are created and
// destroyed very frequently while exploring scheduling options, so we
// have a custom allocator and memory pool. Much like IR nodes, we
// treat them as immutable once created and wrapped in a Bound object
// so that they can be shared safely across scheduling alternatives.

class Span {
    int64_t first, second; // TODO: rename to min/max
    bool constant_extent_;
public:
    int64_t min() const {return first;}
    int64_t max() const {return second;}
    int64_t extent() const {return second - first + 1;}
    bool constant_extent() const {return constant_extent_;}

    void union_with(const Span &other) {
        first = std::min(first, other.min());
        second = std::max(second, other.max());
        constant_extent_ = constant_extent_ && other.constant_extent();
    }

    void set_extent(int64_t e) {
        second = first + e - 1;
    }

    void translate(int64_t x) {
        first += x;
        second += x;
    }

    Span(int64_t a, int64_t b, bool c) : first(a), second(b), constant_extent_(c) {}
    Span() = default;
    Span(const Span &other) = default;
    static Span empty_span() {
        return Span(INT64_MAX, INT64_MIN, true);
    }
};

struct BoundContents;
using Bound = IntrusivePtr<const BoundContents>;
struct BoundContents {
    mutable RefCount ref_count;
    struct Layout;
    const Layout *layout = nullptr;

    Span *data() const {
        // This struct is a header
        return (Span *)(const_cast<BoundContents *>(this) + 1);
    }

    Span &region_required(int i) {
        return data()[i];
    }

    Span &region_computed(int i) {
        return data()[i + layout->computed_offset];
    }

    Span &loops(int i, int j) {
        return data()[j + layout->loop_offset[i]];
    }


    const Span &region_required(int i) const {
        return data()[i];
    }

    const Span &region_computed(int i) const {
        return data()[i + layout->computed_offset];
    }

    const Span &loops(int i, int j) const {
        return data()[j + layout->loop_offset[i]];
    }

    BoundContents *make_copy() const {
        auto b = layout->make();
        size_t bytes = sizeof(data()[0]) * layout->total_size;
        memcpy(b->data(), data(), bytes);
        return b;
    }

    void validate() const {
        /*
        for (int i = 0; i < layout->total_size; i++) {
            auto p = data()[i];
            if (p.second < p.first) {
                debug(0) << "Bad bounds object:\n";
                for (int j = 0; j < layout->total_size; j++) {
                    if (i == j) debug(0) << "=> ";
                    else debug(0) << "   ";
                    debug(0) << j << ": " << data()[j].first << ", " << data()[j].second << "\n";
                }
                internal_error << "Aborting";
            }
        }
        */
    }

    // We're frequently going to need to make these concrete bounds
    // arrays.  It makes things more efficient if we figure out the memory
    // layout of those data structures once ahead of time, and make each
    // individual instance just use that.
    struct Layout {
        // number of Span to allocate
        int total_size;

        // region_required has size func->dimensions() and comes first in the memory layout

        // region_computed comes next at the following index
        int computed_offset;

        // the loop for each stage starts at the following index
        std::vector<int> loop_offset;

        // A memory pool of free BoundContent objects with this layout
        mutable std::vector<BoundContents *> pool;

        // All the blocks of memory allocated
        mutable std::vector<void *> blocks;

        Layout() {}

        ~Layout() {
            for (auto b : blocks) {
                free(b);
            }
        }

        Layout(const Layout &) = delete;
        void operator=(const Layout &) = delete;
        Layout(Layout &&) = delete;
        void operator=(Layout &&) = delete;

        void allocate_some_more() const {
            size_t size_of_one = sizeof(BoundContents) + total_size * sizeof(Span);
            const size_t number_per_block = std::max((size_t)8, 4096 / size_of_one); // Make a page of them, or 8, whichever is larger.
            const size_t bytes_to_allocate = std::max(size_of_one * number_per_block, (size_t)4096);
            unsigned char *mem = (unsigned char *)malloc(bytes_to_allocate);

            // HACK
            /*
            // Mark the memory with something recognizable to make it easier to catch use of uninitialized memory
            for (size_t i = 0; i < bytes_to_allocate / 16; i++) {
                ((int64_t *)mem)[2*i] = 1234567;
                ((int64_t *)mem)[2*i + 1] = -1234567;
            }
            */

            blocks.push_back(mem);
            static_assert((sizeof(BoundContents) & 7) == 0, "BoundContents header is not aligned");
            for (size_t i = 0; i < number_per_block; i++) {
                BoundContents *b = (BoundContents *)(mem + i * size_of_one);
                new (b) BoundContents;
                b->layout = this;
                pool.push_back(b);
            }
            internal_assert(((unsigned char *)(pool[0]) + size_of_one) == (unsigned char *)(pool[1]));
        }

        // Make a BoundContents object with this layout
        BoundContents *make() const {
            if (pool.empty()) {
                allocate_some_more();
            }
            BoundContents *b = pool.back();
            pool.pop_back();
            // HACK: make use-of-uninitialized on a recycled block of memory easier to find.
            /*
            for (int i = 0; i < total_size; i++) {
                b->data()[i].first = 1010101;
                b->data()[i].second = -1010101;
            }
            */
            return b;
        }

        // Release a BoundContents object with this layout back to the pool
        void release(const BoundContents *b) const {
            internal_assert(b->layout == this) << "Releasing BoundContents onto the wrong pool!";
            pool.push_back(const_cast<BoundContents *>(b));
        }
    };
};

// A representation of the function DAG. The nodes and edges are both
// in reverse realization order, so if you want to walk backwards up
// the DAG, just iterate the nodes or edges in-order.
struct FunctionDAG {

    struct Edge;

    struct Node {
        // A pointer back to the owning DAG
        FunctionDAG *dag;

        Function func;

        double bytes_per_point;

        // The min/max variables used to denote a symbolic region of
        // this Func. Used in the cost above, and in the Edges below.
        vector<Interval> region_required;

        // A concrete region required from a bounds estimate. Only
        // defined for outputs.
        vector<Span> estimated_region_required;

        // The region computed of a Func, in terms of the region
        // required. For simple Funcs this is identical to the
        // region_required. However, in some Funcs computing one
        // output requires computing other outputs too. You can't
        // really ask for a single output pixel from something blurred
        // with an IIR without computing the others, for example.
        struct RegionComputedInfo {
            // The min and max in their full symbolic glory
            Interval in;

            // Analysis used to accelerate common cases
            bool equals_required = false, equals_union_of_required_with_constants = false;
            int64_t c_min = 0, c_max = 0;
        };
        vector<RegionComputedInfo> region_computed;
        bool region_computed_all_common_cases = false;

        // Expand a region required into a region computed, using the
        // symbolic intervals above.
        void required_to_computed(const Span *required,
                                  Span *computed) const {
            map<string, Expr> required_map;
            if (!region_computed_all_common_cases) {
                for (int i = 0; i < func.dimensions(); i++) {
                    required_map[region_required[i].min.as<Variable>()->name] = (int)required[i].min();
                    required_map[region_required[i].max.as<Variable>()->name] = (int)required[i].max();
                }
            }
            for (int i = 0; i < func.dimensions(); i++) {
                const auto &comp = region_computed[i];
                if (comp.equals_required) {
                    computed[i] = required[i];
                } else if (comp.equals_union_of_required_with_constants) {
                    computed[i] = Span(std::min(required[i].min(), comp.c_min),
                                       std::max(required[i].max(), comp.c_max),
                                       false);
                } else {
                    Expr min = simplify(substitute(required_map, comp.in.min));
                    Expr max = simplify(substitute(required_map, comp.in.max));
                    const int64_t *imin = as_const_int(min);
                    const int64_t *imax = as_const_int(max);
                    internal_assert(imin && imax) << min << ", " << max << '\n';
                    computed[i] = Span(*imin, *imax, false);
                }
            }
        }

        struct Loop {
            string var;
            bool pure, rvar;
            Expr min, max;

            // Which pure dimension does this loop correspond to? Invalid if it's an rvar
            int pure_dim;

            // Common case optimizations:

            // If true, the loop bounds are just the region computed in the given dimension
            bool equals_region_computed = false;
            int region_computed_dim = 0;

            // If true, the loop bounds are a constant with the given min and max
            bool bounds_are_constant = false;
            int64_t c_min = 0, c_max = 0;

            // A persistent fragment of source for getting this Var from its owner Func.
            string accessor;
        };

        // Get the loop nest shape as a function of the region computed
        void loop_nest_for_region(int stage_idx,
                                  const Span *computed,
                                  Span *loop) const {
            // debug(0) << "Loop nest for region func " << func.name() << " stage " << stage_idx << "\n";
            const auto &s = stages[stage_idx];
            map<string, Expr> computed_map;
            if (!s.loop_nest_all_common_cases) {
                for (int i = 0; i < func.dimensions(); i++) {
                    computed_map[region_required[i].min.as<Variable>()->name] = (int)computed[i].min();
                    computed_map[region_required[i].max.as<Variable>()->name] = (int)computed[i].max();
                }
            }

            for (size_t i = 0; i < s.loop.size(); i++) {
                const auto &l = s.loop[i];
                if (l.equals_region_computed) {
                    loop[i] = computed[l.region_computed_dim];
                } else if (l.bounds_are_constant) {
                    loop[i] = Span(l.c_min, l.c_max, true);
                } else {
                    Expr min = simplify(substitute(computed_map, l.min));
                    Expr max = simplify(substitute(computed_map, l.max));
                    const int64_t *imin = as_const_int(min);
                    const int64_t *imax = as_const_int(max);
                    internal_assert(imin && imax) << min << ", " << max << '\n';
                    loop[i] = Span(*imin, *imax, false);
                }
                // debug(0) << i << ": " << loop[i].first << " " << loop[i].second << "\n";
            }
        }

        // One stage of a Func
        struct Stage {
            // The owning Node
            Node *node;

            // Which stage of the Func is this. 0 = pure.
            int index;

            // The loop nest that computes this stage, from innermost out.
            vector<Loop> loop;
            bool loop_nest_all_common_cases = false;

            // The vectorization width that will be used for
            // compute. Corresponds to the natural width for the
            // narrowest type used.
            int vector_size;

            // The vector size used for storing outputs. Corresponds
            // to the natural width for the output type.
            int output_vector_size;

            // The featurization of the compute done
            PipelineFeatures features;

            // The actual Halide front-end stage object
            Halide::Stage stage;

            // The name for scheduling (e.g. "foo.update(3)")
            string name;

            // Ids for perfect hashing on stages.
            int id, max_id;

            vector<Edge *> incoming_edges;

            vector<bool> dependencies;
            bool downstream_of(const Node &n) const {
                return dependencies[n.id];
            };

            Stage(Halide::Stage s) : stage(s) {}
        };
        vector<Stage> stages;

        vector<Edge *> outgoing_edges;

        // Max vector size across the stages
        int vector_size;

        // A unique ID for this node, allocated consecutively starting
        // at zero for each pipeline.
        int id, max_id;

        // Just func->dimensions(), but we ask for it so many times
        // that's it's worth avoiding the function call into
        // libHalide.
        int dimensions;

        bool is_wrapper; // Is a single pointwise call to another Func

        bool is_output, is_input;

        bool is_pointwise; // Only uses pointwise calls

        bool is_boundary_condition; // Pointwise calls + clamping on all indices

        std::unique_ptr<BoundContents::Layout> bounds_memory_layout;

        BoundContents *make_bound() const {
            return bounds_memory_layout->make();
        }
    };

    struct Edge {
        struct BoundInfo {
            // The symbolic expression for the bound in this dimension
            Expr expr;

            // Fields below are the results of additional analysis
            // used to evaluate this bound more quickly.
            int64_t coeff, constant;
            int64_t consumer_dim;
            bool affine, uses_max;

            BoundInfo(const Expr &e, const Node::Stage &consumer) : expr(e) {
                // Do the analysis to detect if this is a simple case
                // that can be evaluated more cheaply. Currently this
                // acceleration recognises affine expressions. In the
                // future we may consider quasi-affine, or even
                // piecewise-quasi-affine.
                const Add *add = expr.as<Add>();
                const Mul *mul = add ? add->a.as<Mul>() : expr.as<Mul>();
                const IntImm *coeff_imm = mul ? mul->b.as<IntImm>() : nullptr;
                const IntImm *constant_imm = add ? add->b.as<IntImm>() : nullptr;
                Expr v = (mul ? mul->a :
                          add ? add->a :
                          expr);
                const Variable *var = v.as<Variable>();

                if (var && (!mul || coeff_imm) && (!add || constant_imm)) {
                    affine = true;
                    coeff = mul ? coeff_imm->value : 1;
                    constant = add ? constant_imm->value : 0;
                    consumer_dim = -1;
                    for (int i = 0; i < (int)consumer.loop.size(); i++) {
                        const auto &in = consumer.loop[i];
                        if (var->name == consumer.node->func.name() + "." + in.var + ".min") {
                            consumer_dim = i;
                            uses_max = false;
                            break;
                        } else if (var->name == consumer.node->func.name() + "." + in.var + ".max") {
                            consumer_dim = i;
                            uses_max = true;
                            break;
                        }
                    }
                    internal_assert(consumer_dim >= 0) << "Could not find consumer loop variable: " << var->name << "\n";
                    debug(2) << "Bound is affine: " << e << " == " << var->name << " * " << coeff << " + " << constant << "\n";
                } else {
                    affine = false;
                    debug(2) << "Bound is non-affine: " << e << "\n";
                }
            }
        };
        vector<pair<BoundInfo, BoundInfo>> bounds;

        FunctionDAG::Node *producer;
        FunctionDAG::Node::Stage *consumer;

        // The number of calls the consumer makes to the producer, per
        // point in the loop nest of the consumer.
        int calls;

        vector<LoadJacobian> load_jacobians;

        void add_load_jacobian(LoadJacobian j1) {
            for (auto &j2 : load_jacobians) {
                if (j2.merge(j1)) return;
            }
            load_jacobians.emplace_back(std::move(j1));
        }

        // Given a loop nest of the consumer stage, expand a region
        // required of the producer to be large enough to include all
        // points required.
        void expand_footprint(const Span *consumer_loop,
                              Span *producer_required) const {

            // Create a map from the symbolic loop variables to the actual loop size
            const auto &symbolic_loop = consumer->loop;
            map<string, Expr> s;
            if (!all_bounds_affine) {
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    auto p = consumer_loop[i];
                    const string &var = symbolic_loop[i].var;
                    s[consumer->node->func.name() + "." + var + ".min"] = (int)p.min();
                    s[consumer->node->func.name() + "." + var + ".max"] = (int)p.max();
                    // debug(0) << consumer->func.name() << " " << var << " " << p.first << " " << p.second << "\n";
                }
            }
            // Apply that map to the bounds relationship encoded
            // in the edge to expand the bounds of the producer to
            // satisfy the consumer
            for (int i = 0; i < producer->func.dimensions(); i++) {
                // Get bounds required of this dimension of the
                // producer in terms of a symbolic region of the
                // consumer.
                bool bounds_are_constant = true;
                auto eval_bound = [&](const BoundInfo &b) {
                    if (b.affine) {
                        // Common-case performance optimization
                        const auto &src_pair = consumer_loop[b.consumer_dim];
                        int64_t src = b.uses_max ? src_pair.max() : src_pair.min();
                        bounds_are_constant &= src_pair.constant_extent();
                        return src * b.coeff + b.constant;
                    } else {
                        Expr substituted = substitute(s, b.expr);
                        Expr e = simplify(substituted);
                        const int64_t *i = as_const_int(e);
                        internal_assert(i) << "Should be constant: " << b.expr << " -> " << substituted << " -> " << e << '\n';
                        bounds_are_constant = false;
                        return *i;
                    }
                };
                int64_t a = eval_bound(bounds[i].first);
                int64_t b = eval_bound(bounds[i].second);
                producer_required[i].union_with(Span(a, b, bounds_are_constant));
            }
        }

        bool all_bounds_affine;
    };

    vector<Node> nodes;
    vector<Edge> edges;

    // We're going to be querying this DAG a lot while searching for
    // an optimal schedule, so we'll also create a variety of
    // auxiliary data structures.
    map<Function, Node *, Function::Compare> node_map;

    // Create the function DAG, and do all the dependency and cost
    // analysis. This is done once up-front before the tree search.
    FunctionDAG(const vector<Function> &outputs, const MachineParams &params, const Target &target) {
        map<string, Function> env;
        for (Function o : outputs) {
            populate_environment(o, env);
        }

        // A mutator to apply parameter estimates to the expressions
        // we encounter while constructing the graph.
        class ApplyParamEstimates : public IRMutator2 {
            using IRMutator2::visit;

            Expr visit(const Variable *op) override {
                Expr expr;
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
                    internal_assert(expr.defined()) << "Missing estimate for " << op->name << '\n';
                    return expr;
                } else {
                    return op;
                }
            }
        } apply_param_estimates;

        // Compute a realization order
        vector<string> order = topological_order(outputs, env);

        // Construct the mapping from Funcs to Nodes
        nodes.resize(order.size());
        for (size_t i = 0; i < order.size(); i++) {
            Function f = env[order[order.size() - i - 1]];
            nodes[i].func = f;
            nodes[i].id = (int)i;
            nodes[i].max_id = (int)order.size();
            nodes[i].dag = this;
            node_map[f] = &nodes[i];
        }

        int stage_count = 0;

        for (size_t i = order.size(); i > 0; i--) {
            Function consumer = env[order[i-1]];

            Node &node = nodes[order.size() - i];
            Scope<Interval> scope;
            node.func = consumer;

            // Create a symbolic region for this Func.
            for (int j = 0; j < consumer.dimensions(); j++) {
                Expr min_var = Variable::make(Int(32), consumer.name() + "." + consumer.args()[j] + ".min");
                Expr max_var = Variable::make(Int(32), consumer.name() + "." + consumer.args()[j] + ".max");
                Expr extent = max_var - min_var + 1;
                Interval interval(min_var, max_var);
                scope.push(consumer.args()[j], interval);
                node.region_required.push_back(interval);
            }

            auto pure_args = node.func.args();

            for (int s = 0; s <= (int)consumer.updates().size(); s++) {
                stage_count++;
                Halide::Stage halide_stage = Func(consumer);
                if (s > 0) halide_stage = Func(consumer).update(s-1);
                Node::Stage stage(halide_stage);
                node.stages.emplace_back(std::move(stage));
            }

            for (int s = 0; s <= (int)consumer.updates().size(); s++) {
                auto &stage = node.stages[s];
                stage.node = &node;
                // stage.name = "get_func(" + std::to_string(i - 1) + ")";
                stage.name = consumer.name();
                if (s > 0) {
                    stage.name += ".update(" + std::to_string(s - 1) + ")";
                }

                const Definition &def = (s == 0) ? consumer.definition() : consumer.update(s - 1);
                const StageSchedule &sched = def.schedule();

                Scope<Interval> stage_scope_with_concrete_rvar_bounds, stage_scope_with_symbolic_rvar_bounds;
                stage_scope_with_concrete_rvar_bounds.set_containing_scope(&scope);
                stage_scope_with_symbolic_rvar_bounds.set_containing_scope(&scope);
                for (const auto &rv : sched.rvars()) {
                    Expr min = simplify(apply_param_estimates.mutate(rv.min));
                    Expr max = simplify(apply_param_estimates.mutate(rv.min + rv.extent - 1));
                    stage_scope_with_concrete_rvar_bounds.push(rv.var, Interval(min, max));
                    min = Variable::make(Int(32), consumer.name() + "." + rv.var + ".min");
                    max = Variable::make(Int(32), consumer.name() + "." + rv.var + ".max");
                    stage_scope_with_symbolic_rvar_bounds.push(rv.var, Interval(min, max));
                }

                // Figure out the region computed of the stage by taking bounds of the LHS Exprs
                if (s == 0) {
                    node.region_computed.resize(consumer.dimensions());
                }
                for (int j = 0; j < consumer.dimensions(); j++) {
                    // The region computed always uses the full extent of the rvars
                    Interval in = bounds_of_expr_in_scope(def.args()[j], stage_scope_with_concrete_rvar_bounds);
                    internal_assert(in.is_bounded())
                        << "Region computed of " << consumer.name()
                        << " is unbounded: [" << in.min << " " << in.max << "]\n";
                    if (s == 0) {
                        node.region_computed[j].in = in;
                    } else {
                        node.region_computed[j].in.include(in);
                    }
                }
                if (s == (int)consumer.updates().size()) {
                    // Simplify region computed and perform additional
                    // special-case analysis to make it faster to evaluate.
                    node.region_computed_all_common_cases = true;
                    for (int j = 0; j < consumer.dimensions(); j++) {
                        const auto &req = node.region_required[j];
                        auto &comp = node.region_computed[j];
                        comp.in.min = simplify(apply_param_estimates.mutate(comp.in.min));
                        comp.in.max = simplify(apply_param_estimates.mutate(comp.in.max));
                        if (equal(comp.in.min, req.min) && equal(comp.in.max, req.max)) {
                            comp.equals_required = true;
                        } else {
                            const Min *min = comp.in.min.as<Min>();
                            const Max *max = comp.in.max.as<Max>();
                            const int64_t *min_b = min ? as_const_int(min->b) : nullptr;
                            const int64_t *max_b = max ? as_const_int(max->b) : nullptr;
                            if (min_b && max_b && equal(min->a, req.min) && equal(max->a, req.max)) {
                                comp.equals_union_of_required_with_constants = true;
                                comp.c_min = *min_b;
                                comp.c_max = *max_b;
                            } else {
                                node.region_computed_all_common_cases = false;
                            }
                        }
                    }
                }

                // We'll take any existing reordering, but won't handle existing splits
                internal_assert(sched.splits().empty());
                stage.loop_nest_all_common_cases = true;
                for (size_t i = 0; i < sched.dims().size(); i++) {
                    const auto &d = sched.dims()[i];
                    // Skip synthetic loops like "__outermost"
                    if (!stage_scope_with_symbolic_rvar_bounds.contains(d.var)) continue;

                    Node::Loop l;
                    l.var = d.var;
                    l.accessor = stage.name + ".get_schedule().dims()[" + std::to_string(i) + "].var";

                    // We already have the right variable names in the stage scope
                    Interval in = stage_scope_with_concrete_rvar_bounds.get(l.var);
                    l.min = in.min;
                    l.max = in.max;
                    l.pure = d.is_pure();
                    l.rvar = d.is_rvar();
                    l.pure_dim = -1;

                    // Additional analysis to speed up evaluation of
                    // common cases. Loop bounds that are just one of
                    // the dimensions of the symbolic region computed
                    // are common, as are constant bounds.
                    l.equals_region_computed = false;
                    for (int j = 0; j < consumer.dimensions(); j++) {
                        if (l.var == pure_args[j]) {
                            l.pure_dim = j;
                        }
                        if (equal(l.min, node.region_computed[j].in.min) &&
                            equal(l.max, node.region_computed[j].in.max)) {
                            l.equals_region_computed = true;
                            l.region_computed_dim = j;
                            break;
                        }
                    }

                    if (!l.equals_region_computed) {
                        const int64_t *c_min = as_const_int(l.min), *c_max = as_const_int(l.max);
                        if (c_min && c_max) {
                            l.bounds_are_constant = true;
                            l.c_min = *c_min;
                            l.c_max = *c_max;
                        } else {
                            l.bounds_are_constant = false;
                        }
                    }

                    stage.loop_nest_all_common_cases &= (l.bounds_are_constant || l.equals_region_computed);
                    stage.loop.emplace_back(std::move(l));
                }

                // Bundle all expressions associated with the definition into a single dummy call node
                vector<Expr> exprs_vector = def.args();
                exprs_vector.insert(exprs_vector.end(), def.values().begin(), def.values().end());
                if (def.predicate().defined()) {
                    exprs_vector.push_back(def.predicate());
                }
                Expr exprs = Call::make(Int(32), "dummy", exprs_vector, Call::Extern);

                // Walk over the expressions involved sniffing types
                class CheckTypes : public IRVisitor {
                    using IRVisitor::visit;

                    void visit(const IntImm *op) override {
                        check_type(op->type);
                    }

                    void visit(const UIntImm *op) override {
                        check_type(op->type);
                    }

                    void visit(const FloatImm *op) override {
                        check_type(op->type);
                    }

                    void visit(const Variable *op) override {
                        check_type(op->type);
                    }

                    void visit(const Call *op) override {
                        calls[op->name]++;
                        IRVisitor::visit(op);
                        check_type(op->type);
                        if (op->call_type == Call::Halide || op->call_type == Call::Image) {
                            is_pointwise &= op->args.size() == func.args().size();
                            // TODO: actually sniff the coordinates
                            // accessed, instead of only catching the
                            // built-in boundary condition primitive.
                            if (is_pointwise) {
                                for (size_t i = 0; i < op->args.size(); i++) {
                                    const Variable *v = op->args[i].as<Variable>();
                                    is_pointwise &= (v != nullptr) && (v->name == func.args()[i]);
                                }
                            }
                        }
                    }

                    void visit(const Cast *op) override {
                        IRVisitor::visit(op);
                        check_type(op->type);
                    }

                    void check_type(Type t) {
                        if (t.bits() > 1 &&
                            (!narrowest_type.bits() ||
                             t.bits() < narrowest_type.bits())) {
                            narrowest_type = t;
                        }
                    }
                    Function func;
                public:
                    bool is_pointwise = true;
                    int leaves = 0;
                    Type narrowest_type;
                    map<string, int> calls;
                    CheckTypes(Function f) : func(f) {}
                };
                CheckTypes checker(consumer);
                exprs.accept(&checker);

                Type widest_output_type = def.values()[0].type();

                int bytes_per_point = 0;
                for (const auto &e : def.values()) {
                    bytes_per_point += e.type().bytes();
                    if (e.type().bytes() > widest_output_type.bytes()) {
                        widest_output_type = e.type();
                    }
                }
                if (s == 0) {
                    node.bytes_per_point = bytes_per_point;
                }

                if (target.has_gpu_feature()) {
                    stage.vector_size = 32; // warp size
                } else {
                    stage.vector_size = target.natural_vector_size(checker.narrowest_type);
                    stage.output_vector_size = target.natural_vector_size(widest_output_type);
                }

                if (s == 0) {
                    node.vector_size = stage.vector_size;
                } else {
                    node.vector_size = std::max(node.vector_size, stage.vector_size);
                }

                node.is_output = false;
                for (const auto &o : outputs) {
                    node.is_output |= o.same_as(node.func);
                }

                if (node.is_output) {
                    // Get the bounds estimate
                    map<string, Span> estimates;
                    for (auto b : consumer.schedule().estimates()) {
                        const int64_t *i_min = as_const_int(b.min);
                        const int64_t *i_extent = as_const_int(b.extent);

                        if (!i_min || !i_extent) continue;

                        if (false) {
                            // HACK: Some methods we compare to
                            // compile for statically known
                            // input/output sizes. We don't need to -
                            // we take estimates but the compiled code
                            // doesn't enforce them. If you want to
                            // make a comparison fair and target a
                            // fixed size, use this branch of the
                            // if. In practice I don't see a runtime
                            // difference, so I left it disabled.  In
                            // theory, Sizes being constant makes it
                            // possible to do things like unroll
                            // across color channels, so it affects
                            // the scheduling space.  TODO: Make this
                            // a documented env var
                            Func(node.func).bound(b.var, b.min, b.extent);
                            estimates[b.var] = Span(*i_min, *i_min + *i_extent - 1, true);
                        } else {
                            estimates[b.var] = Span(*i_min, *i_min + *i_extent - 1, false);
                        }
                    }
                    for (auto b : consumer.schedule().bounds()) {
                        const int64_t *i_min = as_const_int(b.min);
                        const int64_t *i_extent = as_const_int(b.extent);
                        if (i_min && i_extent) {
                            // It's a true bound, not just an estimate
                            estimates[b.var] = Span(*i_min, *i_min + *i_extent - 1, true);
                        }
                    }
                    // Set the bounds using the estimates
                    for (int i = 0; i < consumer.dimensions(); i++) {
                        auto it = estimates.find(consumer.args()[i]);
                        user_assert(it != estimates.end())
                            << "Need an estimate on dimension " << i << " of \"" << consumer.name() << "\"";
                        node.estimated_region_required.push_back(it->second);
                    }
                }

                stage.index = s;

                exprs = apply_param_estimates.mutate(exprs);

                FuncValueBounds func_value_bounds = compute_function_value_bounds(order, env);
                for (auto &p : func_value_bounds) {
                    p.second.min = apply_param_estimates.mutate(p.second.min);
                    p.second.max = apply_param_estimates.mutate(p.second.max);
                }

                // For this stage scope we want symbolic bounds for the rvars

                // Now create the edges that lead to this func
                bool any_incoming_edges = false;
                node.is_pointwise = !node.func.has_update_definition();

                // TODO: peephole the boundary condition call pattern instead of assuming the user used the builtin
                node.is_boundary_condition = node.is_pointwise && starts_with(node.func.name(), "repeat_edge");

                auto boxes = boxes_required(exprs, stage_scope_with_symbolic_rvar_bounds, func_value_bounds);
                for (auto &p : boxes) {
                    auto it = env.find(p.first);
                    if (it != env.end() && p.first != consumer.name()) {
                        // Discard loads from input images and self-loads
                        Edge edge;
                        edge.consumer = &stage;
                        edge.producer = node_map.at(env[p.first]);
                        edge.all_bounds_affine = true;

                        for (Interval &in : p.second.bounds) {
                            // Whenever a relationship is unbounded, we must inline
                            internal_assert(in.is_bounded())
                                << "Unbounded producer->consumer relationship: "
                                << edge.producer->func.name() << " -> " << edge.consumer->name << "\n";
                            Edge::BoundInfo min(simplify(in.min), *edge.consumer);
                            Edge::BoundInfo max(simplify(in.max), *edge.consumer);
                            edge.bounds.emplace_back(std::move(min), std::move(max));
                            edge.all_bounds_affine &= edge.bounds.back().first.affine;
                            edge.all_bounds_affine &= edge.bounds.back().second.affine;
                        }
                        edge.calls = checker.calls[edge.producer->func.name()];
                        any_incoming_edges = true;
                        node.is_pointwise &= checker.is_pointwise;
                        edges.emplace_back(std::move(edge));
                    }
                }

                node.is_wrapper = node.func.is_wrapper();
                node.is_input = !node.func.has_update_definition() && node.is_wrapper && !any_incoming_edges;
                node.dimensions = node.func.dimensions();
            }
        }

        // Initialize the memory layouts for the bounds structs
        for (auto &n : nodes) {
            n.bounds_memory_layout.reset(new BoundContents::Layout);
            auto &l = *(n.bounds_memory_layout);
            l.computed_offset = n.func.dimensions();
            l.total_size = l.computed_offset + n.func.dimensions();
            for (const auto &s : n.stages) {
                l.loop_offset.push_back(l.total_size);
                l.total_size += (int)s.loop.size();
            }
        }


        // Give all the stages unique ids to support perfect hashing of them
        {
            int i = 0;
            for (auto &n : nodes) {
                for (auto &s : n.stages) {
                    s.id = i;
                    s.max_id = stage_count;
                    i++;
                }
            }
        }

        for (size_t i = 0; i < edges.size(); i++) {
            edges[i].producer->outgoing_edges.push_back(&(edges[i]));
            edges[i].consumer->incoming_edges.push_back(&(edges[i]));
        }

        // Compute transitive dependencies
        for (size_t i = nodes.size(); i > 0; i--) {
            auto &n = nodes[i-1];

            for (auto &s : n.stages) {
                s.dependencies.resize(nodes.size(), false);

                for (auto *e : s.incoming_edges) {
                    s.dependencies[e->producer->id] = true;
                    for (auto &s2 : e->producer->stages) {
                        for (size_t j = 0; j < nodes.size(); j++) {
                            s.dependencies[j] = s.dependencies[j] || s2.dependencies[j];
                        }
                    }
                }
            }
        }

        // Compute features for the neural net
        featurize();
    }

    class Featurizer : public IRVisitor {
        using IRVisitor::visit;

        Function &func;
        Node::Stage &stage;

        int &op_bucket(PipelineFeatures::OpType op_type, Type scalar_type) {
            int type_bucket = (int)classify_type(scalar_type);
            stage.features.types_in_use[type_bucket] = true;
            return stage.features.op_histogram[(int)op_type][type_bucket];
        }

        PipelineFeatures::ScalarType classify_type(Type t) {
            if (t.is_float() && t.bits() > 32) {
                return PipelineFeatures::ScalarType::Double;
            } else if (t.is_float()) {
                return PipelineFeatures::ScalarType::Float;
            } else if (t.bits() == 1) {
                return PipelineFeatures::ScalarType::Bool;
            } else if (t.bits() <= 8) {
                return PipelineFeatures::ScalarType::UInt8;
            } else if (t.bits() <= 16) {
                return PipelineFeatures::ScalarType::UInt16;
            } else if (t.bits() <= 32) {
                return PipelineFeatures::ScalarType::UInt32;
            } else {
                return PipelineFeatures::ScalarType::UInt64;
            }
        }
        void visit(const Variable *op) override {
            if (op->param.defined()) {
                op_bucket(PipelineFeatures::OpType::Param, op->type)++;
            } else {
                op_bucket(PipelineFeatures::OpType::Variable, op->type)++;
            }
        }
        void visit(const IntImm *op) override {
            op_bucket(PipelineFeatures::OpType::Const, op->type)++;
        }
        void visit(const UIntImm *op) override {
            op_bucket(PipelineFeatures::OpType::Const, op->type)++;
        }
        void visit(const FloatImm *op) override {
            op_bucket(PipelineFeatures::OpType::Const, op->type)++;
        }
        void visit(const Add *op) override {
            op_bucket(PipelineFeatures::OpType::Add, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Sub *op) override {
            op_bucket(PipelineFeatures::OpType::Sub, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Mul *op) override {
            op_bucket(PipelineFeatures::OpType::Mul, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Mod *op) override {
            op_bucket(PipelineFeatures::OpType::Mod, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Div *op) override {
            op_bucket(PipelineFeatures::OpType::Div, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Min *op) override {
            op_bucket(PipelineFeatures::OpType::Min, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Max *op) override {
            op_bucket(PipelineFeatures::OpType::Max, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const EQ *op) override {
            op_bucket(PipelineFeatures::OpType::EQ, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const NE *op) override {
            op_bucket(PipelineFeatures::OpType::NE, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const LT *op) override {
            op_bucket(PipelineFeatures::OpType::LT, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const LE *op) override {
            op_bucket(PipelineFeatures::OpType::LE, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const GT *op) override {
            // Treat as a flipped LT
            op_bucket(PipelineFeatures::OpType::LT, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const GE *op) override {
            op_bucket(PipelineFeatures::OpType::LE, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const And *op) override {
            op_bucket(PipelineFeatures::OpType::And, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Or *op) override {
            op_bucket(PipelineFeatures::OpType::Or, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Not *op) override {
            op_bucket(PipelineFeatures::OpType::Not, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Select *op) override {
            op_bucket(PipelineFeatures::OpType::Select, op->type)++;
            IRVisitor::visit(op);
        }
        Scope<Expr> lets;

        void visit(const Let *op) override {
            ScopedBinding<Expr> bind(lets, op->name, op->value);
            op_bucket(PipelineFeatures::OpType::Let, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Call *op) override {
            IRVisitor::visit(op);
            if (op->call_type == Call::Halide) {
                if (op->name == func.name()) {
                    visit_memory_access(op->name, op->type, op->args, PipelineFeatures::AccessType::LoadSelf);
                    op_bucket(PipelineFeatures::OpType::SelfCall, op->type)++;
                } else {
                    visit_memory_access(op->name, op->type, op->args, PipelineFeatures::AccessType::LoadFunc);
                    op_bucket(PipelineFeatures::OpType::FuncCall, op->type)++;
                }
            } else if (op->call_type == Call::Extern || op->call_type == Call::PureExtern ||
                       op->call_type == Call::Intrinsic || op->call_type == Call::PureIntrinsic) {
                op_bucket(PipelineFeatures::OpType::ExternCall, op->type)++;
            } else if (op->call_type == Call::Image) {
                visit_memory_access(op->name, op->type, op->args, PipelineFeatures::AccessType::LoadImage);
                op_bucket(PipelineFeatures::OpType::ImageCall, op->type)++;
            } // TODO: separate out different math calls a little better (sqrt vs sin vs lerp)
        }

        // Take the derivative of an integer index expression. If it's
        // a rational constant, return it, otherwise return a sentinel
        // value.

        // The derivative of each let w.r.t each var. The keys are
        // just the var names separated by a space.
        Scope<OptionalRational> dlets;

        OptionalRational differentiate(const Expr &e, const string &v) {
            if (!expr_uses_var(e, v, lets)) {
                return {true, 0, 1};
            } else if (const Variable *var = e.as<Variable>()) {
                if (var->name == v) {
                    return {true, 1, 1};
                }
                for (const auto &l : stage.loop) {
                    if (var->name == l.var) {
                        // Some other loop variable
                        return {true, 0, 1};
                    }
                }
                if (var->param.defined()) {
                    // An argument
                    return {true, 0, 1};
                } else if (lets.contains(var->name)) {
                    string key = v + " " + var->name;
                    if (dlets.contains(key)) {
                        return dlets.get(key);
                    }
                    auto a = differentiate(lets.get(var->name), v);
                    dlets.push(key, a);
                    return a;
                }
                // Some mystery variable. Who knows what it depends on.
                internal_error << "Encountered unbound variable in call args: " << var->name << "\n";
                return {false, 0, 0};
            } else if (const Add *op = e.as<Add>()) {
                auto a = differentiate(op->a, v);
                a += differentiate(op->b, v);
                return a;
            } else if (const Sub *op = e.as<Sub>()) {
                auto a = differentiate(op->a, v);
                auto b = differentiate(op->b, v);
                b.numerator = -b.numerator;
                a += b;
                return a;
            } else if (const Mul *op = e.as<Mul>()) {
                auto a = differentiate(op->a, v);
                if (const int64_t *ib = as_const_int(op->b)) {
                    a.numerator *= *ib;
                    return a;
                } else {
                    return {false, 0, 0};
                }
            } else if (const Div *op = e.as<Div>()) {
                auto a = differentiate(op->a, v);
                if (const int64_t *ib = as_const_int(op->b)) {
                    if (a.numerator != 0) {
                        a.denominator *= *ib;
                    }
                    return a;
                } else {
                    return {false, 0, 0};
                }
            } else if (const Call *op = e.as<Call>()) {
                if (op->is_intrinsic(Call::likely)) {
                    // TODO: Should a likely on one side of a min/max dominate?
                    return differentiate(op->args[0], v);
                }
            }

            return {false, 0, 0};
        }

        void visit_memory_access(const std::string &name, Type t, const vector<Expr> &args, PipelineFeatures::AccessType type) {
            // Compute matrix of partial derivatives of args w.r.t. loop params
            vector<vector<OptionalRational>> matrix;
            vector<size_t> ones_per_row(args.size(), 0),
                zeros_per_row(args.size(), 0),
                ones_per_col(stage.loop.size(), 0),
                zeros_per_col(stage.loop.size(), 0);
            matrix.resize(args.size());
            bool is_pointwise = args.size() == stage.loop.size();
            for (size_t i = 0; i < args.size(); i++) {
                matrix[i].resize(stage.loop.size());
                for (size_t j = 0; j < stage.loop.size(); j++) {
                    auto deriv = differentiate(args[i], stage.loop[j].var);
                    zeros_per_row[i] += deriv == 0;
                    ones_per_row[i] += deriv == 1;
                    zeros_per_col[j] += deriv == 0;
                    ones_per_col[j] += deriv == 1;
                    is_pointwise &= (i == j ? deriv == 1 : deriv == 0);
                    matrix[i][j] = deriv;
                }
            }
            bool is_transpose = (args.size() == stage.loop.size());
            bool is_broadcast = true, is_slice = true;
            for (size_t i = 0; i < args.size(); i++) {
                bool single_one = (ones_per_row[i] == 1) && (zeros_per_row[i] == stage.loop.size() - 1);
                bool all_zero = (zeros_per_row[i] == stage.loop.size());
                is_transpose &= single_one;
                is_broadcast &= single_one;
                is_slice &= single_one || all_zero;
            }
            for (size_t j = 0; j < stage.loop.size(); j++) {
                bool single_one = (ones_per_col[j] == 1) && (zeros_per_col[j] == args.size() - 1);
                bool all_zero = (zeros_per_col[j] == args.size());
                is_transpose &= single_one || all_zero;
                is_broadcast &= single_one;
                is_slice &= single_one;
            }

            auto type_class = classify_type(t);

            stage.features.pointwise_accesses[(int)type][(int)type_class] += is_pointwise;
            stage.features.transpose_accesses[(int)type][(int)type_class] += is_transpose;
            stage.features.broadcast_accesses[(int)type][(int)type_class] += is_broadcast;
            stage.features.slice_accesses[(int)type][(int)type_class] += is_slice;

            for (auto *e : stage.incoming_edges) {
                if (e->producer->func.name() == name) {
                    e->add_load_jacobian(std::move(matrix));
                }
            }
        }

    public:
        Featurizer(Function &func, Node::Stage &stage) :
            func(func), stage(stage) {}

        void visit_store_args(const std::string &name, Type t, vector<Expr> args) {
            for (auto &e : args) {
                e = common_subexpression_elimination(simplify(e)); // Get things into canonical form
            }
            visit_memory_access(name, t, args, PipelineFeatures::AccessType::Store);
        }
    };

    // Compute the featurization for the entire DAG
    void featurize() {
        for (Node &node : nodes) {
            for (size_t stage_idx = 0; stage_idx < node.stages.size(); stage_idx++) {
                Node::Stage &stage = node.stages[stage_idx];

                // Pick a dimension to vectorize over - the innermost pure loop
                size_t vector_dim = 0;
                while (vector_dim < stage.loop.size() && !stage.loop[vector_dim].pure) vector_dim++;
                // bool vectorized = vector_dim < stage.loop.size();

                Featurizer featurizer(node.func, stage);

                Definition def = node.func.definition();
                if (stage_idx > 0) def = node.func.updates()[stage_idx - 1];

                memset(&stage.features, 0, sizeof(stage.features));

                for (auto v : def.values()) {
                    featurizer.visit_store_args(node.func.name(), v.type(), def.args());
                    v = common_subexpression_elimination(simplify(v)); // Get things into canonical form
                    v.accept(&featurizer);
                }
                for (auto v : def.args()) {
                    v = common_subexpression_elimination(simplify(v)); // Get things into canonical form
                    v.accept(&featurizer);
                }
            }
        }
    }

    void dump() const {
        for (const Node &n : nodes) {
            debug(0) << "Node: " << n.func.name() << '\n'
                     << "  Symbolic region required: \n";
            for (const Interval &i : n.region_required) {
                debug(0) << "    " << i.min << ", " << i.max << '\n';
            }
            debug(0) << "  Region computed: \n";
            for (const auto &i : n.region_computed) {
                debug(0) << "    " << i.in.min << ", " << i.in.max << '\n';
            }
            for (size_t i = 0; i < n.stages.size(); i++) {
                debug(0) << "  Stage " << i << ":\n";
                for (const auto &l : n.stages[i].loop) {
                    debug(0) << "    " << l.var << " " << l.min << " " << l.max << '\n';
                }
                n.stages[i].features.dump();
            }
            debug(0) << "  pointwise: " << n.is_pointwise
                     << " boundary condition: " << n.is_boundary_condition
                     << " wrapper: " << n.is_wrapper
                     << " input: " << n.is_input
                     << " output: " << n.is_output << "\n";
        }
        for (const Edge &e : edges) {
            debug(0) << "Edge: " << e.producer->func.name() << " -> " << e.consumer->name << '\n'
                     << "  Footprint: \n";
            int j = 0;
            for (const auto &i : e.bounds) {
                debug(0) << "    Min " << j << ": " << i.first.expr << '\n';
                debug(0) << "    Max " << j << ": " << i.second.expr << '\n';
                j++;
            }

            debug(0) << "  Load Jacobians:\n";
            for (const auto &jac : e.load_jacobians) {
                jac.dump("  ");
            }
        }
    }

private:
    // The auxiliary data structures use internal pointers, so we'll hide the copy constructor
    FunctionDAG(const FunctionDAG &other) = delete;
    void operator=(const FunctionDAG &other) = delete;

};

}

template<>
RefCount &ref_count<Autoscheduler::BoundContents>(const Autoscheduler::BoundContents *t) {return t->ref_count;}

template<>
void destroy<Autoscheduler::BoundContents>(const Autoscheduler::BoundContents *t) {
    // Release it back into the memory pool to be reused
    t->layout->release(t);
}

}}

#endif
