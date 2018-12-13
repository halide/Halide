#include <set>
#include <queue>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iostream>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>

#include "Halide.h"
#include "halide_benchmark.h"
#include "ThroughputPredictorPipeline.h"
#include "Errors.h"

namespace Halide {
namespace Internal {

namespace {

using std::string;
using std::vector;
using std::map;
using std::set;
using std::pair;

uint64_t get_dropout_threshold() {
    string random_dropout_str = get_env_variable("HL_RANDOM_DROPOUT");
    if (!random_dropout_str.empty()) {
        return atoi(random_dropout_str.c_str());
    } else {
        return 100;
    }
}

static uint64_t random_dropout_threshold = 100;

bool random_dropout() {
    static bool init =
        []() {random_dropout_threshold = get_dropout_threshold(); return true;}();
    (void)init;
    uint64_t r = rand();
    bool drop_it = (r % 100) >= random_dropout_threshold;
    return drop_it;
}

struct PipelineFeatures {
    // A featurization of the compute done by a Func, to
    // feed the neural network.

    enum class OpType {
        Const,
        Cast,
        Variable,
        Param,
        Add, Sub, Mod, Mul, Div, Min, Max,
        EQ, NE, LT, LE,
        And, Or, Not,
        Select,
        ImageCall,
        FuncCall,
        SelfCall,   // Recursive calls from a Func to itself
        ExternCall, // Math intrinsics, typically
        Let,        // Depends on what CSE has decided to do, but a good indication of register pressure
        NumOpTypes,
    };

    enum class ScalarType {
        Bool,
        UInt8,  // includes Int8
        UInt16, // includes Int16
        UInt32, // includes Int32 (TODO: is this a good idea? index math is a different sort of beast)
        UInt64, // Includes Int64
        Float,
        Double,
        NumScalarTypes
    };

    // Not a super-useful feature, but helps avoid printing huge numbers of zeros while debugging things
    int types_in_use[(int)ScalarType::NumScalarTypes];

    int op_histogram[(int)OpType::NumOpTypes][(int)ScalarType::NumScalarTypes];

    enum class AccessType {
        LoadFunc,
        LoadSelf,
        LoadImage,
        Store,
        NumAccessTypes
    };

    // Finer granularity call/store node properties. These are a
    // function of the matrix of derivatives of each arg to a
    // call w.r.t the loop variables of the Stage. Each row of
    // the matrix corresponds to one of the call arguments. In
    // each case we illustrate such a call, assuming that the
    // variables of this Func are x, y, z, and that the
    // dimension vectorized over is the first (x).
    int pointwise_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],  // Square identity matrix. f(x - 2, y + 8, z + param)
        transpose_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],         // Square permutation matrix. f(y + 1, z - 3, x)
        broadcast_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],         // Each row sums to 1. Each column sums to 1 or 0. f(y, x)
        slice_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],             // Each row sums to 1 or 0. Each column sums to 1. f(z, y, x, 4)


    // The following four features are dead (always zero), now that we
    // actually search over which dimension to vectorize over:
        vectorizable_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes], // First (vectorized) col is 1, 0, 0, ... f(x+y, z*y, y/z)
        strided_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],      // First col is [(int)2,3,4], 0, 0, ...        f(3*x + 1, z/8, y/z)
        scalar_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],       // First col is all zero                  f(y, 2, z*8)
        gather_scatter_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes];            // Not one of the three categories above  f(x, x, sqrt(y))

    // TODO: We should possibly feed these Jacobians directly
    // to the net rather than computing the properties above.

    // TODO: strided captures downsamples. What about upsamples?

    void dump() const {
        for (int i = 0; i < (int)ScalarType::NumScalarTypes; i++) {
            const char *type_names[] = {"Bool", "UInt8", "UInt16", "UInt32", "UInt64", "Float", "Double"};
            // Skip printing for types not used
            if (!types_in_use[i]) continue;


            debug(0) << "    Featurization for type " << type_names[i] << '\n'
                     << "     Op histogram:\n"
                     << "      Constant:   " << op_histogram[(int)OpType::Const][i] << '\n'
                     << "      Cast:       " << op_histogram[(int)OpType::Cast][i] << '\n'
                     << "      Variable:   " << op_histogram[(int)OpType::Variable][i] << '\n'
                     << "      Param:      " << op_histogram[(int)OpType::Param][i] << '\n'
                     << "      Add:        " << op_histogram[(int)OpType::Add][i] << '\n'
                     << "      Sub:        " << op_histogram[(int)OpType::Sub][i] << '\n'
                     << "      Mod:        " << op_histogram[(int)OpType::Mod][i] << '\n'
                     << "      Mul:        " << op_histogram[(int)OpType::Mul][i] << '\n'
                     << "      Div:        " << op_histogram[(int)OpType::Div][i] << '\n'
                     << "      Min:        " << op_histogram[(int)OpType::Min][i] << '\n'
                     << "      Max:        " << op_histogram[(int)OpType::Max][i] << '\n'
                     << "      EQ:         " << op_histogram[(int)OpType::EQ][i] << '\n'
                     << "      NE:         " << op_histogram[(int)OpType::NE][i] << '\n'
                     << "      LT:         " << op_histogram[(int)OpType::LT][i] << '\n'
                     << "      LE:         " << op_histogram[(int)OpType::LE][i] << '\n'
                     << "      And:        " << op_histogram[(int)OpType::And][i] << '\n'
                     << "      Or:         " << op_histogram[(int)OpType::Or][i] << '\n'
                     << "      Not:        " << op_histogram[(int)OpType::Not][i] << '\n'
                     << "      Select:     " << op_histogram[(int)OpType::Select][i] << '\n'
                     << "      ImageCall:  " << op_histogram[(int)OpType::ImageCall][i] << '\n'
                     << "      FuncCall:   " << op_histogram[(int)OpType::FuncCall][i] << '\n'
                     << "      SelfCall:   " << op_histogram[(int)OpType::SelfCall][i] << '\n'
                     << "      ExternCall: " << op_histogram[(int)OpType::ExternCall][i] << '\n'
                     << "      Let:        " << op_histogram[(int)OpType::Let][i] << '\n'
                     << "     Memory access patterns. Columns are calls to other Funcs, self-calls, input image access, and stores\n"
                     << "      Pointwise:      " << pointwise_accesses[0][i] << ' ' << pointwise_accesses[1][i] << ' ' << pointwise_accesses[2][i] << ' ' << pointwise_accesses[3][i] << '\n'
                     << "      Transpose:      " << transpose_accesses[0][i] << ' ' << transpose_accesses[1][i] << ' ' << transpose_accesses[2][i] << ' ' << transpose_accesses[3][i] << '\n'
                     << "      Broadcast:      " << broadcast_accesses[0][i] << ' ' << broadcast_accesses[1][i] << ' ' << broadcast_accesses[2][i] << ' ' << broadcast_accesses[3][i] << '\n'
                     << "      Slice:          " << slice_accesses[0][i] << ' ' << slice_accesses[1][i] << ' ' << slice_accesses[2][i] << ' ' << slice_accesses[3][i] << '\n'
                     << "      Vectorizable:   " << vectorizable_accesses[0][i] << ' ' << vectorizable_accesses[1][i] << ' ' << vectorizable_accesses[2][i] << ' ' << vectorizable_accesses[3][i] << '\n'
                     << "      Strided:        " << strided_accesses[0][i] << ' ' << strided_accesses[1][i] << ' ' << strided_accesses[2][i] << ' ' << strided_accesses[3][i] << '\n'
                     << "      Scalar:         " << scalar_accesses[0][i] << ' ' << scalar_accesses[1][i] << ' ' << scalar_accesses[2][i] << ' ' << scalar_accesses[3][i] << '\n'
                     << "      Gather/Scatter: " << gather_scatter_accesses[0][i] << ' ' << gather_scatter_accesses[1][i] << ' ' << gather_scatter_accesses[2][i] << ' ' << gather_scatter_accesses[3][i] << '\n';
        }
    }

};


// A concrete set of bounds for a Func. These are created and
// destroyed very frequently while exploring scheduling options, so we
// have a custom allocator and memory pool. Much like IR nodes, we
// treat them as immutable once created and wrapped in a Bound object
// so that they can be shared safely across scheduling alternatives.

struct BoundContents;
using Bound = IntrusivePtr<const BoundContents>;
struct BoundContents {
    mutable RefCount ref_count;
    struct Layout;
    const Layout *layout = nullptr;

    pair<int64_t, int64_t> *data() const {
        // This struct is a header
        return (pair<int64_t, int64_t> *)(const_cast<BoundContents *>(this) + 1);
    }

    pair<int64_t, int64_t> &region_required(int i) {
        return data()[i];
    }

    pair<int64_t, int64_t> &region_computed(int i) {
        return data()[i + layout->computed_offset];
    }

    pair<int64_t, int64_t> &loops(int i, int j) {
        return data()[j + layout->loop_offset[i]];
    }


    const pair<int64_t, int64_t> &region_required(int i) const {
        return data()[i];
    }

    const pair<int64_t, int64_t> &region_computed(int i) const {
        return data()[i + layout->computed_offset];
    }

    const pair<int64_t, int64_t> &loops(int i, int j) const {
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
        // number of pair<int64_t, int64_t> to allocate
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
            size_t size_of_one = sizeof(BoundContents) + total_size * sizeof(pair<int64_t, int64_t>);
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

}

template<>
RefCount &ref_count<BoundContents>(const BoundContents *t) {return t->ref_count;}

template<>
void destroy<BoundContents>(const BoundContents *t) {
    // Release it back into the memory pool to be reused
    t->layout->release(t);
}

namespace {

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
        vector<pair<int64_t, int64_t>> estimated_region_required;

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
        void required_to_computed(const pair<int64_t, int64_t> *required,
                                  pair<int64_t, int64_t> *computed) const {
            map<string, Expr> required_map;
            if (!region_computed_all_common_cases) {
                for (int i = 0; i < func.dimensions(); i++) {
                    required_map[region_required[i].min.as<Variable>()->name] = (int)required[i].first;
                    required_map[region_required[i].max.as<Variable>()->name] = (int)required[i].second;
                }
            }
            for (int i = 0; i < func.dimensions(); i++) {
                const auto &comp = region_computed[i];
                if (comp.equals_required) {
                    computed[i] = required[i];
                } else if (comp.equals_union_of_required_with_constants) {
                    computed[i].first = std::min(required[i].first, comp.c_min);
                    computed[i].second = std::max(required[i].second, comp.c_max);
                } else {
                    Expr min = simplify(substitute(required_map, comp.in.min));
                    Expr max = simplify(substitute(required_map, comp.in.max));
                    const int64_t *imin = as_const_int(min);
                    const int64_t *imax = as_const_int(max);
                    internal_assert(imin && imax) << min << ", " << max << '\n';
                    computed[i].first = *imin;
                    computed[i].second = *imax;
                }
            }
        }

        struct Loop {
            string var;
            bool pure;
            Expr min, max;

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
                                  const pair<int64_t, int64_t> *computed,
                                  pair<int64_t, int64_t> *loop) const {
            // debug(0) << "Loop nest for region func " << func.name() << " stage " << stage_idx << "\n";
            const auto &s = stages[stage_idx];
            map<string, Expr> computed_map;
            if (!s.loop_nest_all_common_cases) {
                for (int i = 0; i < func.dimensions(); i++) {
                    computed_map[region_required[i].min.as<Variable>()->name] = (int)computed[i].first;
                    computed_map[region_required[i].max.as<Variable>()->name] = (int)computed[i].second;
                }
            }

            for (size_t i = 0; i < s.loop.size(); i++) {
                const auto &l = s.loop[i];
                if (l.equals_region_computed) {
                    loop[i] = computed[l.region_computed_dim];
                } else if (l.bounds_are_constant) {
                    loop[i].first = l.c_min;
                    loop[i].second = l.c_max;
                } else {
                    Expr min = simplify(substitute(computed_map, l.min));
                    Expr max = simplify(substitute(computed_map, l.max));
                    const int64_t *imin = as_const_int(min);
                    const int64_t *imax = as_const_int(max);
                    internal_assert(imin && imax) << min << ", " << max << '\n';
                    loop[i] = std::make_pair(*imin, *imax);
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

            // The vectorization width that will be used.
            int vector_size;

            // The featurization of the compute done
            PipelineFeatures features;

            // The actual Halide front-end stage object
            Halide::Stage stage;

            // The name for scheduling (e.g. "foo.update(3)")
            string name;

            // Ids for perfect hashing on stages.
            int id, max_id;

            Stage(Halide::Stage s) : stage(s) {}
        };
        vector<Stage> stages;

        vector<const Edge *> outgoing_edges, incoming_edges;

        // Max vector size across the stages
        int vector_size;

        // A unique ID for this node, allocated consecutively starting
        // at zero for each pipeline.
        int id, max_id;

        bool is_output, is_input;

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
                    debug(2) << "Bound is affine: " << e << " == " << var << " * " << coeff << " + " << constant << "\n";
                } else {
                    affine = false;
                    debug(2) << "Bound is non-affine: " << e << "\n";
                }
            }
        };
        vector<pair<BoundInfo, BoundInfo>> bounds;

        FunctionDAG::Node *producer, *consumer;
        int consumer_stage;

        // The number of calls the consumer makes to the producer, per
        // point in the loop nest of the consumer.
        int calls;

        // Given a loop nest of the consumer stage, expand a region
        // required of the producer to be large enough to include all
        // points required.
        void expand_footprint(const pair<int64_t, int64_t> *consumer_loop,
                              pair<int64_t, int64_t> *producer_required) const {

            // Create a map from the symbolic loop variables to the actual loop size
            const auto &symbolic_loop = consumer->stages[consumer_stage].loop;
            map<string, Expr> s;
            if (!all_bounds_affine) {
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    auto p = consumer_loop[i];
                    const string &var = symbolic_loop[i].var;
                    s[consumer->func.name() + "." + var + ".min"] = (int)p.first;
                    s[consumer->func.name() + "." + var + ".max"] = (int)p.second;
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
                auto eval_bound = [&](const BoundInfo &b) {
                    if (b.affine) {
                        // Common-case performance optimization
                        const auto &src_pair = consumer_loop[b.consumer_dim];
                        int64_t src = b.uses_max ? src_pair.second : src_pair.first;
                        return src * b.coeff + b.constant;
                    } else {
                        Expr substituted = substitute(s, b.expr);
                        Expr e = simplify(substituted);
                        const int64_t *i = as_const_int(e);
                        internal_assert(i) << "Should be constant: " << b.expr << " -> " << substituted << " -> " << e << '\n';
                        return *i;
                    }
                };
                producer_required[i].first = std::min(producer_required[i].first, eval_bound(bounds[i].first));
                producer_required[i].second = std::max(producer_required[i].second, eval_bound(bounds[i].second));
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

            for (int s = 0; s <= (int)consumer.updates().size(); s++) {
                stage_count++;

                Halide::Stage halide_stage = Func(consumer);
                if (s > 0) halide_stage = Func(consumer).update(s-1);
                Node::Stage stage(halide_stage);
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

                    // Additional analysis to speed up evaluation of
                    // common cases. Loop bounds that are just one of
                    // the dimensions of the symbolic region computed
                    // are common, as are constant bounds.
                    l.equals_region_computed = false;
                    for (int j = 0; j < consumer.dimensions(); j++) {
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
                // Do the cost analysis. Simplistic for now - just counts
                // leaf nodes in the expression trees.
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
                public:
                    int leaves = 0;
                    Type narrowest_type;
                    map<string, int> calls;
                };
                CheckTypes checker;
                exprs.accept(&checker);

                int bytes_per_point = 0;
                for (const auto &e : def.values()) {
                    bytes_per_point += e.type().bytes();
                }
                if (s == 0) {
                    node.bytes_per_point = bytes_per_point;
                }

                stage.vector_size = target.natural_vector_size(checker.narrowest_type);

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
                    map<string, pair<int64_t, int64_t>> estimates;
                    for (auto b : consumer.schedule().estimates()) {
                        int64_t i_min = *as_const_int(b.min);
                        int64_t i_extent = *as_const_int(b.extent);
                        estimates[b.var] = {i_min, i_min + i_extent - 1};
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

                node.stages.emplace_back(std::move(stage));

                exprs = apply_param_estimates.mutate(exprs);

                FuncValueBounds func_value_bounds = compute_function_value_bounds(order, env);
                for (auto &p : func_value_bounds) {
                    p.second.min = apply_param_estimates.mutate(p.second.min);
                    p.second.max = apply_param_estimates.mutate(p.second.max);
                }

                // For this stage scope we want symbolic bounds for the rvars

                // Now create the edges that lead to this func
                bool any_incoming_edges = false;
                auto boxes = boxes_required(exprs, stage_scope_with_symbolic_rvar_bounds, func_value_bounds);
                for (auto &p : boxes) {
                    auto it = env.find(p.first);
                    if (it != env.end() && p.first != consumer.name()) {
                        // Discard loads from input images and self-loads
                        Edge edge;
                        edge.consumer = node_map.at(consumer);
                        edge.consumer_stage = s;
                        edge.producer = node_map.at(env[p.first]);
                        edge.all_bounds_affine = true;

                        for (Interval &in : p.second.bounds) {
                            // Whenever a relationship is unbounded, we must inline
                            internal_assert(in.is_bounded())
                                << "Unbounded producer->consumer relationship: "
                                << edge.producer->func.name() << " -> " << edge.consumer->func.name() << "\n";
                            Edge::BoundInfo min(simplify(in.min), edge.consumer->stages[s]);
                            Edge::BoundInfo max(simplify(in.max), edge.consumer->stages[s]);
                            edge.bounds.emplace_back(std::move(min), std::move(max));
                            edge.all_bounds_affine &= edge.bounds.back().first.affine;
                            edge.all_bounds_affine &= edge.bounds.back().second.affine;
                        }
                        edge.calls = checker.calls[edge.producer->func.name()];
                        any_incoming_edges = true;
                        edges.emplace_back(std::move(edge));
                    }
                }

                node.is_input = node.func.is_wrapper() && !any_incoming_edges;

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

        // Compute features for the neural net
        featurize();
    }

    class Featurizer : public IRVisitor {
        using IRVisitor::visit;

        Function &func;
        Node::Stage &stage;
        size_t vector_dim;

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
        void visit(const Let *op) override {
            op_bucket(PipelineFeatures::OpType::Let, op->type)++;
            IRVisitor::visit(op);
        }
        void visit(const Call *op) override {
            IRVisitor::visit(op);
            if (op->call_type == Call::Halide) {
                if (op->name == func.name()) {
                    visit_memory_access(op->type, op->args, PipelineFeatures::AccessType::LoadSelf);
                    op_bucket(PipelineFeatures::OpType::SelfCall, op->type)++;
                } else {
                    visit_memory_access(op->type, op->args, PipelineFeatures::AccessType::LoadFunc);
                    op_bucket(PipelineFeatures::OpType::FuncCall, op->type)++;
                }
            } else if (op->call_type == Call::Extern || op->call_type == Call::PureExtern) {
                op_bucket(PipelineFeatures::OpType::ExternCall, op->type)++;
            } else if (op->call_type == Call::Image) {
                visit_memory_access(op->type, op->args, PipelineFeatures::AccessType::LoadImage);
                op_bucket(PipelineFeatures::OpType::ImageCall, op->type)++;
            }
        }

        struct DerivativeResult {
            bool exists;
            int64_t numerator, denominator;

            void operator+=(const DerivativeResult &other) {
                if (!exists || !other.exists) {
                    exists = false;
                    return;
                }
                int64_t l = lcm(denominator, other.denominator);
                numerator *= l / denominator;
                denominator *= l / denominator;
                numerator += other.numerator * (l / other.denominator);
                int64_t g = gcd(numerator, denominator);
                numerator /= g;
                denominator /= g;
            }

            bool is_one() const {
                return exists && (numerator == denominator);
            }

            bool is_zero() const {
                return exists && (numerator == 0);
            }

            bool is_small_integer() const {
                return exists && (numerator == denominator ||
                                  numerator == denominator * 2 ||
                                  numerator == denominator * 3 ||
                                  numerator == denominator * 4);
            }
        };

        // Take the derivative of an integer index expression. If it's
        // a rational constant, return it, otherwise return a sentinel
        // value.
        DerivativeResult differentiate(const Expr &e, const string &v) {
            if (!expr_uses_var(e, v)) {
                return {true, 0, 1};
            } else if (e.as<Variable>()) {
                return {true, 1, 1};
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
                if (const int64_t *ib = as_const_int(op->b)) {
                    auto a = differentiate(op->a, v);
                    a.numerator *= *ib;
                    return a;
                } else {
                    return {false, 0, 0};
                }
            } else if (const Div *op = e.as<Div>()) {
                if (const int64_t *ib = as_const_int(op->b)) {
                    auto a = differentiate(op->a, v);
                    a.denominator *= *ib;
                    return a;
                } else {
                    return {false, 0, 0};
                }
            } else {
                // TODO: min, max?
                return {false, 0, 0};
            }
        }

        void visit_memory_access(Type t, const vector<Expr> &args, PipelineFeatures::AccessType type) {
            // Compute matrix of partial derivatives of args w.r.t. loop params
            vector<vector<Expr>> matrix;
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
                    zeros_per_row[i] += deriv.is_zero();
                    ones_per_row[i] += deriv.is_one();
                    zeros_per_col[j] += deriv.is_zero();
                    ones_per_col[j] += deriv.is_one();
                    is_pointwise &= (i == j ? deriv.is_one() : deriv.is_zero());
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
        }

    public:
        Featurizer(Function &func, Node::Stage &stage, size_t vector_dim) :
            func(func), stage(stage), vector_dim(vector_dim) {}

        void visit_store_args(Type t, vector<Expr> args) {
            for (auto &e : args) {
                e = common_subexpression_elimination(simplify(e)); // Get things into canonical form
            }
            visit_memory_access(t, args, PipelineFeatures::AccessType::Store);
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

                Featurizer featurizer(node.func, stage, vector_dim);

                Definition def = node.func.definition();
                if (stage_idx > 0) def = node.func.updates()[stage_idx - 1];

                memset(&stage.features, 0, sizeof(stage.features));

                for (auto v : def.values()) {
                    featurizer.visit_store_args(v.type(), def.args());
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
        }
        for (const Edge &e : edges) {
            debug(0) << "Edge: " << e.producer->func.name() << " -> " << e.consumer->func.name() << '\n'
                     << "  Footprint: \n";
            int j = 0;
            for (const auto &i : e.bounds) {
                debug(0) << "    Min " << j << ": " << i.first.expr << '\n';
                debug(0) << "    Max " << j << ": " << i.second.expr << '\n';
                j++;
            }

        }
    }

private:
    // The auxiliary data structures use internal pointers, so we'll hide the copy constructor
    FunctionDAG(const FunctionDAG &other) = delete;
    void operator=(const FunctionDAG &other) = delete;

};

vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor, bool allow_splits, int vector_dim, int vector_size) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v;
        v = generate_tilings(s, d - 1, factor, allow_splits, vector_dim, vector_size);
        // If we're already generated tons of tiling configs for the
        // inner loops, search the outer loops with coarser
        // granularity.
        while (v.size() > (size_t)factor * 100) {
            factor *= 2;
        }

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
                if (s[d] != 1 && !is_full && is_one && (d != vector_dim)) {
                    t.back() = s[d];
                    result.push_back(t);
                }
            } else {
                int max_inner = 0;
                int first_inner = (d == vector_dim) ? vector_size : 1;
                for (int inner = first_inner; inner < s[d]; inner *= factor) {
                    int outer = (s[d] + inner - 1) / inner;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    // Stop when we hit inner sizes that would do too much recompute
                    if (inner > first_inner && inner * outer * 7 > s[d] * 8) break;
                    max_inner = inner;
                    t.back() = outer;
                    result.push_back(t);
                }
                for (int outer = 1; outer <= s[d]; outer *= factor) {
                    int inner = (s[d] + outer - 1) / outer;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    // Stop when we get into the regime covered by the loop above.
                    if (inner < max_inner * 2) break;
                    // Or when the wasted compute gets too bad.
                    if (inner * outer * 7 > s[d] * 8) break;
                    t.back() = outer;
                    result.push_back(t);
                }

                // The sequence above (in terms of the inner loop) goes 1 2 4 8 16 ...
                // but 3 is an important inner tiling factor for matrix multiply ops.
                int inner3 = (d == vector_dim) ? 3*vector_size : 3;
                int outer3 = (s[d] + inner3 - 1) / inner3;
                if (factor == 2 && inner3 < s[d] && outer3 < s[d] && outer3 > 1) {
                    if (inner3 * outer3 * 7 <= s[d] * 8) {
                        t.back() = outer3;
                        result.push_back(t);
                    }
                }
            }
        }
    }
    return result;
}

// The schedule-dependent portion of the featurization of a stage
struct ScheduleFeatures {
    int64_t num_realizations = 0; // Product of outer loops at store_at site
    int64_t num_productions = 0;  // Product of outer loops at compute_at site
    int64_t points_computed_per_realization = 0; // Number of times the innermost stmt happens per store_at
    int64_t points_computed_per_production = 0;  // Number of times the innermost stmt happens per compute_at
    int64_t points_computed_total = 0;
    // points_computed_total
    //  == num_realizations * points_computed_per_realization
    //  ~= num_productions * points_computed_per_production
    // Only approximately equal because of the simplifications made
    // regarding the modeling of sliding window

    int64_t points_computed_minimum = 0; // The minimum number of points that are actually required to be computed to produce a correct output.

    int64_t innermost_loop_extent = 0; // Trip count of innermost serial loop
    int64_t innermost_pure_loop_extent = 0; // Trip count of innermost loop over the innermost storage dimension

    int64_t inner_parallelism = 0; // The number of parallel jobs used in the production of this Func. 1 unless the Func is compute_root.
    int64_t outer_parallelism = 0; // The number of times this Func could be realized in parallel. 1 when the Func is compute_root.

    int64_t bytes_at_realization = 0; // Size of the region computed at the store_at site, measured in bytes
    int64_t bytes_at_production = 0; // Size of the region computed at the compute_at site, measured in bytes
    int64_t bytes_at_root = 0; // The same at root, regardless of where it's actually scheduled
    int64_t innermost_bytes_at_realization = 0;
    int64_t innermost_bytes_at_production = 0;
    int64_t innermost_bytes_at_root = 0;

    int64_t inlined_calls = 0; // For inlined Funcs, how many calls are made to this Func total

    // Logically these features should be grouped earlier, but the convnet currently doesn't know about them
    int64_t unique_bytes_read_per_realization = 0; // Number of unique bytes loaded from all inputs per production
    int64_t unique_lines_read_per_realization = 0; // Number of unique contiguous segments of memory loaded from all inputs per production
    int64_t allocation_bytes_read_per_realization = 0; // The sum of the sizes of the allocations accessed. Gives a hint as to the likely locality of it.

    int64_t working_set = 0; // The sum of the sizes of the allocations within the production of this Func. Probably a good thing if it fits in cache.

    int64_t vector_size = 0; // The vectorization factor (#simd lanes) to be used to compute this stage. Wasted work if it's smaller than the stage's native vector size (which is in the pipeline features).

    int native_vector_size = 0; // The native vector size for the narrowest type used.

    int64_t num_vectors = 0; // Number of vectors computed (Assuming sliding worked)
    int64_t scalar_loads_per_vector = 0;
    int64_t vector_loads_per_vector = 0;

    void dump() const {
        debug(0) << "    num_realizations:                      " << num_realizations << '\n'
                 << "    num_productions:                       " << num_productions << '\n'
                 << "    points_computed_per_realization:       " << points_computed_per_realization << '\n'
                 << "    points_computed_per_production:        " << points_computed_per_production << '\n'
                 << "    points_computed_total:                 " << points_computed_total << '\n'
                 << "    points_computed_minimum:               " << points_computed_minimum << '\n'
                 << "    innermost_loop_extent:                 " << innermost_loop_extent << '\n'
                 << "    innermost_pure_loop_extent:            " << innermost_pure_loop_extent << '\n'
                 << "    inner_parallelism:                     " << inner_parallelism << '\n'
                 << "    outer_parallelism:                     " << outer_parallelism << '\n'
                 << "    bytes_at_realization:                  " << bytes_at_realization << '\n'
                 << "    bytes_at_production:                   " << bytes_at_production << '\n'
                 << "    bytes_at_root:                         " << bytes_at_root << '\n'
                 << "    innermost_bytes_at_realization:        " << innermost_bytes_at_realization << '\n'
                 << "    innermost_bytes_at_production:         " << innermost_bytes_at_production << '\n'
                 << "    innermost_bytes_at_root:               " << innermost_bytes_at_root << '\n'
                 << "    inlined_calls:                         " << inlined_calls << '\n'
                 << "    unique_bytes_read_per_realization:     " << unique_bytes_read_per_realization << '\n'
                 << "    unique_lines_read_per_realization:     " << unique_lines_read_per_realization << '\n'
                 << "    allocation_bytes_read_per_realization: " << allocation_bytes_read_per_realization << '\n'
                 << "    working_set:                           " << working_set << '\n'
                 << "    vector_size:                           " << vector_size << '\n'
                 << "    native_vector_size:                    " << native_vector_size << '\n'
                 << "    num_vectors:                           " << num_vectors << '\n'
                 << "    scalar_loads_per_vector:               " << scalar_loads_per_vector << '\n'
                 << "    vector_loads_per_vector:               " << vector_loads_per_vector << '\n';

    }
};

// A specialized hash map used in this file. It can only grow, and it
// requires a perfect hash in the form of "id" and "max_id" fields on
// each key. If the keys don't all have a consistent max_id, or if you
// call make_large with the wrong max_id, you get UB. If you think
// that might be happening, uncomment the assertions below for some
// extra checking.
template<typename K, typename T, int max_small_size = 4>
class PerfectHashMap {
    using storage_type = std::vector<std::pair<const K *, T>>;

    storage_type storage;

    int occupied = 0;

    // Equivalent to storage[i], but broken out into a separate method
    // to allow for bounds checks when debugging this.
    pair<const K *, T> &storage_bucket(int i) {
        /*
        internal_assert(i >= 0 && i < (int)storage.size())
            << "Out of bounds access: " << i << " " << storage.size() << "\n";
        */
        return storage[i];
    }

    const pair<const K *, T> &storage_bucket(int i) const {
        /*
        internal_assert(i >= 0 && i < (int)storage.size())
            << "Out of bounds access: " << i << " " << storage.size() << "\n";
        */
        return storage[i];
    }

    enum {
        Empty = 0, // No storage allocated
        Small = 1, // Storage is just an array of key/value pairs
        Large = 2  // Storage is an array with empty slots, indexed by the 'id' field of each key
    } state = Empty;

    void upgrade_from_empty_to_small() {
        storage.resize(max_small_size);
        state = Small;
    }

    void upgrade_from_empty_to_large(int n) {
        storage.resize(n);
        state = Large;
    }

    void upgrade_from_small_to_large(int n) {
        internal_assert(occupied <= max_small_size) << occupied << " " << max_small_size << "\n";
        storage_type tmp(n);
        state = Large;
        tmp.swap(storage);
        int o = occupied;
        for (int i = 0; i < o; i++) {
            emplace_large(tmp[i].first, std::move(tmp[i].second));
        }
    }

    // Methods when the map is in the empty state
    T &emplace_empty(const K *n, T &&t) {
        upgrade_from_empty_to_small();
        storage_bucket(0).first = n;
        storage_bucket(0).second = std::move(t);
        occupied = 1;
        return storage_bucket(0).second;
    }

    const T &get_empty(const K *n) const {
        internal_error << "Calling get on an empty PerfectHashMap";
        return unreachable_value();
    }

    T &get_empty(const K *n) {
        internal_error << "Calling get on an empty PerfectHashMap";
        return unreachable_value();
    }

    T &get_or_create_empty(const K *n) {
        occupied = 1;
        return emplace_empty(n, T());
    }

    bool contains_empty(const K *n) const {
        return false;
    }

    // Methods when the map is in the small state
    int find_index_small(const K *n) const {
        int i;
        for (i = 0; i < (int)occupied; i++) {
            if (storage_bucket(i).first == n) return i;
        }
        return i;
    }

    T &emplace_small(const K *n, T &&t) {
        int idx = find_index_small(n);
        if (idx >= max_small_size) {
            upgrade_from_small_to_large((int)(n->max_id));
            return emplace_large(n, std::move(t));
        }
        auto &p = storage_bucket(idx);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        p.second = std::move(t);
        return p.second;
    }

    const T &get_small(const K *n) const {
        int idx = find_index_small(n);
        return storage_bucket(idx).second;
    }

    T &get_small(const K *n) {
        int idx = find_index_small(n);
        return storage_bucket(idx).second;
    }

    T &get_or_create_small(const K *n) {
        int idx = find_index_small(n);
        if (idx >= max_small_size) {
            upgrade_from_small_to_large((int)(n->max_id));
            return get_or_create_large(n);
        }
        auto &p = storage_bucket(idx);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        return p.second;
    }

    bool contains_small(const K *n) const {
        int idx = find_index_small(n);
        return (idx < max_small_size) && (storage_bucket(idx).first == n);
    }

    // Methods when the map is in the large state
    T &emplace_large(const K *n, T &&t) {
        auto &p = storage_bucket(n->id);
        if (!p.first) occupied++;
        p.first = n;
        p.second = std::move(t);
        return p.second;
    }

    const T &get_large(const K *n) const {
        return storage_bucket(n->id).second;
    }

    T &get_large(const K *n) {
        return storage_bucket(n->id).second;
    }

    T &get_or_create_large(const K *n) {
        auto &p = storage_bucket(n->id);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        return storage_bucket(n->id).second;
    }

    bool contains_large(const K *n) const {
        return storage_bucket(n->id).first != nullptr;
    }

    void check_key(const K *n) const {
        /*
        internal_assert(n->id >= 0 && n->id < n->max_id)
            << "Invalid hash key: " << n->id << " " << n->max_id << "\n";
        internal_assert(state != Large || (int)storage.size() == n->max_id)
            << "Inconsistent key count: " << n->max_id << " vs " << storage.size() << "\n";
        */
    }

    // Helpers used to pacify compilers
    T &unreachable_value() {
        return storage_bucket(0).second;
    }

    const T &unreachable_value() const {
        return storage_bucket(0).second;
    }

public:

    // Jump straight to the large state
    void make_large(int n) {
        if (state == Empty) upgrade_from_empty_to_large(n);
        else if (state == Small) upgrade_from_small_to_large(n);
    }

    T &emplace(const K *n, T &&t) {
        check_key(n);
        switch(state) {
        case Empty: return emplace_empty(n, std::move(t));
        case Small: return emplace_small(n, std::move(t));
        case Large: return emplace_large(n, std::move(t));
        }
        return unreachable_value();
    }

    T &insert(const K *n, const T &t) {
        check_key(n);
        T tmp(t);
        switch(state) {
        case Empty: return emplace_empty(n, std::move(tmp));
        case Small: return emplace_small(n, std::move(tmp));
        case Large: return emplace_large(n, std::move(tmp));
        }
        return unreachable_value();
    }

    const T &get(const K *n) const {
        check_key(n);
        switch(state) {
        case Empty: return get_empty(n);
        case Small: return get_small(n);
        case Large: return get_large(n);
        }
        return unreachable_value();
    }

    T &get(const K *n) {
        check_key(n);
        switch(state) {
        case Empty: return get_empty(n);
        case Small: return get_small(n);
        case Large: return get_large(n);
        }
        return unreachable_value();
    }

    T &get_or_create(const K *n) {
        check_key(n);
        switch(state) {
        case Empty: return get_or_create_empty(n);
        case Small: return get_or_create_small(n);
        case Large: return get_or_create_large(n);
        }
        return unreachable_value();
    }

    bool contains(const K *n) const {
        check_key(n);
        switch(state) {
        case Empty: return contains_empty(n);
        case Small: return contains_small(n);
        case Large: return contains_large(n);
        }
        return false; // Unreachable
    }

    size_t size() const {
        return occupied;
    }

    struct iterator {
        pair<const K *, T> *iter, *end;

        void operator++(int) {
            do {
                iter++;
            } while (iter != end && iter->first == nullptr);
        }

        const K *key() const {
            return iter->first;
        }

        T &value() const {
            return iter->second;
        }

        bool operator!=(const iterator &other) const {
            return iter != other.iter;
        }

        bool operator==(const iterator &other) const {
            return iter == other.iter;
        }
    };

    struct const_iterator {
        const pair<const K *, T> *iter, *end;

        void operator++(int) {
            do {
                iter++;
            } while (iter != end && iter->first == nullptr);
        }

        const K *key() const {
            return iter->first;
        }

        const T &value() const {
            return iter->second;
        }

        bool operator!=(const const_iterator &other) const {
            return iter != other.iter;
        }

        bool operator==(const const_iterator &other) const {
            return iter == other.iter;
        }
    };

    iterator begin() {
        if (state == Empty) return end();
        iterator it;
        it.iter = storage.data();
        it.end = it.iter + storage.size();
        if (it.key() == nullptr) it++;
        internal_assert(it.iter == it.end || it.key());
        return it;
    }

    iterator end() {
        iterator it;
        it.iter = it.end = storage.data() + storage.size();
        return it;
    }

    const_iterator begin() const {
        if (storage.empty()) return end();
        const_iterator it;
        it.iter = storage.data();
        it.end = it.iter + storage.size();
        if (it.key() == nullptr) it++;
        internal_assert(it.iter == it.end || it.key());
        return it;
    }

    const_iterator end() const {
        const_iterator it;
        it.iter = it.end = storage.data() + storage.size();
        return it;
    }
};

template<typename T>
using NodeMap = PerfectHashMap<FunctionDAG::Node, T>;

template<typename T>
using StageMap = PerfectHashMap<FunctionDAG::Node::Stage, T>;

// We're going to do a tree search over possible schedules to find an
// optimal one. A tree search requires a state, and a function that
// gives you children of the state (with costs). The following struct
// represents the state, which is a partial schedule.
//
// A partial schedule is a tree. Each node is some portion of the for
// loop nest of some Func. If there are no children, it's the
// innermost set of loops. If there are children, it's a loop over
// tiles of that Func.
struct LoopNest {
    mutable RefCount ref_count;

    // The size of the outer loop, and the split factor used to create
    // the inner loop. Put another way, the number of tiles, and the
    // size of each tile.
    vector<int64_t> size, split_factor;

    // The nodes inside the loop body
    vector<IntrusivePtr<const LoopNest>> children;

    // Funcs inlined into this inner loop, and the number of times they are called. Only valid if children is empty.
    NodeMap<int64_t> inlined;

    // Funcs realized inside this inner loop
    set<const FunctionDAG::Node *> store_at;

    // The total bounds required of the given Func for one
    // representative iteration of this loop. Computed lazily and
    // cached. entries are immutable so that bounds are shared across
    // different instances.
    mutable NodeMap<Bound> bounds;

    const FunctionDAG::Node *node = nullptr;
    const FunctionDAG::Node::Stage *stage = nullptr;
    int stage_idx = 0;

    // Is this the innermost loop of this func?
    bool innermost = false;

    // Are we permitted to tile this loop?
    bool tileable = false;

    // What dimension is this Func vectorized over, in terms of the args of the Func?
    int vector_dim = -1;

    // Which loop corresponds to the innermost storage dimension and will be vectorized. -1 means none of them.
    int vectorized_loop_index = -1;

    void copy_from(const LoopNest &n) {
        size = n.size;
        children = n.children;
        inlined = n.inlined;
        store_at = n.store_at;
        bounds = n.bounds;
        node = n.node;
        stage = n.stage;
        stage_idx = n.stage_idx;
        innermost = n.innermost;
        tileable = n.tileable;
        vector_dim = n.vector_dim;
        vectorized_loop_index = n.vectorized_loop_index;
    };

    static void hash_combine(uint64_t &h, uint64_t next) {
        // From boost
        h ^= (next + 0x9e3779b9 + (h<<6) + (h>>2));
    }

    // Hash the loop structure and sizes up to a fixed depth
    void structural_hash(uint64_t &h, int depth, int parallelism) const {
        if (depth < 0) return;

        // Which Funcs are store_at this level?
        for (const auto *n : store_at) {
            hash_combine(h, n->id);
        }

        hash_combine(h, -1);

        // Which Funcs are compute_at this level?
        for (const auto &c : children) {
            hash_combine(h, c->stage->id);
        }

        // Add a barrier to ensure that moving something from the last
        // compute_at to the first inlined doesn't result in the same
        // hash.
        hash_combine(h, -1);

        // Which Funcs are inlined at this level?
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            hash_combine(h, it.key()->id);
        }

        hash_combine(h, -1);

        if (depth > 0) {
            // What are their loop sizes?
            for (const auto &c : children) {
                for (int64_t s : c->size) {
                    if (depth == 1) {
                        // Just take the most significant bit: is it more
                        // or less than the parallelism factor.
                        s = s >= parallelism ? 1 : 0;
                    }
                    hash_combine(h, s);
                }
            }
        }

        if (innermost) {
            // Which dimension are we vectorized over?
            hash_combine(h, vectorized_loop_index);
        }

        if (depth > 1) {
            // Descend into children
            for (const auto &c : children) {
                c->structural_hash(h, depth - 2, parallelism);
            }
        }
    }

    size_t funcs_realized_or_inlined() const {
        size_t count = inlined.size() + store_at.size();
        for (auto c : children) {
            count += c->funcs_realized_or_inlined();
        }
        return count;
    }

    struct Sites {
        const LoopNest *compute = nullptr, *store = nullptr, *produce = nullptr, *innermost = nullptr;
    };

    void get_sites(NodeMap<Sites> &sites,
                   const LoopNest *parent = nullptr) const {
        for (auto c : children) {
            c->get_sites(sites, this);
        }
        if (parent && node != parent->node) {
            auto &s = sites.get_or_create(node);
            s.compute = parent;
            s.produce = this;
        }
        for (auto f : store_at) {
            sites.get_or_create(f).store = this;
        }
        if (innermost) {
            sites.get_or_create(node).innermost = this;
        }
    }

    void compute_features(const MachineParams &params,
                          const NodeMap<Sites> &sites,
                          int64_t instances,
                          int64_t parallelism,
                          const LoopNest *parent,
                          const LoopNest &root,
                          int64_t *working_set,
                          StageMap<ScheduleFeatures> *features) const {

        int64_t working_set_here = 0;

        int64_t loop_instances = 1, parallel_loop_instances = 1;
        size_t idx = 0;
        bool in_impure = false;
        for (auto i : size) {
            loop_instances *= i;
            if (stage->loop[idx++].pure && !in_impure) {
                parallel_loop_instances *= i;
            } else {
                in_impure = true;
            }
        }
        int64_t subinstances = instances * loop_instances;

        for (const auto *node : store_at) {
            // Figure out the features at the store_at level
            const auto &bounds = get_bounds(node);

            for (size_t s = 0; s < node->stages.size(); s++) {
                // TODO: Lift invariants from this loop. Most of it's the same for every stage.
                ScheduleFeatures &feat = features->get_or_create(&(node->stages[s]));

                feat.num_realizations = subinstances;

                feat.points_computed_per_realization = 1;
                feat.num_vectors = subinstances;
                for (int i = 0; i < (int)node->stages[s].loop.size(); i++) {
                    const auto &p = bounds->loops(s, i);
                    int64_t extent = p.second - p.first + 1;
                    feat.points_computed_per_realization *= extent;
                    if (i == sites.get(node).produce->vectorized_loop_index) {
                        feat.num_vectors *= (extent + node->stages[s].vector_size - 1) / node->stages[s].vector_size;
                    } else {
                        feat.num_vectors *= extent;
                    }
                }
                feat.points_computed_total = feat.points_computed_per_realization * feat.num_realizations;

                feat.bytes_at_realization = node->bytes_per_point;
                for (int i = 0; i < node->func.dimensions(); i++) {
                    const auto &p = bounds->region_computed(i);
                    feat.bytes_at_realization *= (p.second - p.first) + 1;
                }
                int64_t innermost_storage_extent = 1;
                int v = sites.get(node).produce->vector_dim;
                if (v >= 0) {
                    innermost_storage_extent = (bounds->region_computed(v).second -
                                                bounds->region_computed(v).first + 1);
                }
                feat.innermost_bytes_at_realization = node->bytes_per_point * innermost_storage_extent;
            }
        }

        if (is_root()) {
            for (auto c : children) {
                c->compute_features(params, sites, subinstances, parallelism, this, root, &working_set_here, features);
            }

            // Figure out the root-level features for every Func
            for (auto it = features->begin(); it != features->end(); it++) {
                const auto *stage = it.key();
                const auto *node = stage->node;
                auto &feat = it.value();
                const auto &root_bounds = root.get_bounds(node);

                feat.bytes_at_root = node->bytes_per_point;
                for (int i = 0; i < node->func.dimensions(); i++) {
                    const auto &p = root_bounds->region_computed(i);
                    feat.bytes_at_root *= (p.second - p.first) + 1;
                }

                // What innermost storage extent means for inlined
                // Funcs is unclear, because we haven't selected which
                // storage dimension is innermost.
                auto *p = sites.get(node).produce;
                if (p) {
                    int64_t innermost_storage_extent = 1;
                    int v = p->vector_dim;
                    if (v >= 0) {
                        innermost_storage_extent = root_bounds->region_computed(v).second - root_bounds->region_computed(v).first + 1;
                    }
                    feat.innermost_bytes_at_root = node->bytes_per_point * innermost_storage_extent;
                } else {
                    feat.innermost_bytes_at_root = 0;
                }

                feat.points_computed_minimum = 1;
                int s = stage - &node->stages[0];
                for (int i = 0; i < (int)stage->loop.size(); i++) {
                    const auto &p = root_bounds->loops(s, i);
                    feat.points_computed_minimum *= (p.second - p.first + 1);
                }

                if (node->stages.size() == 1 && !node->is_output) {
                    int64_t points_computed_minimum_if_inlined = 0;
                    for (auto *e : node->outgoing_edges) {
                        points_computed_minimum_if_inlined += features->get(&(e->consumer->stages[e->consumer_stage])).points_computed_minimum * e->calls;
                    }
                    feat.points_computed_minimum = std::min(feat.points_computed_minimum, points_computed_minimum_if_inlined);
                }
            }

            return;
        }

        int64_t parallel_tasks = parent->is_root() ? parallel_loop_instances : 1;
        int64_t subparallelism = parallel_tasks * parallelism;

        // Figure out the features at the compute_at level
        ScheduleFeatures &feat = features->get_or_create(stage);

        if (!innermost) {
            // These will get progressively overwritten as we visit the children
            feat.innermost_loop_extent = size.empty() ? 1 : size[0];
            feat.innermost_pure_loop_extent = size.empty() ? 1 : size[vectorized_loop_index];
        }

        const bool at_production = parent->node != node;
        const bool at_pure_production = at_production && stage_idx == 0;

        if (at_production) {
            feat.num_productions = instances;
            feat.inner_parallelism = parallel_tasks;
            feat.outer_parallelism = parallelism;
            feat.vector_size = stage->vector_size;
            feat.native_vector_size = stage->vector_size;

            const auto &bounds = parent->get_bounds(node);

            feat.bytes_at_production = node->bytes_per_point;
            for (int i = 0; i < node->func.dimensions(); i++) {
                const auto &p = bounds->region_computed(i);
                feat.bytes_at_production *= (p.second - p.first) + 1;
            }
            int64_t innermost_storage_extent = 1;
            if (vector_dim >= 0) {
                innermost_storage_extent = bounds->region_computed(vector_dim).second - bounds->region_computed(vector_dim).first + 1;
            }
            feat.innermost_bytes_at_production = node->bytes_per_point * innermost_storage_extent;
        }

        // Recurse inwards
        for (auto c : children) {
            c->compute_features(params, sites, subinstances, subparallelism, this, root, &working_set_here, features);
        }

        if (at_production) {
            for (const auto *node : store_at) {
                working_set_here += features->get(&(node->stages[0])).bytes_at_production;
            }
            // TODO: This seems like it would mask off allocations just inside an inner loop
            feat.working_set = working_set_here;
        }

        *working_set += working_set_here;

        int64_t bytes_loaded = 0, lines_loaded = 0, allocation_bytes_loaded = 0, vectors_loaded = 0, scalars_loaded = 0;
        if (innermost || at_production) {
            // Pick the site at which we will compute the footprint relationship
            const auto *consumer_store_site = innermost ? parent : sites.get(node).store;
            int consumer_instances = innermost ? instances : feat.num_realizations;

            vector<const FunctionDAG::Node *> pending;
            pending.push_back(node);
            while (!pending.empty()) {
                const auto &next = pending.back()->incoming_edges;
                pending.pop_back();
                for (const auto *e : next) {
                    if (e->consumer == node && e->consumer_stage != stage_idx) {
                        // This edge not actually connected to this stage
                        continue;
                    }

                    if (!sites.contains(e->producer)) {
                        // Producer was inlined, recursively examine
                        // its inputs. This also triggers for
                        // unscheduled Funcs, and harmlessly
                        // recursively walks all the way back to the
                        // inputs. TODO: cut this off to reduce compile times.
                        pending.push_back(e->producer);
                        continue;
                    }

                    const auto &site = sites.get(e->producer);
                    const auto *producer_compute_site = site.compute;
                    const auto *producer_store_site = site.store;
                    const auto &bounds = consumer_store_site->get_bounds(e->producer);
                    const auto &producer_compute_bounds = producer_compute_site->get_bounds(e->producer);
                    const auto &producer_store_bounds = producer_store_site->get_bounds(e->producer);
                    int64_t footprint = e->producer->bytes_per_point;
                    int64_t vector_footprint = 1;
                    int64_t compute_footprint = footprint;
                    int64_t store_footprint = footprint;
                    int64_t line_footprint = 1;
                    int64_t compute_line_footprint = 1;
                    int64_t store_line_footprint = 1;

                    bool dense_vector_loads = true;

                    for (int i = 0; i < e->producer->func.dimensions(); i++) {
                        auto p = bounds->region_required(i);
                        auto compute_p = producer_compute_bounds->region_computed(i);
                        auto store_p = producer_store_bounds->region_required(i);
                        int64_t extent = p.second - p.first + 1;
                        int64_t compute_extent = compute_p.second - compute_p.first + 1;
                        int64_t store_extent = store_p.second - store_p.first + 1;
                        footprint *= extent;
                        compute_footprint *= compute_extent;
                        store_footprint *= store_extent;

                        bool dense = (i == site.produce->vector_dim);

                        if (dense) {
                            dense_vector_loads = extent >= feat.vector_size;
                            // TODO: This is not exactly correct. The
                            // footprint can be larger than a vector
                            // without the loads being contiguous
                            // vector loads - e.g. consider a lookup
                            // into an 8-element LUT.
                            vector_footprint *= (extent + stage->vector_size - 1) / stage->vector_size;
                        } else {
                            line_footprint *= extent;
                            compute_line_footprint *= compute_extent;
                            store_line_footprint *= store_extent;
                            vector_footprint *= extent;
                        }
                    }

                    if (dense_vector_loads) {
                        vectors_loaded += vector_footprint;
                    } else {
                        scalars_loaded += footprint / e->producer->bytes_per_point;
                    }

                    int64_t store_instances_per_consumption = 1;
                    const auto &producer_feat = features->get_or_create(&(e->producer->stages[0]));

                    if (producer_feat.num_realizations) {
                        // The producer's realization is nested inside this Func's realization
                        const int64_t producer_store_instances = producer_feat.num_realizations;
                        if (producer_store_instances > consumer_instances) {
                            store_instances_per_consumption = producer_store_instances / consumer_instances;
                        }
                    }

                    allocation_bytes_loaded += compute_footprint;

                    if (store_instances_per_consumption > 1) {
                        // The producer is nested inside the consumer
                        bytes_loaded += store_footprint * store_instances_per_consumption;
                        // Due to folding, the actual buffer size is smaller than the bounds at the store level
                        lines_loaded += store_line_footprint * store_instances_per_consumption;
                    } else {
                        // The consumer is consuming some portion of a larger producer computed earlier
                        bytes_loaded += footprint;
                        lines_loaded += line_footprint;
                    }
                }
            }
        }

        if (at_production) {
            // Properties of the realization, but the values are
            // computable at the production site because that's where
            // the consumers are.
            internal_assert(bytes_loaded >= 0) << "Negative bytes loaded: " << bytes_loaded << "\n";
            feat.unique_bytes_read_per_realization = bytes_loaded;
            feat.allocation_bytes_read_per_realization = allocation_bytes_loaded;
            feat.unique_lines_read_per_realization = lines_loaded;

            if (!at_pure_production) {
                // Also pessimistically assume this update definition relies on the entirety of the produced region so far.
                // TODO: This overbills scatters, or writes to a restriction region.
                internal_assert(bytes_loaded >= 0) << "Negative bytes at production: " << feat.bytes_at_production << "\n";
                feat.unique_bytes_read_per_realization += feat.bytes_at_production;
                feat.unique_lines_read_per_realization++; // It's accessed contiguously (TODO: This is fishy. Should probably be lines_at_production)
                feat.allocation_bytes_read_per_realization += feat.bytes_at_production;
            }
        }

        if (innermost) {
            feat.points_computed_per_production = subinstances / feat.num_productions;
            // Important TODO: account for loads from input images
            feat.vector_loads_per_vector = vectors_loaded;
            feat.scalar_loads_per_vector = scalars_loaded;
        }

        // Track features for inlined Funcs
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            const auto *f = it.key();
            internal_assert(f);
            auto &inlined_feat = features->get_or_create(&(f->stages[0]));
            inlined_feat.inlined_calls += it.value() * subinstances;
            inlined_feat.native_vector_size = (int64_t)(stage->vector_size);
            if (inlined_feat.vector_size > 0) {
                inlined_feat.vector_size = std::min(inlined_feat.vector_size, (int64_t)stage->vector_size);
            } else {
                inlined_feat.vector_size = feat.vector_size;
            }
            if (inlined_feat.innermost_pure_loop_extent > 0) {
                inlined_feat.innermost_pure_loop_extent = std::min(inlined_feat.innermost_pure_loop_extent,
                                                                   feat.innermost_pure_loop_extent);
            } else {
                inlined_feat.innermost_pure_loop_extent = feat.innermost_pure_loop_extent;
            }
            inlined_feat.inner_parallelism = 1;
            inlined_feat.outer_parallelism = parallelism;
        }
    }

    bool is_root() const {
        return node == nullptr;
    }

    const Bound &set_bounds(const FunctionDAG::Node *f, BoundContents *b) const {
        return bounds.emplace(f, b);
    }

    const Bound &get_bounds(const FunctionDAG::Node *f) const {
        // debug(0) << "get_bounds of " << f.name() << " in loop over " << (is_root() ? "root" : func.name()) << '\n';
        if (bounds.contains(f)) {
            const Bound &b = bounds.get(f);
            // debug(0) << "Getting bounds of " << f->func.name() << " at site:\n";
            // dump("  ");
            b->validate();
            return b;
        }
        auto bound = f->make_bound();
        // Compute the region required
        if (f->is_output && is_root()) {
            internal_assert(f->outgoing_edges.empty()) << "Outputs that access other outputs not yet supported\n";
            // It's an output.
            // Use the bounds estimate
            for (int i = 0; i < f->func.dimensions(); i++) {
                bound->region_required(i) = f->estimated_region_required[i];
            }
        } else {
            internal_assert(!f->outgoing_edges.empty())
                << "No consumers of " << f->func.name()
                << " at loop over " << (is_root() ? "root" : node->func.name()) << '\n';
            std::pair<int64_t, int64_t> init {INT64_MAX, INT64_MIN};
            for (int i = 0; i < f->func.dimensions(); i++) {
                bound->region_required(i) = init;
            }
            for (const auto *e : f->outgoing_edges) {
                // Ignore consumers outside of this loop nest
                if (!computes(e->consumer)) {
                    continue;
                }
                const auto &c_bounds = get_bounds(e->consumer);
                // debug(0) << "Expanding footprint along edge " << e->producer->func.name() << " -> " << e->consumer->func.name() << "\n";
                const auto *consumer_loop = &(c_bounds->loops(e->consumer_stage, 0)); // For the concrete sizes of the loop
                e->expand_footprint(consumer_loop, &(bound->region_required(0)));
            }
        }

        f->required_to_computed(&(bound->region_required(0)), &(bound->region_computed(0)));

        for (int i = 0; i < (int)f->stages.size(); i++) {
            f->loop_nest_for_region(i, &(bound->region_computed(0)), &(bound->loops(i, 0)));
        }

        const Bound &b = set_bounds(f, bound);
        b->validate();
        return b;
    }

    void dump(string prefix) const {
        if (!is_root()) {
            debug(0) << prefix << node->func.name();
            prefix += " ";
        }
        for (size_t i = 0; i < size.size(); i++) {
            debug(0) << " " << size[i];
            if (innermost && i == vectorized_loop_index) {
                debug(0) << 'v';
            }
        }
        debug(0) << " (" << vectorized_loop_index << ")";
        if (tileable) {
            debug(0) << " t";
        }
        if (innermost) {
            debug(0) << " *\n";
        } else {
            debug(0) << '\n';
        }
        for (auto p : store_at) {
            debug(0) << prefix << "realize: " << p->func.name() << '\n';
        }
        for (size_t i = children.size(); i > 0; i--) {
            children[i-1]->dump(prefix);
        }
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            debug(0) << prefix << "inlined: " << it.key()->func.name() << " " << it.value() << '\n';
        }
        /*
          for (auto p : bounds) {
          debug(0) << prefix << "bounds: " << p.first.name();
          for (auto d : p.second.region) {
          debug(0) << " [" << d.first << ", " << d.second << "]";
          }
          debug(0) << '\n';
          }
        */
    }

    bool calls(const FunctionDAG::Node *f) const {
        for (const auto &c : children) {
            if (c->calls(f)) return true;
        }
        for (const auto *e : f->outgoing_edges) {
            if (e->consumer == node && e->consumer_stage == stage_idx) {
                return true;
            }
            if (inlined.contains(e->consumer)) {
                return true;
            }
        }
        return false;
    }

    int64_t max_inlined_calls() const {
        int64_t result = 0;
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            result = std::max(result, it.value());
        }
        for (const auto &c : children) {
            result = std::max(result, c->max_inlined_calls());
        }
        return result;
    }

    bool accesses_input_buffer() const {
        for (const auto &c : children) {
            if (c->accesses_input_buffer()) return true;
        }
        if (is_root()) return false;

        auto check = [&](const FunctionDAG::Node *n) {
            for (const auto &s : n->stages) {
                for (int t = 0; t < (int)PipelineFeatures::ScalarType::NumScalarTypes; t++) {
                    if (s.features.op_histogram[(int)PipelineFeatures::OpType::ImageCall][t] > 0) return true;
                }
            }
            return false;
        };

        if (check(node)) return true;
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            if (check(it.key())) return true;
        }
        return false;
    }

    bool computes(const FunctionDAG::Node *f) const {
        if (f == node) {
            return true;
        }
        if (inlined.contains(f)) {
            return true;
        }
        for (const auto &c : children) {
            if (c->computes(f)) return true;
        }
        return false;
    }

    void inline_func(const FunctionDAG::Node *f) {
        // Inline it into the children
        for (size_t i = 0; i < children.size(); i++) {
            if (children[i]->calls(f)) {
                std::unique_ptr<LoopNest> new_child{new LoopNest};
                new_child->copy_from(*children[i]);
                new_child->inline_func(f);
                children[i] = new_child.release();
            }
        }

        // Inline it here if there are any direct calls
        if (innermost) {
            int64_t calls = 0;
            for (const auto *e : f->outgoing_edges) {
                if (inlined.contains(e->consumer)) {
                    calls += inlined.get(e->consumer) * e->calls;
                }
                if (e->consumer == node) {
                    calls += e->calls;
                }
            }
            if (calls) {
                inlined.insert(f, calls);
            }
        }
    }

    void compute_here(const FunctionDAG::Node *f, bool tileable, int vector_dim) {
        const auto &bounds = get_bounds(f);

        for (int s = (int)f->stages.size() - 1; s >= 0; s--) {
            LoopNest *node = new LoopNest;
            node->node = f;
            node->stage_idx = s;
            node->stage = &f->stages[s];
            node->innermost = true;
            node->vectorized_loop_index = -1;
            // TODO: rvars are not tileable
            node->tileable = tileable;
            // Set up a bound for the inside of the
            // loop. computed/required is still the full region, but
            // the loop nest will be a single representative point.
            auto single_point = bounds->make_copy();
            size_t loop_dim = f->stages[s].loop.size();
            node->size.resize(loop_dim);

            bool single_vector = true;
            int64_t total_extent = 1;
            for (size_t i = 0; i < loop_dim; i++) {
                const auto &l = bounds->loops(s, i);
                // Initialize the loop nest
                node->size[i] = l.second - l.first + 1;
                total_extent *= node->size[i];
                // Pick a representative loop iteration for the inner
                // loop. With the way tiling is done below, it needs
                // to be the first loop iteration.
                single_point->loops(s, i) = {l.first, l.first};

                if (f->stages[s].loop[i].var == f->func.args()[vector_dim]) {
                    node->vectorized_loop_index = (int)i;
                    int sz = node->stage->vector_size;
                    single_point->loops(s, i).second += sz - 1;
                    node->size[i] += sz - 1;
                    node->size[i] /= sz;
                }
                single_vector &= (node->size[i] == 1);
            }
            // Leave region required blank inside the computation of a Func
            node->set_bounds(f, std::move(single_point));
            node->vector_dim = vector_dim;

            if (node->vectorized_loop_index >= 0) {
                // Split off the single vector as an inner loop nest.
                node->innermost = false;

                LoopNest *one_vector = new LoopNest;
                one_vector->node      = node->node;
                one_vector->stage     = node->stage;
                one_vector->stage_idx = node->stage_idx;
                one_vector->tileable  = false;
                one_vector->vectorized_loop_index = node->vectorized_loop_index;
                one_vector->vector_dim = vector_dim;
                one_vector->size.resize(loop_dim, 1);
                one_vector->innermost = true;
                auto b = node->get_bounds(f)->make_copy();
                // Set the region computed inside this node to be the first vector lane
                b->loops(s, node->vectorized_loop_index).second = b->loops(s, node->vectorized_loop_index).first;
                one_vector->set_bounds(f, b);
                one_vector->size[node->vectorized_loop_index] = node->stage->vector_size;

                if (single_vector) {
                    children.emplace_back(one_vector);
                } else {
                    node->children.emplace_back(one_vector);
                }
            }

            if (single_vector) {
                delete node;
            } else {
                children.emplace_back(node);
            }
        }
    }

    // Return all possible ways to compute f in tiles.
    vector<IntrusivePtr<const LoopNest>> compute_in_tiles(const FunctionDAG::Node *f,
                                                          const LoopNest *parent,
                                                          const MachineParams &params,
                                                          int vector_dim,
                                                          bool in_realization) const {
        internal_assert(f);

        vector<IntrusivePtr<const LoopNest>> result;

        // Figure out which child we can fuse this into
        int child = -1;
        bool called_by_multiple_children = false;
        for (int i = 0; i < (int)children.size(); i++) {
            if (children[i]->calls(f)) {
                if (child != -1) {
                    called_by_multiple_children = true;
                }
                child = i;
            }
        }

        const int vector_size = is_root() ? 1 : stage->vector_size;

        // HACK (when true)
        const bool force_only_output_compute_root = false;

        if ((!is_root() || f->is_output || !force_only_output_compute_root) &&
            !innermost &&
            (!in_realization || size.empty() || size[vector_dim] == 1)) {
            // Place the computation inside this loop
            std::unique_ptr<LoopNest> r{new LoopNest};
            r->copy_from(*this);
            r->compute_here(f, true, vector_dim);
            if (!in_realization) {
                r->store_at.insert(f);
            } else {
                r->tileable = false;
            }
            result.emplace_back(r.release());
        }

        if (f->is_output || f->is_input) {
            // Not permitted to compute at tiles of some consumer
            return result;
        }

        if (tileable) {
            // Generate a list of tile sizes to try
            auto tilings = generate_tilings(size, (int)(size.size() - 1), 2, !in_realization, vectorized_loop_index, innermost ? vector_size : 1);

            if (tilings.size() > 1000) {
                debug(0) << "Warning: lots of tilings: " << tilings.size() << "\n";
            }

            for (auto t : tilings) {
                if (parent->is_root()) {
                    const auto &l = stage->loop;
                    // Skip root-level tilings that provide
                    // insufficient parallelism to avoid nested
                    // parallelism, and root-level tilings that would
                    // force serialization of dimensions we have
                    // decided to parallelize over in an earlier pass.
                    int total = 1;
                    size_t idx = 0;
                    for (auto s : t) {
                        if (l[idx].pure) {
                            total *= s;
                        }
                        idx++;
                    }
                    if (total < params.parallelism) continue;
                }

                // Tile this loop and place the computation at some coarser granularity
                LoopNest *inner = new LoopNest, *outer = new LoopNest;
                inner->node      = outer->node      = node;
                inner->stage     = outer->stage     = stage;
                inner->stage_idx = outer->stage_idx = stage_idx;
                inner->tileable  = outer->tileable  = tileable;
                inner->vector_dim = outer->vector_dim = vector_dim;
                inner->vectorized_loop_index = outer->vectorized_loop_index = vectorized_loop_index;
                outer->size = size;
                outer->innermost = false;

                // First make an inner loop representing a 1x1x1... tile
                inner->size.resize(size.size(), 1);
                inner->innermost = innermost;
                inner->children = children;
                inner->inlined = inlined;
                inner->bounds = bounds;
                inner->store_at = store_at;


                {
                    auto b = inner->get_bounds(node)->make_copy();

                    // Then move factors from the outer loop to the inner loop
                    auto parent_bounds = parent->get_bounds(node);

                    for (size_t i = 0; i < t.size(); i++) {
                        int factor = t[i];
                        inner->size[i] = (outer->size[i] + factor - 1) / factor;
                        outer->size[i] = factor;
                        const auto &p = parent_bounds->loops(stage_idx, i);
                        int64_t min = p.first;
                        int64_t extent = p.second - min + 1;
                        extent = (extent + factor - 1) / factor;
                        b->loops(stage_idx, i) = {min, min + extent - 1};
                    }

                    // Region_{computed/required} on outer is now
                    // wrong, but it doesn't matter because consumers
                    // only look at the loops in get_bounds. Still,
                    // this is weird.

                    if (false) {// HACK
                        // Set those values to something clearly recognizable as non-meaningful.
                        for (int i = 0; i < node->func.dimensions(); i++) {
                            // The schedule depends on these!!! Chaos! Madness!
                            b->region_required(i).first = 2020202;
                            b->region_required(i).second = -2020202;
                            b->region_computed(i).first = 2020202;
                            b->region_computed(i).second = -2020202;
                        }
                    }

                    outer->set_bounds(node, b);
                }

                if (!in_realization) {
                    outer->store_at.insert(f);
                }

                bool may_slide = (!in_realization &&
                                  f->stages.size() == 1);
                if (may_slide) {
                    // Store here, but compute further in. Currently
                    // don't have to worry about the constraints this
                    // places on parallelism, as we forced all the
                    // parallelism to the outer loop.
                    auto v = inner->compute_in_tiles(f, outer, params, vector_dim, true);
                    for (IntrusivePtr<const LoopNest> &n : v) {
                        LoopNest *store_at_outer_compute_further_in = new LoopNest;
                        store_at_outer_compute_further_in->copy_from(*outer);
                        store_at_outer_compute_further_in->children.emplace_back(std::move(n));
                        result.emplace_back(store_at_outer_compute_further_in);
                    }
                }

                // Site the computation inside the outer loop
                outer->children.emplace_back(inner);
                outer->compute_here(f, true, vector_dim);
                outer->tileable &= !in_realization;
                result.emplace_back(outer);
            }
        }

        if (child >= 0 && !called_by_multiple_children && !in_realization) {
            // Push the Func further inwards in the loop nest

            // See if it's appropriate to slide over this loop
            const vector<int64_t> &child_size = children[child]->size;
            int num_ones = 0;
            for (auto s : child_size) {
                num_ones += (s == 1) ? 1 : 0;
            }
            bool may_slide = !is_root() && (num_ones == ((int)child_size.size() - 1)) && f->stages.size() == 1;
            may_slide &= (vector_dim >= (int)child_size.size()) || (child_size[vector_dim] == 1);
            for (int store_here = 0; store_here < 2; store_here++) {
                if (store_here && !may_slide) {
                    // We place all our parallel loops at the root
                    // level, so this would constrain parallelism.
                    continue;
                }
                auto v = children[child]->compute_in_tiles(f, this, params, vector_dim, store_here);
                for (IntrusivePtr<const LoopNest> &n : v) {
                    // (Only valid if one child calls f) Push the
                    // computation into the child. Possibly leaving
                    // the storage out here.
                    LoopNest *r = new LoopNest;
                    r->copy_from(*this);
                    if (store_here) {
                        r->store_at.insert(f);
                    }
                    r->children[child] = n;
                    result.emplace_back(r);
                }
            }
        }

        return result;
    }

    struct StageScheduleState {
        double num_cores = 0; // How much parallelism do we need to exploit with this Func?
        int vector_dim = -1; // Which storage dimension is vectorized? We need to reorder it innermost.
        struct FuncVar {
            VarOrRVar orig;
            VarOrRVar var;
            string accessor;
            int64_t extent = 0;
            bool outermost = false, parallel = false, exists = false, pure = false;
            FuncVar() : orig(Var()), var(Var()) {}
        };
        vector<FuncVar> vars; // In order from innermost to outermost. Each group of d is one tiling.
        std::ostringstream schedule_source;
    };

    void apply(LoopLevel here,
               map<const FunctionDAG::Node::Stage *, StageScheduleState> &state_map,
               double num_cores,
               int depth,
               const LoopNest *parent,
               const LoopNest *compute_site) const {
        if (is_root()) {
            for (auto &c : children) {
                Func(c->node->func).compute_root();
                c->apply(LoopLevel::root(), state_map, num_cores, 1, this, c.get());
                if (c->stage_idx == 0) {
                    auto &state = state_map[c->stage];
                    state.schedule_source << "\n    .compute_root()";
                    // TODO: Omitting logic for printing store_root() assumes everything store_root is also compute root
                }
            }
        } else {
            if (parent && parent->node != node) {
                compute_site = this;
            }

            auto it = state_map.find(stage);
            const auto &symbolic_loop = stage->loop;
            const auto &parent_bounds = parent->get_bounds(node);
            if (it == state_map.end()) {
                StageScheduleState state;
                state.num_cores = num_cores;
                state.vector_dim = vector_dim;
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    StageScheduleState::FuncVar fv;
                    const auto &l = symbolic_loop[i];
                    fv.var = VarOrRVar(l.var, !l.pure);
                    fv.orig = fv.var;
                    fv.accessor = l.accessor;
                    const auto &p = parent_bounds->loops(stage_idx, i);
                    fv.extent = p.second - p.first + 1;
                    fv.outermost = true;
                    fv.parallel = parent->is_root() && l.pure;
                    fv.exists = true;
                    fv.pure = l.pure;
                    state.vars.push_back(fv);
                }
                state_map.emplace(stage, std::move(state));
            }
            auto &state = state_map[stage];

            // The getter for grabbing Func handles is reverse topological order
            Stage s = Func(node->func);
            if (stage_idx > 0) {
                s = Func(node->func).update(stage_idx - 1);
            }

            if (stage_idx == 0 && parent->node != node) {
                // Pick a memory type
                double bytes = node->bytes_per_point;
                for (int i = 0; i < node->func.dimensions(); i++) {
                    const auto &p = parent_bounds->region_computed(i);
                    bytes *= p.second - p.first + 1;
                }
                if (bytes < 64000 && depth > 2) {
                    // If it's probably a small allocation, and it's
                    // made more than once, use stack-scoped
                    // storage. Otherwise let the compiler pick heap
                    // or stack as it likes.
                    Func(node->func).store_in(MemoryType::Stack);
                    state.schedule_source << "\n    .store_in(MemoryType::Stack)";
                }
            }

            // Pick a tail strategy for any splits of pure vars. RVars always use guardwithif
            auto pure_var_tail_strategy = TailStrategy::Auto;
            if (!compute_site->accesses_input_buffer() && !node->is_output) {
                // Roundup is lowest overhead, provided it doesn't
                // expand the bounds read on the input or written on
                // the output. However, you can only really use it on
                // pure stages that don't access the input anywhere in
                // their loop nest.
                pure_var_tail_strategy = TailStrategy::RoundUp;
            } else if (stage_idx == 0) {
                // Pure stages that access the input use shiftinwards
                pure_var_tail_strategy = TailStrategy::ShiftInwards;
            } else {
                // For pure vars in update stages that access the
                // input, it's not safe to round up or redundantly
                // recompute
                pure_var_tail_strategy = TailStrategy::GuardWithIf;
            }

            if (!size.empty()) {
                if (innermost) {
                    if (vectorized_loop_index >= 0) {
                        state.schedule_source
                            << "\n    .vectorize(" << state.vars[vectorized_loop_index].var.name() << ")";
                        s.vectorize(state.vars[vectorized_loop_index].var);
                    }
                } else {
                    // This is not the innermost loop. Go find the next inner loop.
                    const LoopNest *child = nullptr;
                    for (const auto &c : children) {
                        if (c->node == node) {
                            child = c.get();
                            break;
                        }
                    }

                    // Do the implied splits
                    vector<StageScheduleState::FuncVar> new_inner;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
                        StageScheduleState::FuncVar v;
                        StageScheduleState::FuncVar &parent = state.vars[i];

                        int64_t factor = (parent.extent + size[i] - 1) / size[i];

                        if (child && child->size[i] > factor) {
                            // We may have picked a
                            // larger-that-minimal extent for the
                            // child loop, for the purpose of
                            // vectorization.
                            factor = child->size[i];
                        }

                        if (!parent.exists || parent.extent == 1 || factor == 1) {
                            v.exists = false;
                            v.extent = 1;
                        } else if (size[i] == 1) {
                            // Not split in this dimension
                            v = parent;
                            v.parallel = false;
                            parent.exists = false;
                            parent.extent = 1;
                        } else {
                            VarOrRVar inner(Var(parent.var.name() + "i"));
                            if (parent.var.is_rvar) {
                                inner = RVar(parent.var.name() + "i");
                            }

                            auto tail_strategy = pure_var_tail_strategy;
                            // If it's an RVar, or not the outermost split and we're in an update, we need a guard with if instead.
                            if (parent.var.is_rvar || (stage_idx != 0 && !parent.outermost)) {
                                tail_strategy = TailStrategy::GuardWithIf;
                            }
                            s.split(parent.var, parent.var, inner, (int)factor, tail_strategy);
                            state.schedule_source
                                << "\n    .split("
                                << parent.var.name() << ", "
                                << parent.var.name() << ", "
                                << inner.name() << ", "
                                << factor << ", "
                                << "TailStrategy::" << tail_strategy << ")";
                            v = parent;
                            parent.extent = size[i];
                            v.var = inner;
                            v.accessor.clear();
                            v.extent = factor;
                            v.parallel = false;
                            v.outermost = false;
                        }
                        new_inner.push_back(v);
                    }

                    if (child->innermost) {
                        // Maybe do some unrolling

                        int64_t product_of_pure_loops = 1;
                        for (size_t i = 0; i < symbolic_loop.size(); i++) {
                            if (symbolic_loop[i].pure) {
                                product_of_pure_loops *= state.vars[i].extent;
                            }
                        }

                        // Temporary hack until we can actually model
                        // which loops are constant size. The other part
                        // of this hack is that we changed the unrolling
                        // pass to not complain if things are not
                        // constant.
                        bool all_pure_loops_constant_size = true;

                        if (product_of_pure_loops <= 16 && all_pure_loops_constant_size) {
                            // There's a hope we can fit anything compute-at this level into registers if we fully unroll
                            // TODO: 16 should be the number of vector registers in the architecture
                            std::stable_sort(state.vars.begin(), state.vars.begin() + symbolic_loop.size(),
                                             [](const StageScheduleState::FuncVar &a, const StageScheduleState::FuncVar &b) {
                                                 return a.pure && !b.pure;
                                             });

                            for (size_t i = 0; i < symbolic_loop.size(); i++) {
                                if (state.vars[i].pure && state.vars[i].exists && state.vars[i].extent > 1) {
                                    s.unroll(state.vars[i].var);
                                    state.schedule_source << "\n    .unroll(" << state.vars[i].var.name() << ")";
                                }
                            }
                        }
                    }

                    bool found = false;
                    for (const auto &v : state.vars) {
                        if (!v.exists) continue;
                        here = LoopLevel(node->func, v.var);
                        found = true;
                        break;
                    }
                    internal_assert(found) << "Could not find appropriate compute_at location for children of " << node->func.name() << "\n";
                    state.vars.insert(state.vars.begin(), new_inner.begin(), new_inner.end());
                }
            }
            if (innermost) {
                internal_assert(store_at.empty());
                internal_assert(children.empty());
                return;
            }


            for (auto f : store_at) {
                Func(f->func).store_at(here);
            }
            for (auto s : size) {
                num_cores /= s;
            }
            here.lock();
            string loop_level;
            if (here.is_root()) {
                loop_level = "_root()";
            } else {
                loop_level = "_at(" + here.func() + ", " + here.var().name() + ")";
            }
            for (auto &c : children) {
                if (c->node != node) {
                    Func(c->node->func).compute_at(here);
                }
                c->apply(here, state_map, num_cores, depth + 1, this, compute_site);
                if (c->node != node && c->stage_idx == 0) {
                    auto &state = state_map[c->stage];
                    state.schedule_source << "\n    .compute" << loop_level;
                }
            }
            for (auto f : store_at) {
                bool computed_here = false;
                for (auto &c : children) {
                    if (c->node == f) {
                        computed_here = true;
                        break;
                    }
                }
                if (!computed_here) {
                    auto &state = state_map[&(f->stages[0])];
                    state.schedule_source << "\n    .store" << loop_level;
                }
            }
        }
    }

};

}

template<>
RefCount &ref_count<LoopNest>(const LoopNest *t) {return t->ref_count;}

template<>
void destroy<LoopNest>(const LoopNest *t) {delete t;}

namespace {

struct State {
    mutable RefCount ref_count;
    IntrusivePtr<const LoopNest> root;
    IntrusivePtr<const State> parent;
    double cost = 0;
    int num_funcs_scheduled = 0;
    bool penalized = false;

    static int cost_calculations;

    uint64_t structural_hash(int depth, int parallelism) const {
        uint64_t h = num_funcs_scheduled;
        internal_assert(root.defined());
        root->structural_hash(h, depth, parallelism);
        return h;
    }

    void compute_featurization(const FunctionDAG &dag, const MachineParams &params, StageMap<ScheduleFeatures> *features) {
        NodeMap<LoopNest::Sites> sites;
        sites.make_large(dag.nodes.size());
        features->make_large(dag.nodes[0].stages[0].max_id);
        internal_assert(root.defined());
        root->get_sites(sites);
        root->compute_features(params, sites, 1, 1, nullptr, *root, nullptr, features);
    }

    void save_featurization(const FunctionDAG &dag, const MachineParams &params, const std::string &feature_file) {
        StageMap<ScheduleFeatures> features;
        compute_featurization(dag, params, &features);

        std::ofstream binfile(feature_file, std::ios::binary | std::ios_base::trunc);
        for (const auto &n : dag.nodes) {
            for (size_t stage_idx = n.stages.size(); stage_idx > 0; stage_idx--) {
                const auto &s = n.stages[stage_idx - 1];
                const size_t num_schedule_features = sizeof(ScheduleFeatures) / sizeof(int64_t);
                const size_t num_pipeline_features = sizeof(PipelineFeatures) / sizeof(int);
                const auto &sched_feat = features.get(&s);
                const int64_t *sched_ints = (const int64_t *)(&sched_feat);
                const int *pipe_ints = (const int *)(&s.features);

                float buf[num_schedule_features + num_pipeline_features];
                // Save them as floats
                for (size_t i = 0; i < num_schedule_features; i++) {
                    buf[i] = sched_ints[i];
                }

                for (size_t i = 0; i < num_pipeline_features; i++) {
                    buf[i + num_schedule_features] = pipe_ints[i];
                }

                binfile.write((const char *)buf, sizeof(buf));
            }
        }
        binfile.close();
    }

    bool calculate_cost(const FunctionDAG &dag, const MachineParams &params, ThroughputPredictorPipeline *throughput_predictor, bool verbose = false) {
        StageMap<ScheduleFeatures> features;
        compute_featurization(dag, params, &features);

        cost = 0;

        if (verbose) {
            for (auto it = features.begin(); it != features.end(); it++) {
                auto &stage = *(it.key());
                const auto &feat = it.value();
                debug(0) << "Schedule features for " << stage.stage.name() << "\n";
                feat.dump();
            }
        }

        // use either deep network or linear model to predict cost
        if (throughput_predictor) {

            // Perform any quick rejection tests before enqueuing this

            // TODO: allow massive recompute for stages that are no more expensive than a load from them
            /*
            for (auto it = features.begin(); it != features.end(); it++) {
                auto &feat = it.value();
                if (feat.points_computed_total + feat.inlined_calls > 10 * feat.points_computed_minimum) {
                    cost = 1e50;
                    return true;
                }
            }
            */

            // Avoid code size explosion from recursive inlining.
            if (root->max_inlined_calls() > 100) {
                cost = 1e50;
                return true;
            }

            int num_stages = (int)features.size();

            const int schedule_feat_size = 26;

            Runtime::Buffer<float> schedule_features;

            // Won't actually run anything until we call evaluate_costs...
            throughput_predictor->enqueue(num_stages, &schedule_features, &cost);

            // index of current stage whose features we are reading
            int stage = 0;
            // load schedule features into input buffer
            for (const auto &n : dag.nodes) {
                if (stage >= num_stages) break;
                for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
                    internal_assert(features.contains(&*it));
                    const auto &feat = features.get(&*it);
                    const int64_t *sched_stats = (const int64_t *)(&feat);
                    for (int i = 0; i < schedule_feat_size; i++) {
                        schedule_features(i, stage) = sched_stats[i];
                    }

                    stage += 1;
                }
            }
            internal_assert(stage == num_stages);
        } else {
            // We have no throughput predictor.
            for (auto it = features.begin(); it != features.end(); it++) {
                auto &stage = *(it.key());
                const auto &feat = it.value();
                // Reject silly schedules. They're not even useful for
                // training data, as they potentially take the age of
                // the universe to benchmark. We define 'silly' as
                // doing more than 10x redundant recompute for any one
                // stage.
                //if (feat.points_computed_total + feat.inlined_calls > 10*feat.points_computed_minimum) return false;

                double compute_cost = 0;
                const int *pipeline_feat = (const int *)(&stage.features.op_histogram[0][0]);
                double per_element_compute_cost = 0;
                for (size_t i = 0; i < sizeof(stage.features.op_histogram) / sizeof(int); i++) {
                    per_element_compute_cost += pipeline_feat[i];
                }

                // Assume that narrow types are cheaper because they vectorize wider.
                compute_cost *= 8.0 / feat.native_vector_size; // Relative to fp32

                compute_cost = per_element_compute_cost * feat.points_computed_total;

                // Figure out vector overcompute
                const int native_vector_size = feat.native_vector_size;
                const double idle_simd_lanes = (double)native_vector_size / feat.vector_size;

                // Inlining saves a call node, which in our cost
                // model costs...
                const double per_element_compute_cost_of_memcpy = 1 + 2*stage.node->func.dimensions();
                const double per_element_compute_cost_inlined = std::max(0.0, per_element_compute_cost - per_element_compute_cost_of_memcpy);
                const double compute_cost_inlined = per_element_compute_cost_inlined * feat.inlined_calls;
                compute_cost += compute_cost_inlined;
                compute_cost *= idle_simd_lanes;

                {
                    // Few parallel tasks may be a bad idea due to
                    // waiting for the long pole to finish.  Say
                    // we have a huge number of tasks relative to
                    // cores. We'd expect their start times to
                    // eventually become evenly spaced, which
                    // means we get a little triangle of idle
                    // cores with total area 0.5 * task_size *
                    // num_cores at the end. This bloats the total
                    // amount of work by:
                    //   (0.5 * task_size * num_cores + task_size * num_tasks) / (task_size * num_tasks)
                    // = (0.5 * num_cores + num_tasks) / num_tasks

                    internal_assert(feat.inner_parallelism > 0 && feat.outer_parallelism > 0);

                    const double num_tasks = feat.inner_parallelism;
                    const double num_cores = (double)params.parallelism / feat.outer_parallelism;
                    double idle_core_wastage = (0.5 * num_cores + num_tasks) / num_tasks;

                    // Evaluated at num_tasks = num_cores, this
                    // gives a ridiculous 1.5x multiplier. Our
                    // argument doesn't hold because the tasks
                    // start synchronized. Just cap it at 20%
                    // wastage.
                    idle_core_wastage = std::min(idle_core_wastage, 1.2);

                    if (verbose) {
                        debug(0) << "idle_core_wastage_1 = " << idle_core_wastage << "\n";
                    }

                    // Cores can also be idle if the number of
                    // tasks is small and not a multiple of the
                    // number of cores. E.g. 9 tasks on 8 cores
                    // takes about the same amount of time as 16
                    // tasks.
                    idle_core_wastage *= std::ceil(num_tasks / num_cores) * (num_cores / num_tasks);

                    compute_cost *= idle_core_wastage;

                    if (verbose) {
                        debug(0) << "idle_core_wastage_2 = " << idle_core_wastage << "\n";
                    }
                }

                double cold_cache_misses = 0, cost_of_cold_miss = 0, capacity_cache_misses = 0, cost_of_capacity_miss = 0;
                if (feat.inlined_calls == 0) {
                    // Estimate the number of cold cache misses on the data that this reads from and their cost
                    // Cost dominated by lines not bytes due to streaming prefetchers
                    cold_cache_misses = (feat.unique_lines_read_per_realization +
                                         feat.unique_bytes_read_per_realization * 1e-3);

                    cold_cache_misses *= feat.num_realizations;
                    //int64_t footprint = std::min(feat.allocation_bytes_read_per_realization, feat.bytes_read_per_realization);
                    int64_t footprint = feat.allocation_bytes_read_per_realization;
                    //cost_of_miss = std::sqrt(footprint) * params.balance * 5e-3;
                    cost_of_cold_miss = footprint * params.balance * 1e-6;

                    // Now estimate the number of capacity-related cache misses using the total number of bytes read.

                    // We have a number of unique bytes read. Call the
                    // cache level large enough to fit it L(n+1). The
                    // next cache level in is Ln. How many misses will
                    // we incur in Ln? If we load randomly within the
                    // footprint, we'll miss some constant fraction of
                    // the time. The cost of such a miss is the cost
                    // of going out to cache level L(n+1). Note that
                    // *cold* misses, by contrast, go out to the cache
                    // level that fits the entire source allocation,
                    // not just the footprint accessed of it.
                    capacity_cache_misses = feat.num_vectors * (feat.vector_loads_per_vector + feat.scalar_loads_per_vector) * 1e-2;
                    cost_of_capacity_miss = feat.unique_bytes_read_per_realization * params.balance * 1e-6;

                    // We'll assume multiway caches work well and ignore the other 'C' (conflict cache misses).
                }

                double memory_load_cost = cold_cache_misses * cost_of_cold_miss + capacity_cache_misses * cost_of_capacity_miss;

                double cache_misses = 0, cost_of_miss = 0;
                if (feat.inlined_calls == 0) {
                    // Estimate the number of cache misses on the data that this writes to and their cost
                    int64_t lines_written_per_realization = feat.bytes_at_realization / feat.innermost_bytes_at_realization;
                    cache_misses = 1e1 * lines_written_per_realization + feat.bytes_at_realization * 1e-2;
                    cache_misses *= feat.num_realizations;
                    //cost_of_miss = std::sqrt(feat.bytes_at_production) * params.balance * 5e-3;
                    cost_of_miss = feat.bytes_at_production * params.balance * 2e-6;
                }

                double memory_store_cost = cache_misses * cost_of_miss;

                // Penalize writing partial cache lines. Assume a cache line is two simd vectors.
                const double native_cache_line_size = native_vector_size * 2;
                const double cache_line_wastage = std::max(1.0, native_cache_line_size / feat.innermost_pure_loop_extent);
                memory_store_cost *= cache_line_wastage;

                // Malloc aint free. Small allocations should go on the stack, but this isn't totally reliable.
                double cost_of_mallocs = feat.num_realizations * 1e2;

                // Penalize working sets that start to fall out of cache
                double ws = 1e-6 * feat.working_set;
                double cost_of_working_set = ws * ws * ws * params.balance * feat.num_realizations;

                if (verbose) {
                    debug(0) << "Cost model for " << stage.stage.name() << " "
                             << compute_cost << " + "
                             << memory_load_cost << " + "
                             << memory_store_cost << " + "
                             << cost_of_mallocs << " + "
                             << cost_of_working_set << '\n';
                }

                cost += compute_cost + memory_load_cost + memory_store_cost + cost_of_mallocs + cost_of_working_set;
            }
        }
        cost_calculations++;
        return true;
    }

    IntrusivePtr<State> make_child() const {
        State *s = new State;
        s->parent = this;
        s->root = root;
        s->cost = cost;
        s->num_funcs_scheduled = num_funcs_scheduled;
        return s;
    }

    void generate_children(const FunctionDAG &dag,
                           const MachineParams &params,
                           ThroughputPredictorPipeline *throughput_predictor,
                           std::function<void(IntrusivePtr<State> &&)> &accept_child) const {
        internal_assert(root.defined() && root->is_root());

        if (num_funcs_scheduled == (int)dag.nodes.size()) {
            return;
        }

        // Enumerate all legal ways to schedule the next Func
        const FunctionDAG::Node *node = &dag.nodes[num_funcs_scheduled];
        for (const auto *e : node->outgoing_edges) {
            internal_assert(root->computes(e->consumer))
                << "Partially scheduled code doesn't compute " << e->consumer->func.name()
                << ", which is one of the consumers of " << node->func.name();
        }

        if (!node->outgoing_edges.empty() && !root->calls(node)) {
            debug(0) << "In state:\n";
            dump();
            debug(0) << node->func.name() << " is consumed by:\n";
            for (const auto *e : node->outgoing_edges) {
                debug(0) << e->consumer->func.name() << " stage " << e->consumer_stage << "\n";
                debug(0) << "Which in turn consumes:\n";
                for (const auto *e2 : e->consumer->incoming_edges) {
                    debug(0) << "  " << e2->producer->func.name() << "\n";
                }
            }
            internal_error << "Pipeline so far doesn't use next Func: " << node->func.name() << '\n';
        }

        int num_children = 0;
        {
            // 1) Inline it
            if (node->stages.size() == 1 && !node->is_output && !node->is_input) {
                auto child = make_child();
                LoopNest *new_root = new LoopNest;
                new_root->copy_from(*root);
                new_root->inline_func(node);
                child->root = new_root;
                child->num_funcs_scheduled++;
                // TODO: filter children here instead of calculating the cost of children we don't want.
                if (child->calculate_cost(dag, params, throughput_predictor)) {
                    internal_assert(child->root->computes(node)) << "Failed to inline " << node->func.name() << '\n';
                    num_children++;
                    accept_child(std::move(child));
                } else {
                    // Discarding state....
                }
            }
        }


        // 2) Realize it somewhere
        for (int vector_dim = 0; vector_dim < node->func.dimensions(); vector_dim++) {
            // Outputs must be vectorized over their innermost
            // dimension, because we don't have control of the
            // storage.
            if (vector_dim > 0 && (node->is_output || node->is_input)) break;

            // TODO: Pre-prune the list of sane dimensions to
            // vectorize a Func over to reduce branching factor.

            auto tile_options = root->compute_in_tiles(node, nullptr, params, vector_dim, false);
            for (IntrusivePtr<const LoopNest> &n : tile_options) {
                auto child = make_child();
                child->root = std::move(n);
                child->num_funcs_scheduled++;
                if (child->calculate_cost(dag, params, throughput_predictor)) {
                    internal_assert(child->root->computes(node)) << "Failed to inject realization of " << node->func.name() << '\n';
                    num_children++;
                    accept_child(std::move(child));
                }
            }
        }

        if (num_children == 0) {
            debug(0) << "Warning: Found no legal way to schedule "
                     << node->func.name() << " in the following State:\n";
            dump();
            internal_error << "Aborted";
        }
    }

    void dump() const {
        debug(0) << "State with cost " << cost << ":\n";
        root->dump("");
        debug(0) << schedule_source;
    }

    string schedule_source;

    void apply_schedule(const FunctionDAG &dag, const MachineParams &params) {
        map<const FunctionDAG::Node::Stage *, LoopNest::StageScheduleState> state_map;
        root->apply(LoopLevel::root(), state_map, params.parallelism, 0, nullptr, nullptr);

        std::ostringstream src;

        // Print handles for all the Funcs
        int i = (int)(dag.nodes.size() - 1);
        for (const auto &n : dag.nodes) {
            if (!n.is_input) {
                src << "Func " << n.func.name() << " = get_pipeline().get_func(" << i << ");\n";
            }
            i--;
        }

        // Gather all Vars and RVars so that we can declare them in the emitted source
        map<string, string> vars, rvars;
        for (auto &p : state_map) {
            for (auto &v : p.second.vars) {
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
            string prefix = "Var ";
            for (const auto &p : vars) {
                if (p.second.empty()) {
                    src << prefix << p.first << "(\"" << p.first << "\")";
                } else {
                    src << prefix << p.first << "(" << p.second << ")";
                }
                prefix = ", ";
            }
            src << ";\n";
        }
        if (!rvars.empty()) {
            string prefix = "RVar ";
            for (const auto &p : rvars) {
                if (p.second.empty()) {
                    src << prefix << p.first << "(\"" << p.first << "\")";
                } else {
                    src << prefix << p.first << "(" << p.second << ")";
                }
                prefix = ", ";
            }
            src << ";\n";
        }

        for (auto &p : state_map) {
            if (p.first->node->is_input) continue;

            Stage stage(p.first->stage);

            // Do all the reorders and pick which vars to
            // parallelize.
            vector<VarOrRVar> vars;
            int64_t parallel_tasks = 1;
            for (auto it = p.second.vars.rbegin(); it != p.second.vars.rend(); it++) {
                if (!it->exists) continue;
                if (!it->parallel) break;
                if (parallel_tasks > params.parallelism && parallel_tasks * it->extent > params.parallelism * 128) {
                    // Probably should stop at this point. TODO:
                    // replace with actually searching for what
                    // parallel tiles to use in the search.
                    break;
                }
                parallel_tasks *= it->extent;
                p.second.schedule_source << "\n    .parallel(" << it->var.name() << ")";
                stage.parallel(it->var);
                // Stop at a sufficient number of tasks (TODO: Make this a tiling level in the search space instead).
                if (parallel_tasks > params.parallelism * 8) break;
            }

            if (p.second.vars.size() > 1) {
                p.second.schedule_source << "\n    .reorder(";
                bool first = true;
                for (auto &v : p.second.vars) {
                    if (v.exists) {
                        vars.push_back(v.var);
                        if (!first) {
                            p.second.schedule_source << ", ";
                        }
                        first = false;
                        p.second.schedule_source << v.var.name();
                    }
                }
                p.second.schedule_source << ")";
                stage.reorder(vars);
            }

            // Reorder the vector dimension innermost
            if (p.first->index == 0 && p.second.vector_dim > 0) {
                vector<Var> storage_vars = Func(p.first->node->func).args();
                for (int i = p.second.vector_dim; i > 0; i--) {
                    std::swap(storage_vars[i], storage_vars[i-1]);
                }
                p.second.schedule_source << "\n    .reorder_storage(";
                bool first = true;
                for (auto v : storage_vars) {
                    if (!first) {
                        p.second.schedule_source << ", ";
                    }
                    first = false;
                    p.second.schedule_source << v.name();
                }
                p.second.schedule_source << ")";
                Func(p.first->node->func).reorder_storage(storage_vars);
            }

            // Dump the schedule source string
            src << p.first->name
                << p.second.schedule_source.str()
                << ";\n";
        }
        schedule_source = src.str();
        bool in_quotes = false;
        for (auto &c : schedule_source) {
            in_quotes ^= (c == '"');
            if (!in_quotes && c == '$') c = '_';
        }
    }
};



int State::cost_calculations = 0;

}

template<>
RefCount &ref_count<State>(const State *t) {return t->ref_count;}

template<>
void destroy<State>(const State *t) {delete t;}

namespace {

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

void configure_pipeline_features(const FunctionDAG &dag,
                                 const MachineParams &params,
                                 ThroughputPredictorPipeline *throughput_predictor) {
    throughput_predictor->reset();
    const int pipeline_feat_size = 56 * 7;
    static_assert(sizeof(PipelineFeatures) - 7 * sizeof(int) ==
                  sizeof(int) * pipeline_feat_size,
                  "Incorrect size for pipeline features");
    const int num_stages = dag.nodes[0].stages[0].max_id; // TODO: Add getter to DAG for this.
    Runtime::Buffer<float> pipeline_features(56, 7, num_stages);
    int stage = 0;
    for (const auto &n : dag.nodes) {
        for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
            const auto &s = *it;
            const int *pipeline_feats = (const int *)(&(s.features)) + 7;
            // skip the first 7 features
            for (int i = 0; i < pipeline_feat_size; i++) {
                int x = i/7;
                int y = i%7;
                pipeline_features(x, y, stage) = pipeline_feats[i];
            }
            stage += 1;
        }
    }
    internal_assert(stage == num_stages);
    throughput_predictor->set_pipeline_features(pipeline_features, params.parallelism);
}

IntrusivePtr<State> optimal_schedule_pass(FunctionDAG &dag,
                                          vector<Function> outputs,
                                          const MachineParams &params,
                                          ThroughputPredictorPipeline *throughput_predictor,
                                          int beam_size,
                                          int pass_idx,
                                          std::unordered_set<uint64_t> &permitted_hashes) {

    if (throughput_predictor) {
        configure_pipeline_features(dag, params, throughput_predictor);
    }

    StateQueue q, pending;

    {
        IntrusivePtr<State> initial{new State};
        initial->root = new LoopNest;
        q.emplace(std::move(initial));
    }

    // A progress bar.
    uint32_t counter = 0;
    bool draw_progress_bar = isatty(2);
    auto tick = [&](double progress) {
        if (!draw_progress_bar) return;
        counter++;
        const int bits = 11;
        if (counter & ((1 << bits) - 1)) return;
        progress *= 78;
        debug(0) << '[';
        for (int j = 0; j < 78; j++) {
            if (j < progress) {
                debug(0) << '.';
            } else if (j - 1 < progress) {
                debug(0) << "/-\\|"[(counter >> bits) % 4];
            } else {
                debug(0) << ' ';
            }
        }
        debug(0) << ']';
        for (int j = 0; j < 80; j++) {
            debug(0) << '\b';
        }
    };

    int expanded;

    std::function<void(IntrusivePtr<State> &&)> enqueue_new_children =
        [&](IntrusivePtr<State> &&s) {

        // debug(0) << "\n** Generated child: ";
        // s->dump();
        // s->calculate_cost(dag, params, nullptr, true);

        internal_assert(s->num_funcs_scheduled == s->parent->num_funcs_scheduled + 1);

        int progress = s->num_funcs_scheduled * beam_size + expanded;
        size_t max_progress = dag.nodes.size() * beam_size;
        tick(double(progress) / max_progress);
        s->penalized = false;

        q.emplace(std::move(s));
    };

    for (int i = 0; ; i++) {
        std::unordered_map<uint64_t, int> hashes;
        q.swap(pending);

        internal_assert(!pending.empty());

        if ((int)pending.size() > beam_size * 10000) {
            debug(0) << "Warning: Huge number of states generated (" << pending.size() << ").\n";
        }

        expanded = 0;
        while (expanded < beam_size && !pending.empty()) {

            IntrusivePtr<State> state {pending.pop()};

            if (beam_size > 1) {
                // Apply cost penalties to the queue according to
                // structural uniqueness.
                if (!state->penalized) {
                    uint64_t h1 = state->structural_hash(pass_idx + 1, params.parallelism);
                    uint64_t h0 = state->structural_hash(pass_idx - 1, params.parallelism);
                    int penalty = ++hashes[h1];
                    if (pass_idx > 0 && !permitted_hashes.count(h0)) {
                        // It's possible to get yourself into a state
                        // where the only things in the beam that match
                        // the hash were quick-rejected due to details not
                        // captured in the hash, so we apply a huge
                        // penalty, but leave the impermissible state in
                        // the beam.
                        // debug(0) << "\nImpermissible hash " << pass_idx << " at " << state->num_funcs_scheduled << " " << h0 << ":\n";
                        // state->dump();
                        penalty += 10;
                    }
                    if (penalty > 1) {
                        state->penalized = true;
                        state->cost *= penalty;
                        // After penalizing this state, it's no longer the
                        // best, defer it.
                        if (!pending.empty() && state->cost > pending.top()->cost) {
                            pending.emplace(std::move(state));
                            continue;
                        }
                    }
                }
            }

            if (pending.size() > 1 && random_dropout()) {
                // debug(0) << "Dropping state\n";
                continue;
            }

            if (state->num_funcs_scheduled == (int)dag.nodes.size()) {
                debug(0) << '\n';

                if (false) {
                    debug(0) << "Optimal state?\n";
                    state->dump();

                    debug(0) << "\nRest of queue:\n";
                    while (!pending.empty()) {
                        pending.pop()->dump();
                    }
                }

                auto best = state;

                // Bless the reasonable stuff in the beam as permissible states to visit again
                int blessed = 0;
                while (state->cost <= 1.2 * best->cost && blessed < beam_size) {
                    const State *s = state.get();
                    while (s) {
                        uint64_t h1 = s->structural_hash(pass_idx, params.parallelism);
                        permitted_hashes.insert(h1);
                        s = s->parent.get();
                    }
                    if (pending.empty()) break;
                    state = pending.pop();
                    blessed++;
                }

                return best;
            }

            /*
            if (state->num_funcs_scheduled > 0 &&
                dag.nodes[state->num_funcs_scheduled].func.name() == "downsampled_x") {
            */
            if (false) {
                debug(0) << "\n\n**** Beam: (" << expanded << "):\n";
                state->dump();
            }

            /*
              debug(0) << "Expanding state:";
              state->dump();
              state->calculate_cost(dag, params, nullptr, true);
            */

            state->generate_children(dag, params, throughput_predictor, enqueue_new_children);
            expanded++;
        }

        // Drop the other states unconsidered.
        pending.clear();

        if (throughput_predictor) {
            // Now evaluate all the costs and re-sort them in the priority queue
            throughput_predictor->evaluate_costs();
            q.resort();
        }

        string cyos_str = get_env_variable("HL_CYOS");
        if (cyos_str == "1") {
            // Manually discard everything in the queue except for the user-chosen option
            // Print user choices.
            debug(0) << "\n--------------------\n";
            debug(0) << "Select a schedule:\n";
            for (int choice_label = (int)q.size() - 1; choice_label >= 0; choice_label--) {
                auto state = q[choice_label];
                debug(0) << "\n[" << choice_label << "]:\n";
                state->dump();
            }

            // Select next partial schedule to expand.
            int selection = -1;
            while (selection < 0 || selection >= (int)q.size()) {
                debug(0) << "\nEnter selection: ";
                std::cin >> selection;
            }

            auto selected = q[selection];
            q.clear();
            q.emplace(std::move(selected));
        }
    }
}

IntrusivePtr<State> optimal_schedule(FunctionDAG &dag,
                                     vector<Function> outputs,
                                     const MachineParams &params,
                                     ThroughputPredictorPipeline *throughput_predictor,
                                     int beam_size) {

    IntrusivePtr<State> best;

    std::unordered_set<uint64_t> permitted_hashes;
    int num_passes = (beam_size == 1) ? 1 : 5;
    for (int i = 0; i < num_passes; i++) {
        auto pass = optimal_schedule_pass(dag, outputs, params, throughput_predictor, beam_size, i, permitted_hashes);
        debug(0) << "\nPass " << i << " result:\n";
        pass->dump();

        if (i == 0 || pass->cost < best->cost) {
            best = pass;
        }
    }

    return best;
}

}

std::string generate_schedules_new(const std::vector<Function> &outputs,
                                   const Target &target,
                                   const MachineParams &params) {

    State::cost_calculations = 0;
    string seed_str = get_env_variable("HL_SEED");
    int seed = (int)time(NULL);
    if (!seed_str.empty()) {
        seed = atoi(seed_str.c_str());
    }
    debug(0) << "Dropout seed = " << seed << '\n';
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

    ThroughputPredictorPipeline throughput_predictor;
    ThroughputPredictorPipeline *tp = &throughput_predictor;
    if (get_env_variable("HL_USE_MANUAL_COST_MODEL") == "1") {
        tp = nullptr;
    }

    IntrusivePtr<State> optimal;

    if (time_limit) {
        // Use a fixed running time
        auto start = std::chrono::steady_clock::now();
        for (size_t beam_size = 1; ; beam_size *= 2) {
            auto s = optimal_schedule(dag, outputs, params, tp, beam_size);
            if (beam_size == 1 || s->cost < optimal->cost) {
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
        optimal = optimal_schedule(dag, outputs, params, tp, beam_size);
    }

    debug(0) << "Cost evaluated this many times: " << State::cost_calculations << '\n';

    debug(0) << "** Optimal schedule:\n";

    // Just to get the debugging prints to fire
    optimal->calculate_cost(dag, params, tp, true);

    // Apply the schedules
    optimal->apply_schedule(dag, params);

    // Print out the schedule
    optimal->dump();

    // Print out the predicted runtime of each Func, so we can compare them to a profile
    // optimal->print_predicted_runtimes(params);

    string feature_file = get_env_variable("HL_FEATURE_FILE");
    if (!feature_file.empty()) {
        optimal->save_featurization(dag, params, feature_file);
    }

    return "";
}

bool autotuner_errored = false;
void autotuner_error_handler(void *, const char *msg) {
    autotuner_errored = true;
    debug(0) << "Error during autotuning: " << msg << "\n";
}

std::string generate_schedules_autotune(const std::vector<Function> &output_funcs,
                                        const Target &target,
                                        const MachineParams &params) {
    const int beam_size = 50;

    ThroughputPredictorPipeline tp;

    struct Trial {
        std::unique_ptr<FunctionDAG> dag;
        IntrusivePtr<State> optimal;
        vector<Function> outputs;
        map<string, Function> env;
        Pipeline p;
        float misprediction;
        uint64_t hash;
    };

    const int max_history = 800;
    const int batch_size = 8;
    vector<std::shared_ptr<Trial>> history;
    Runtime::Buffer<float> runtimes(max_history);

    // Compute an environment
    map<string, Function> env;
    for (Function f : output_funcs) {
        populate_environment(f, env);
    }

    // Construct a temporary dag just to dump it for debugging
    FunctionDAG(output_funcs, params, target).dump();

    // Use a thread pool for compilation jobs. LLVM is slow.
    ThreadPool<void> thread_pool;

    float learning_rate = 0.0001f;

    size_t best_of_all_time = 0;

    int exploration_dropout = 95;

    Tools::BenchmarkConfig config;
    config.min_time = 0.2;
    config.max_time = 2.0;
    config.accuracy = 0.02;

    for (int iter = 0;; iter++) {

        size_t batch_start = history.size();

        // Make a batch of schedules
        for (int b = 0; b < batch_size; b++) {
            debug(0) << "Generating schedule " << b << "\n";
            // Exploitation
            int bs = (b == 0) ? beam_size : 1;
            // Exploration (TODO, set dropout according to std.dev. of runtimes in last batch)
            random_dropout_threshold = (b == 0) ? 100 : exploration_dropout;

            // Create a deep-copy of the entire graph of Funcs.
            vector<Function> outputs;
            map<string, Function> local_env;
            std::tie(outputs, local_env) = deep_copy(output_funcs, env);

            // Autoschedule it
            std::unique_ptr<FunctionDAG> dag { new FunctionDAG {outputs, params, target} };
            std::shared_ptr<Trial> t { new Trial {std::move(dag), nullptr, outputs, local_env, Pipeline{}, 0.0f} };
            bool unique = false;
            while (!unique) {
                t->optimal = optimal_schedule(*(t->dag), outputs, params, &tp, bs);
                if (t->optimal->cost >= 1e50) {
                    // We're starting to include crazy stuff - get more conservative
                    if (exploration_dropout < 99) exploration_dropout++;
                    continue;
                }
                t->hash = t->optimal->structural_hash(0x7fffffff, params.parallelism);

                // Make sure this hash isn't already in the history
                unique = true;
                if (b > 0) {
                    for (size_t i = 0; i < history.size(); i++) {
                        if (history[i]->hash == t->hash) {
                            unique = false;
                            // We're starting to repeat stuff, increase diversity
                            if (exploration_dropout > 50) exploration_dropout--;
                            break;
                        }
                    }
                }
            }
            t->optimal->apply_schedule(*dag, params);
            history.emplace_back(std::move(t));
        }

        // Compile them
        vector<std::future<void>> jobs(batch_size);
        for (int b = 0; b < batch_size; b++) {
            debug(0) << "Compiling batch member " << b << "\n";
            internal_assert(batch_start + b < history.size());
            Trial *t = history[batch_start + b].get();
            vector<Func> outputs;
            for (Function o : t->outputs) {
                outputs.push_back(Func(o));
            }
            t->p = Pipeline(outputs);
            // TODO: We'd like to use the target here, but that
            // triggers recompilation in infer_input_bounds when the
            // target doesn't match the jit target.
            if (debug::debug_level() > 0) {
                t->p.compile_jit();
            } else {
                jobs[b] = thread_pool.async([=]() {t->p.compile_jit();});
            }
        }

        if (debug::debug_level() == 0) {
            for (int b = 0; b < batch_size; b++) {
                jobs[b].wait();
            }
        }

        int best = -1;
        size_t best_cursor = 0;
        double best_runtime = 1e20;

        for (int b = 0; b < batch_size; b++) {
            debug(0) << "Benchmarking batch member " << b << "\n";
            internal_assert(batch_start + b < history.size());
            Trial *t = history[batch_start + b].get();
            vector<Buffer<>> output_buffers;
            for (Function o : t->outputs) {
                // Make output buffers and run
                vector<int> sz;
                sz.resize(o.dimensions());
                for (const auto &b : o.schedule().estimates()) {
                    int dim = 0;
                    for (dim = 0; dim < o.dimensions(); dim++) {
                        if (o.args()[dim] == b.var) break;
                    }
                    internal_assert(dim < o.dimensions());
                    const int64_t *sz_ptr = as_const_int(b.extent);
                    sz[dim] = *sz_ptr;
                }
                for (auto t : o.output_types()) {
                    output_buffers.emplace_back(t, sz);
                }
            }
            Realization r(output_buffers);

            // Make some input buffers
            t->p.infer_input_bounds(r);
            t->p.set_error_handler(autotuner_error_handler);

            // Benchmark it
            autotuner_errored = false;
            size_t c = batch_start + b;
            double ms = 1e3 * Tools::benchmark([&]() {t->p.realize(r);}, config);
            if (autotuner_errored) {
                internal_assert(!history.empty()) << "The very first run errored out\n";
                runtimes(c) = runtimes(0);
                history[c] = history[0];
            } else {
                internal_assert(c >= 0 && c < (size_t)runtimes.dim(0).extent());
                runtimes(c) = ms;
                t->misprediction = std::abs(t->optimal->cost - ms);

                if (runtimes(c) < best_runtime) {
                    best_runtime = runtimes(c);
                    best = b;
                    best_cursor = c;

                    if (runtimes(c) < runtimes(best_of_all_time)) {
                        best_of_all_time = c;
                    }
                }
            }

            debug(0) << "Runtime " << b << ": " << runtimes(c) << "\n";
        }

        debug(0) << "Best runtime in batch was " << best << " " << best_runtime << "\n";
        history[best_cursor]->optimal->dump();

        debug(0) << "Best runtime of all time is " << best_of_all_time << " " << runtimes(best_of_all_time) << "\n";
        {
            auto &t = history[best_of_all_time];
            t->optimal->dump();
            auto args = t->p.infer_arguments();
            auto lean_target = Target("host-no_asserts-no_bounds_query-no_runtime");
            std::string filename = "autotune_best_" + std::to_string(iter) + ".s";
            t->p.compile_to_assembly(filename, args, "best", lean_target);
            t->p.compile_to_lowered_stmt(filename + "tmt", args, StmtOutputFormat::Text, lean_target);
        }

        // Update the model weights

        // First we enqueue all the features
        tp.reset();
        for (size_t i = 0; i < history.size(); i++) {
            history[i]->optimal->calculate_cost(*(history[i]->dag), params, &tp, false);
        }

        // Then run the predictor in training mode once we have enough samples
        if (1) { //history.size() >= max_history / 2) {
            // Iterate until we can model the current set of runtimes to within 20% of the fastest one
            for (int i = 0; i < 1000; i++) {
                float loss = tp.backprop(runtimes.cropped(0, 0, history.size()), learning_rate);
                if (i % 100 == 0) {
                    debug(0) << "RMS misprediction: " << loss << "\n";
                }
                if (i > 100 && loss < runtimes(best_of_all_time) / 5.0) {
                    debug(0) << "Final RMS misprediction: " << loss << "\n";
                    break;
                }
            }
            tp.save_weights();
        }

        // Then dump some of history if we're out of space. Keep the
        // even-numbered samples, but make sure we also preserve the
        // best sample of all time.
        if (history.size() >= max_history) {
            for (size_t i = 0; i < history.size(); i += 2) {
                size_t keep = i;
                if (i/2 == best_of_all_time/2) {
                    keep = best_of_all_time;
                    best_of_all_time = i/2;
                }
                history[i/2] = history[keep];
                runtimes(i/2) = runtimes(keep);
            }
            history.erase(history.begin() + max_history/2, history.end());
            internal_assert(history.size() == max_history/2);
        }
    }

    return "";
}

// Register this as the autoscheduler
struct AutoScheduler {
    AutoScheduler() {
        debug(0) << "Registering autoscheduler...\n";
        Pipeline::set_custom_auto_scheduler(*this);
    }

    string operator()(Pipeline p, const Target &target, const MachineParams &params) {
        std::vector<Function> outputs;
        for (Func f : p.outputs()) {
            outputs.push_back(f.function());
        }
        return generate_schedules_new(outputs, target, params);
    }
} auto_scheduler;

}
}
