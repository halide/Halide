#include "AutoScheduleNew.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "IRVisitor.h"
#include "IRMutator.h"
#include "OutputImageParam.h"
#include "RealizationOrder.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"
#include "PartitionLoops.h"

#include "ThroughputPredictorPipeline.h"
#include "ThroughputPredictorLoader.h"

#include <set>
#include <queue>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iostream>
#include <random>

// TODO: overview of algorithm

namespace Halide {
namespace Internal {

using namespace AutoScheduleModel;

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
        vectorizable_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes], // First (vectorized) col is 1, 0, 0, ... f(x+y, z*y, y/z)
        strided_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],      // First col is [(int)2,3,4], 0, 0, ...        f(3*x + 1, z/8, y/z)
        scalar_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],       // First col is all zero                  f(y, 2, z*8)
        gather_scatter_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes];            // Not one of the three categories above  f(x, x, sqrt(y))

    // TODO: We should possibly feed these Jacobians directly
    // to the net rather than computing the properties above.

    // TODO: strided captures downsamples. What about upsamples?

    // TODO: It's weird that we've already selected a
    // dimension to be vectorized over - that should be part
    // of the scheduling search space instead.

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


// A representation of the function DAG. The nodes and edges are both
// in reverse realization order, so if you want to walk backwards up
// the DAG, just iterate the nodes or edges in-order.
struct FunctionDAG {

    struct Edge;

    struct Node {
        Function func;

        double bytes_per_point;

        // The min/max variables used to denote a symbolic region of
        // this Func. Used in the cost above, and in the Edges below.
        vector<Interval> region_required;

        // The region computed of a Func, in terms of the region
        // required. For simple Funcs this is identical to the
        // region_required. However, in some Funcs computing one
        // output requires computing other outputs too. You can't
        // really ask for a single output pixel from something blurred
        // with an IIR without computing the others, for example.
        vector<Interval> region_computed;

        struct Loop {
            string var;
            bool pure;
            Expr min, max;
        };

        // One stage of a Func
        struct Stage {
            // The loop nest that computes this stage, from innermost out.
            vector<Loop> loop;

            // The amount of compute done per point evaluated, including the need to generate the call.
            double compute;

            // The vectorization width that will be used.
            int vector_size;

            // The featurization of the compute done
            PipelineFeatures features;

            // The actual Halide front-end stage object
            Halide::Stage stage;

            Stage(Halide::Stage s) : stage(s) {}
        };
        vector<Stage> stages;

        // Max vector size across the stages
        int vector_size;

        vector<const Edge *> outgoing_edges, incoming_edges;
    };

    struct Edge {
        FunctionDAG::Node *producer, *consumer;
        int consumer_stage;

        // The region required of producer in terms of the variables
        // of the loops of this stage of the consumer.
        vector<Interval> bounds;

        // The number of calls the consumer makes to the producer, per
        // point in the loop nest of the consumer.
        int calls;
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
            node_map[f] = &nodes[i];
        }

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

            string innermost_storage_dim;
            if (!consumer.args().empty()) {
                innermost_storage_dim = consumer.args()[0];
            }

            for (int s = 0; s <= (int)consumer.updates().size(); s++) {
                Halide::Stage halide_stage = Func(consumer);
                if (s > 0) halide_stage = Func(consumer).update(s-1);
                Node::Stage stage(halide_stage);

                const Definition &def = (s == 0) ? consumer.definition() : consumer.update(s - 1);
                const StageSchedule &sched = def.schedule();

                Scope<Interval> stage_scope;
                stage_scope.set_containing_scope(&scope);
                for (const auto &rv : sched.rvars()) {
                    Expr min = simplify(apply_param_estimates.mutate(rv.min));
                    Expr max = simplify(apply_param_estimates.mutate(rv.min + rv.extent - 1));
                    stage_scope.push(rv.var, Interval(min, max));
                }

                // Figure out the region computed of the stage by taking bounds of the LHS Exprs
                for (int j = 0; j < consumer.dimensions(); j++) {
                    Interval in = bounds_of_expr_in_scope(def.args()[j], stage_scope);
                    in.min = simplify(apply_param_estimates.mutate(in.min));
                    in.max = simplify(apply_param_estimates.mutate(in.max));
                    if (s == 0) {
                        node.region_computed.push_back(in);
                    } else {
                        // We take the bounding box over the stages
                        node.region_computed[j].include(in);
                    }
                }

                bool should_vectorize = false;

                // We'll take any existing reordering, but won't handle existing splits
                internal_assert(sched.splits().empty());
                for (const auto &d : sched.dims()) {
                    // Skip synthetic loops like "__outermost"
                    if (!stage_scope.contains(d.var)) continue;

                    Node::Loop l;
                    l.var = d.var;

                    // We've already captured the loop extents in the subscope, just not the ordering
                    Interval in = stage_scope.get(l.var);
                    l.min = in.min;
                    l.max = in.max;
                    l.pure = !d.is_rvar();

                    if (d.var == innermost_storage_dim) {
                        should_vectorize = true;
                        stage.loop.insert(stage.loop.begin(), l);
                    } else {
                        stage.loop.emplace_back(std::move(l));
                    }
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

                if (!should_vectorize) {
                    stage.vector_size = 1;
                }

                if (s == 0) {
                    node.vector_size = stage.vector_size;
                } else {
                    node.vector_size = std::max(node.vector_size, stage.vector_size);
                }

                node.stages.emplace_back(std::move(stage));

                // Now create the edges that lead to this func
                for (auto p : boxes_required(exprs, stage_scope)) {
                    auto it = env.find(p.first);
                    if (it != env.end() && p.first != consumer.name()) {
                        // Discard loads from input images and self-loads
                        Edge edge;
                        edge.consumer = node_map.at(consumer);
                        edge.consumer_stage = s;
                        edge.producer = node_map.at(env[p.first]);
                        edge.bounds = p.second.bounds;
                        for (Interval &i : edge.bounds) {
                            i.max = simplify(apply_param_estimates.mutate(i.max));
                            i.min = simplify(apply_param_estimates.mutate(i.min));
                        }
                        edge.calls = checker.calls[edge.producer->func.name()];
                        edges.emplace_back(std::move(edge));
                    }
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
            bool is_strided = true, is_vector = true, is_scalar = true;
            for (size_t i = 0; i < args.size(); i++) {
                matrix[i].resize(stage.loop.size());
                for (size_t j = 0; j < stage.loop.size(); j++) {
                    auto deriv = differentiate(args[i], stage.loop[j].var);
                    zeros_per_row[i] += deriv.is_zero();
                    ones_per_row[i] += deriv.is_one();
                    zeros_per_col[j] += deriv.is_zero();
                    ones_per_col[j] += deriv.is_one();
                    is_pointwise &= (i == j ? deriv.is_one() : deriv.is_zero());
                    if (j == vector_dim) {
                        is_vector &= (i == 0 ? deriv.is_one() : deriv.is_zero());
                        is_strided &= (i == 0 ? deriv.is_small_integer() : deriv.is_zero());
                        is_scalar &= deriv.is_zero();
                    }
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
            bool is_gather_scatter = !is_vector && !is_strided && !is_scalar;

            auto type_class = classify_type(t);

            stage.features.pointwise_accesses[(int)type][(int)type_class] += is_pointwise;
            stage.features.transpose_accesses[(int)type][(int)type_class] += is_transpose;
            stage.features.broadcast_accesses[(int)type][(int)type_class] += is_broadcast;
            stage.features.slice_accesses[(int)type][(int)type_class] += is_slice;
            stage.features.vectorizable_accesses[(int)type][(int)type_class] += is_vector;
            stage.features.strided_accesses[(int)type][(int)type_class] += is_strided;
            stage.features.scalar_accesses[(int)type][(int)type_class] += is_scalar;
            stage.features.gather_scatter_accesses[(int)type][(int)type_class] += is_gather_scatter;
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
            for (const Interval &i : n.region_computed) {
                debug(0) << "    " << i.min << ", " << i.max << '\n';
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
            for (const Interval &i : e.bounds) {
                debug(0) << "    Min " << j << ": " << i.min << '\n';
                debug(0) << "    Max " << j << ": " << i.max << '\n';
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
        auto v = generate_tilings(s, d - 1, factor, allow_splits, vector_dim, vector_size);
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
                for (int outer = 1; outer <= s[d]; outer *= factor) {
                    int inner = (s[d] + outer - 1) / outer;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    if (outer > inner || (d == vector_dim && inner < vector_size)) break;
                    t.back() = outer;
                    result.push_back(t);
                }
                for (int inner = (d == vector_dim) ? vector_size : 1; inner < s[d]; inner *= factor) {
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
    // regarding the modelling of sliding window

    int64_t points_computed_minimum = 0; // The minimum number of points that are actually required to be computed to produce a correct output.

    int64_t innermost_loop_extent = 0; // Trip count of innermost loop
    int64_t innermost_pure_loop_extent = 0; // Trip count of the loop that's going to be vectorized
    int64_t inner_parallelism = 0; // The number of parallel jobs used in the production of this Func. 1 unless the Func is compute_root.
    int64_t outer_parallelism = 0; // The number of times this Func could be realized in parallel. 1 when the Func is compute_root.

    int64_t bytes_at_realization = 0; // Size of the region computed at the store_at site, measured in bytes
    int64_t bytes_at_production = 0; // Size of the region computed at the compute_at site, measured in bytes
    int64_t bytes_at_root = 0; // The same at root, regardless of where it's actually scheduled
    int64_t innermost_bytes_at_realization = 0;
    int64_t innermost_bytes_at_production = 0;
    int64_t innermost_bytes_at_root = 0;

    int64_t bytes_read_per_tile = 0; // Number of bytes loaded from alnputs per tile (TODO: Not particularly useful without knowing how many times this runs)

    int64_t inlined_calls = 0; // For inlined Funcs, how many calls are made to this Func total

    // Logically these features should be grouped earlier, but the convnet currently doesn't know about them
    int64_t bytes_read_per_realization = 0; // Number of bytes loaded from all inputs per production
    int64_t lines_read_per_realization = 0; // Number of contiguous segments of memory loaded from all inputs per production
    int64_t allocation_bytes_read_per_realization = 0; // The sum of the sizes of the allocations accessed per production. Gives a hint as to the likely locality of it.

    int64_t working_set = 0; // The sum of the sizes of the allocations within the production of this Func. Probably a good thing if it fits in cache.

    int64_t vector_size = 0; // The vectorization factor (#simd lanes) to be used to compute this stage. Wasted work if innermost_pure_loop is not a multiple of this, or if it's smaller than the stage's native vector size (which is in the pipeline features).

    int64_t rounded_innermost_pure_loop_extent = 0; // Innermost pure loop extend rounded up to the next multiple of the vector size

    int native_vector_size; // The native vector size for the narrowest type used.

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
                 << "    bytes_read_per_tile:                   " << bytes_read_per_tile << '\n'
                 << "    inlined_calls:                         " << inlined_calls << '\n'
                 << "    bytes_read_per_realization:            " << bytes_read_per_realization << '\n'
                 << "    lines_read_per_realization:            " << lines_read_per_realization << '\n'
                 << "    allocation_bytes_read_per_realization: " << allocation_bytes_read_per_realization << '\n'
                 << "    working_set:                           " << working_set << '\n'
                 << "    vector_size:                           " << vector_size << '\n'
                 << "    rounded_innermost_pure_loop_extent     " << rounded_innermost_pure_loop_extent << '\n'
                 << "    native_vector_size:                    " << vector_size << '\n';
    }
};

struct Constraints {
    virtual bool must_root(const FunctionDAG::Node *node) const { return false; }
    virtual bool may_root(const FunctionDAG::Node *node) const { return true; }
    virtual bool must_inline(const FunctionDAG::Node *node) const { return false; }
    virtual bool may_inline(const FunctionDAG::Node *node) const { return true; }
    virtual bool may_subtile() const { return true; }
    virtual bool may_parallelize(const FunctionDAG::Node::Stage *stage, int dim) const { return true; }
    virtual int tiling_factor() const { return 2; }
};

struct CoarsePassConstraints : public Constraints {
    const MachineParams &params;
    CoarsePassConstraints(const MachineParams &p) : params(p) {}
    bool may_subtile() const override { return false; }
    bool may_inline(const FunctionDAG::Node *node) const override { return false; }
    int tiling_factor() const override { return params.parallelism; }
};

struct FinePassConstraints : public Constraints {
    set<const FunctionDAG::Node *> roots;
    map<const FunctionDAG::Node::Stage *, uint64_t> parallel_dims;

    bool must_root(const FunctionDAG::Node *node) const override {
        return roots.find(node) != roots.end();
    }

    bool may_root(const FunctionDAG::Node *node) const override {
        return roots.find(node) != roots.end();
    }

    void permit_parallelization(const FunctionDAG::Node::Stage *stage, int dim) {
        parallel_dims[stage] |= ((uint64_t)1) << dim;
    }

    bool may_parallelize(const FunctionDAG::Node::Stage *stage, int dim) const override {
        auto it = parallel_dims.find(stage);
        return (it != parallel_dims.end()) && (it->second & ((uint64_t)1 << dim));
    }
};

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
    const FunctionDAG::Node *node = nullptr;
    const FunctionDAG::Node::Stage *stage = nullptr;
    int stage_idx = 0;

    // Is this the innermost loop of this func?
    bool innermost = false;

    // Are we permitted to tile this loop?
    bool tileable = false;

    // The extents of the loops
    vector<int64_t> size;

    // The nodes inside the loop body
    vector<std::shared_ptr<PartialScheduleNode>> children;

    // Funcs inlined into this inner loop, and the number of times they are called. Only valid if children is empty.
    map<const FunctionDAG::Node *, int64_t> inlined;

    // Funcs realized inside this inner loop
    set<const FunctionDAG::Node *> store_at;

    static void hash_combine(uint64_t &h, uint64_t next) {
        // From boost
        h ^= (next + 0x9e3779b9 + (h<<6) + (h>>2));
    }

    // Hash the loop structure (but not the sizes)
    void structural_hash(uint64_t &h, int depth) const {
        // Ordering constraints on producers and consumers mean we
        // only need to count the number of children (I think?).

        if (depth == 0) {
            // Don't distinguish below this depth
            hash_combine(h, funcs_realized_or_inlined());
        } else {
            hash_combine(h, inlined.size());
            hash_combine(h, store_at.size());
            hash_combine(h, children.size());
            for (uint64_t s : size) {
                hash_combine(h, s);
            }
            for (auto c : children) {
                c->structural_hash(h, depth - 1);
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

    void get_compute_sites(map<const FunctionDAG::Node *, const PartialScheduleNode *> &compute_site,
                           map<const FunctionDAG::Node *, const PartialScheduleNode *> &store_site,
                           const PartialScheduleNode *parent = nullptr) {
        for (auto c : children) {
            c->get_compute_sites(compute_site, store_site, this);
        }
        if (parent && node != parent->node) {
            compute_site[node] = parent;
        }
        for (auto f : store_at) {
            store_site[f] = this;
        }
    }

    void compute_features(const MachineParams &params,
                          const map<const FunctionDAG::Node *, const PartialScheduleNode *> &compute_site,
                          const map<const FunctionDAG::Node *, const PartialScheduleNode *> &store_site,
                          int64_t instances,
                          int64_t parallelism,
                          const PartialScheduleNode *parent,
                          const PartialScheduleNode &root,
                          int64_t *working_set,
                          map<const FunctionDAG::Node *, vector<ScheduleFeatures>> *features) {

        int64_t working_set_here = 0;

        int64_t loop_instances = 1, pure_loop_instances = 1;
        size_t idx = 0;
        for (auto i : size) {
            loop_instances *= i;
            if (stage->loop[idx++].pure) {
                pure_loop_instances *= i;
            }
        }
        int64_t subinstances = instances * loop_instances;

        for (const auto *node : store_at) {
            // Figure out the features at the store_at level
            const auto &bounds = get_bounds(node);

            vector<ScheduleFeatures> &func_features = (*features)[node];
            if (func_features.empty()) {
                func_features.resize(node->stages.size());
            }

            for (size_t s = 0; s < node->stages.size(); s++) {
                // TODO: Lift invariants from this loop. Most of it's the same for every stage.
                ScheduleFeatures &feat = func_features[s];

                feat.num_realizations = subinstances;

                feat.points_computed_per_realization = 1;
                internal_assert(!bounds.loops[s].empty());
                for (auto p : bounds.loops[s]) {
                    feat.points_computed_per_realization *= (p.second - p.first + 1);
                }
                feat.points_computed_total = feat.points_computed_per_realization * feat.num_realizations;

                feat.bytes_at_realization = node->bytes_per_point;
                for (auto p : bounds.region_computed) {
                    feat.bytes_at_realization *= (p.second - p.first) + 1;
                }
                int64_t innermost_storage_extent = 1;
                if (!bounds.region_computed.empty()) {
                    innermost_storage_extent = bounds.region_computed[0].second - bounds.region_computed[0].first + 1;
                }
                feat.innermost_bytes_at_realization = node->bytes_per_point * innermost_storage_extent;
            }
        }

        if (is_root()) {
            for (auto c : children) {
                c->compute_features(params, compute_site, store_site, subinstances, parallelism, this, root, &working_set_here, features);
            }

            // Figure out the root-level features for every Func
            for (auto &p : *features) {
                const auto *node = p.first;
                auto &feat_vec = p.second;
                const auto &root_bounds = root.get_bounds(node);
                int s = 0;
                for (auto &feat : feat_vec) {
                    feat.bytes_at_root = node->bytes_per_point;
                    for (auto p : root_bounds.region_computed) {
                        feat.bytes_at_root *= (p.second - p.first) + 1;
                    }
                    int64_t innermost_storage_extent = 1;
                    if (!root_bounds.region_computed.empty()) {
                        innermost_storage_extent = root_bounds.region_computed[0].second - root_bounds.region_computed[0].first + 1;
                    }
                    feat.innermost_bytes_at_root = node->bytes_per_point * innermost_storage_extent;

                    feat.points_computed_minimum = 1;
                    for (auto p : root_bounds.loops[s]) {
                        feat.points_computed_minimum *= (p.second - p.first + 1);
                    }
                    s++;
                }
            }

            return;
        }

        int64_t parallel_tasks = parent->is_root() ? pure_loop_instances : 1;
        int64_t subparallelism = parallel_tasks * parallelism;

        // Figure out the features at the compute_at level
        vector<ScheduleFeatures> &func_features = (*features)[node];
        if (func_features.empty()) {
            func_features.resize(node->stages.size());
        }
        ScheduleFeatures &feat = func_features[stage_idx];

        if (innermost) {
            // Figure out the features at the innermost loop cluster level
            feat.innermost_loop_extent = size.empty() ? 1 : size[0];

            feat.innermost_pure_loop_extent = 1;
            size_t i = 0;
            for (auto l : stage->loop) {
                if (l.pure) {
                    feat.innermost_pure_loop_extent = size[i];
                    break;
                }
                i++;
            }
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
            for (auto p : bounds.region_computed) {
                feat.bytes_at_production *= (p.second - p.first) + 1;
            }
            int64_t innermost_storage_extent = 1;
            if (!bounds.region_computed.empty()) {
                innermost_storage_extent = bounds.region_computed[0].second - bounds.region_computed[0].first + 1;
            }
            feat.innermost_bytes_at_production = node->bytes_per_point * innermost_storage_extent;
        }

        for (auto c : children) {
            c->compute_features(params, compute_site, store_site, subinstances, subparallelism, this, root, &working_set_here, features);
        }

        if (at_production) {
            for (const auto *node : store_at) {
                working_set_here += (*features)[node][0].bytes_at_production;
            }
            feat.working_set = working_set_here;
            feat.rounded_innermost_pure_loop_extent = ((feat.innermost_pure_loop_extent + feat.vector_size - 1) / feat.vector_size) * feat.vector_size;
        }

        *working_set += working_set_here;

        int64_t bytes_loaded = 0, lines_loaded = 0, allocation_bytes_loaded = 0;
        if (innermost || at_production) {
            // Pick the site at which we will compute the footprint relationship
            const auto *consumer_store_site = innermost ? parent : store_site.find(node)->second;
            int consumer_instances = innermost ? instances : feat.num_realizations;

            vector<const FunctionDAG::Node *> pending;
            pending.push_back(node);
            while (!pending.empty()) {
                const auto &next = pending.back()->incoming_edges;
                pending.pop_back();
                for (const auto *e : next) {
                    auto it = compute_site.find(e->producer);
                    if (it == compute_site.end()) {
                        // Producer was inlined, recursively examine its inputs
                        pending.push_back(e->producer);
                        continue;
                    }

                    const auto *producer_compute_site = it->second;
                    const auto *producer_store_site = store_site.find(e->producer)->second;
                    const auto &bounds = consumer_store_site->get_bounds(e->producer);
                    const auto &producer_compute_bounds = producer_compute_site->get_bounds(e->producer);
                    const auto &producer_store_bounds = producer_store_site->get_bounds(e->producer);
                    int64_t footprint = e->producer->bytes_per_point;
                    int64_t compute_footprint = footprint;
                    int64_t store_footprint = footprint;
                    int64_t line_footprint = 1;
                    int64_t compute_line_footprint = 1;
                    int64_t store_line_footprint = 1;
                    bool discontinuous = false;

                    internal_assert(bounds.region_required.size() == producer_compute_bounds.region_computed.size());
                    internal_assert(bounds.region_required.size() == producer_store_bounds.region_computed.size());
                    for (size_t i = 0; i < bounds.region_required.size(); i++) {
                        auto p = bounds.region_required[i];
                        auto compute_p = producer_compute_bounds.region_computed[i];
                        auto store_p = producer_store_bounds.region_required[i];
                        int64_t extent = p.second - p.first + 1;
                        int64_t compute_extent = compute_p.second - compute_p.first + 1;
                        int64_t store_extent = store_p.second - store_p.first + 1;
                        footprint *= extent;
                        compute_footprint *= compute_extent;
                        store_footprint *= store_extent;
                        if (discontinuous) {
                            line_footprint *= extent;
                            compute_line_footprint *= compute_extent;
                            store_line_footprint *= store_extent;
                        }
                        // Allocated extent might be smaller than extent if
                        // the producer was fused into this consumer. If
                        // it's larger, we may be reading from something
                        // that was computed at a much coarser
                        // granularity.
                        // discontinuous |= (store_extent > extent);
                        discontinuous = true;
                    }


                    int64_t store_instances_per_consumption = 1;
                    const auto &producer_feat = (*features)[e->producer];
                    if (!producer_feat.empty()) {
                        // The producer's realization is nested inside this Func's realization
                        const int64_t producer_store_instances = producer_feat[0].num_realizations;
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

        // TODO: consider input images in these bytes-read metrics.
        if (innermost) {
            feat.bytes_read_per_tile = bytes_loaded;
        }

        if (at_production) {
            feat.bytes_read_per_realization = bytes_loaded;
            feat.allocation_bytes_read_per_realization = allocation_bytes_loaded;
            feat.lines_read_per_realization = lines_loaded;

            if (!at_pure_production) {
                // Also pessimistically assume this update definition relies on the entirety of the produced region so far.
                // TODO: This overbills scatters, or writes to a restriction region.
                feat.bytes_read_per_realization += feat.bytes_at_production;
                feat.lines_read_per_realization++; // It's accessed contiguously
                feat.allocation_bytes_read_per_realization += feat.bytes_at_production;
            }
        }

        if (at_pure_production) {
            feat.points_computed_per_production = feat.points_computed_total / instances;
        }

        // Track features for inlined Funcs
        for (auto p : inlined) {
            auto &f = p.first;
            vector<ScheduleFeatures> &func_features = (*features)[f];
            func_features.resize(1);
            auto &inlined_feat = func_features[0];
            inlined_feat.inlined_calls += p.second * subinstances;
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
            inlined_feat.rounded_innermost_pure_loop_extent =
                ((inlined_feat.innermost_pure_loop_extent + inlined_feat.vector_size - 1) /
                 inlined_feat.vector_size) * inlined_feat.vector_size;
            inlined_feat.inner_parallelism = 1;
            inlined_feat.outer_parallelism = parallelism;
        }
    }

    bool is_root() const {
        return node == nullptr;
    }

    struct Bound {
        // The box over which something is required, touched, and the shape of the loop nest(s)
        vector<pair<int64_t, int64_t>> region_required, region_computed;
        vector<vector<pair<int64_t, int64_t>>> loops;

        // The number of points in the iteration domain. Sum over the
        // products of the loops. Outside the realization of the Func
        // it's the minimum number of iteration domain points to
        // compute the required region. Inside it's the actual.
        int64_t iteration_domain_points;
    };

    // The total bounds required of the given Func for one representative iteration of this loop. Computed lazily and cached.
    mutable map<const FunctionDAG::Node *, Bound> bounds;
    const Bound &get_bounds(const FunctionDAG::Node *f) const {
        // debug(0) << "get_bounds of " << f.name() << " in loop over " << (is_root() ? "root" : func.name()) << '\n';
        auto it = bounds.find(f);
        if (it != bounds.end()) {
            return it->second;
        }
        Bound bound;
        // Compute the region required
        if (f->outgoing_edges.empty() && is_root()) {
            // It's an output.
            // Use the bounds estimate
            bound.iteration_domain_points = 1;
            map<string, pair<int64_t, int64_t>> estimates;
            for (auto b : f->func.schedule().estimates()) {
                int64_t i_min = *as_const_int(b.min);
                int64_t i_extent = *as_const_int(b.extent);
                estimates[b.var] = {i_min, i_min + i_extent - 1};
            }
            // Set the bounds using the estimates
            for (int i = 0; i < f->func.dimensions(); i++) {
                auto it = estimates.find(f->func.args()[i]);
                user_assert(it != estimates.end())
                    << "Need an estimate on dimension " << i << " of \"" << f->func.name() << "\"";
                bound.iteration_domain_points *= it->second.second - it->second.first + 1;
                bound.region_required.push_back(it->second);
            }
        } else {
            internal_assert(!f->outgoing_edges.empty())
                << "No consumers of " << f->func.name()
                << " at loop over " << (is_root() ? "root" : node->func.name()) << '\n';
            for (const auto *e : f->outgoing_edges) {
                // Ignore consumers outside of this loop nest
                if (!computes(e->consumer)) {
                    continue;
                }
                const auto &c_bounds = get_bounds(e->consumer);
                const auto *c_node = e->consumer;
                const auto &concrete_loop = c_bounds.loops[e->consumer_stage]; // For the concrete sizes of the loop
                const auto &symbolic_loop = c_node->stages[e->consumer_stage].loop; // Just for the var names of the loop
                if (concrete_loop.empty()) {
                    // This consumer loop doesn't occur within this PartialScheduleNode
                    // TODO: Not a good way to encode this. What about deps on scalars?
                    continue;
                }
                // Create a map from the symbolic loop variables to the actual loop size
                map<string, Expr> s;
                internal_assert(concrete_loop.size() == symbolic_loop.size());
                for (size_t i = 0; i < concrete_loop.size(); i++) {
                    auto p = concrete_loop[i];
                    const string &var = symbolic_loop[i].var;
                    s[e->consumer->func.name() + "." + var + ".min"] = (int)p.first;
                    s[e->consumer->func.name() + "." + var + ".max"] = (int)p.second;
                }
                // Apply that map to the bounds relationship encoded
                // in the edge to expand the bounds of the producer to
                // satisfy the consumer
                for (int i = 0; i < f->func.dimensions(); i++) {
                    // Get bounds required of this dimension of the
                    // producer in terms of a symbolic region of the
                    // consumer.
                    Interval in = e->bounds[i];
                    // Map from symbolic region to concrete region
                    in.min = simplify(substitute(s, in.min));
                    in.max = simplify(substitute(s, in.max));
                    const int64_t *imin = as_const_int(in.min);
                    const int64_t *imax = as_const_int(in.max);
                    internal_assert(imin && imax) << in.min << ", " << in.max << '\n';
                    // Expand the bounds of the producer
                    if ((size_t)i >= bound.region_required.size()) {
                        bound.region_required.push_back({*imin, *imax});
                    } else {
                        bound.region_required[i].first = std::min(bound.region_required[i].first, *imin);
                        bound.region_required[i].second = std::max(bound.region_required[i].second, *imax);
                    }
                }
            }
            internal_assert(bound.region_required.size() == (size_t)f->func.dimensions())
                << is_root() << ' '
                << f->func.name() << ' '
                << bound.region_required.size() << ' '
                << f->func.dimensions() << '\n';
        }

        // Use the region required and the dag to compute the region computed and the iteration domain
        map<string, Expr> required_map, computed_map;
        for (int i = 0; i < f->func.dimensions(); i++) {
            required_map[f->region_required[i].min.as<Variable>()->name] = (int)bound.region_required[i].first;
            required_map[f->region_required[i].max.as<Variable>()->name] = (int)bound.region_required[i].second;
        }
        for (int i = 0; i < f->func.dimensions(); i++) {
            Interval in = f->region_computed[i];
            in.min = simplify(substitute(required_map, in.min));
            in.max = simplify(substitute(required_map, in.max));
            const int64_t *imin = as_const_int(in.min);
            const int64_t *imax = as_const_int(in.max);
            internal_assert(imin && imax) << in.min << ", " << in.max << '\n';
            bound.region_computed.push_back({*imin, *imax});
            computed_map[f->region_required[i].min.as<Variable>()->name] = (int)(*imin);
            computed_map[f->region_required[i].max.as<Variable>()->name] = (int)(*imax);
        }
        bound.iteration_domain_points = 0;
        for (const auto &s : f->stages) {
            vector<pair<int64_t, int64_t>> loop;
            int64_t prod = 1;
            for (const auto &l : s.loop) {
                Expr min = simplify(substitute(computed_map, l.min));
                Expr max = simplify(substitute(computed_map, l.max));
                const int64_t *imin = as_const_int(min);
                const int64_t *imax = as_const_int(max);
                internal_assert(imin && imax) << min << ", " << max << '\n';
                loop.push_back({*imin, *imax});
                prod *= (*imax) - (*imin) + 1;
            }
            bound.iteration_domain_points += prod;
            bound.loops.emplace_back(std::move(loop));
        }

        bounds[f] = std::move(bound);
        return bounds[f];
    }

    void dump(string prefix) const {
        if (!is_root()) {
            debug(0) << prefix << node->func.name();
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
            debug(0) << '\n';
        }
        for (auto p : store_at) {
            debug(0) << prefix << "realize: " << p->func.name() << '\n';
        }
        for (size_t i = children.size(); i > 0; i--) {
            children[i-1]->dump(prefix);
        }
        for (auto p : inlined) {
            debug(0) << prefix << "inlined: " << p.first->func.name() << " " << p.second << '\n';
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
            auto it = inlined.find(e->consumer);
            if (it != inlined.end()) {
                return true;
            }
        }
        return false;
    }

    bool computes(const FunctionDAG::Node *f) const {
        if (f == node) {
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
    PartialScheduleNode inline_func(const FunctionDAG::Node *f) const {
        PartialScheduleNode result = *this;

        // Inline it into the children
        for (size_t i = 0; i < result.children.size(); i++) {
            if (children[i]->calls(f)) {
                result.children[i] = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode(children[i]->inline_func(f)));
            }
        }

        // Inline it here if there are any direct calls
        if (innermost) {
            int64_t calls = 0;
            for (const auto *e : f->outgoing_edges) {
                auto it = inlined.find(e->consumer);
                if (it != inlined.end()) {
                    calls += it->second * e->calls;
                }
                if (e->consumer == node) {
                    calls += e->calls;
                }
            }
            if (calls) {
                result.inlined[f] = calls;
            }
        }
        return result;
    }

    void compute_here(const FunctionDAG::Node *f, bool tileable) {
        auto bounds = get_bounds(f);
        for (int s = (int)f->stages.size() - 1; s >= 0; s--) {
            auto node = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode);
            node->node = f;
            node->stage_idx = s;
            node->stage = &f->stages[s];
            node->innermost = true;
            // TODO: rvars are not tileable
            node->tileable = tileable;
            Bound single_point;
            single_point.loops.resize(f->stages.size());
            single_point.iteration_domain_points = 1;
            for (const auto &l : bounds.loops[s]) {
                // Initialize the loop nest
                node->size.push_back(l.second - l.first + 1);
                // Pick a representative loop iteration for the inner
                // loop. With the way tiling is done below, it needs
                // to be the first loop iteration.
                single_point.loops[s].push_back({l.first, l.first});
            }
            // Leave region required blank inside the computation of a Func
            node->bounds[f] = single_point;
            children.emplace_back(std::move(node));
        }
    }

    // Return all possible ways to compute f in tiles.
    vector<PartialScheduleNode> compute_in_tiles(const FunctionDAG::Node *f,
                                                 const PartialScheduleNode *parent,
                                                 const Constraints *constraints,
                                                 const MachineParams &params,
                                                 bool in_realization) const {
        internal_assert(f);
        internal_assert(constraints);

        vector<PartialScheduleNode> result;

        // Is it worth descending into this loop? If we don't end up doing less work, it's pointless.
        if (false && parent) {
            int64_t parent_points = parent->get_bounds(f).iteration_domain_points;
            int64_t in_loop_points = get_bounds(f).iteration_domain_points;
            if (parent_points <= in_loop_points) {
                return result;
            }
        }

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

        int vector_size = is_root() ? 1 : stage->vector_size;
        int vector_dim = 0;
        if (!is_root()) {
            const auto &l = stage->loop;
            while (vector_dim < (int)l.size() && !l[vector_dim].pure) vector_dim++;
        }

        if ((!is_root() || constraints->may_root(f)) &&
            !innermost &&
            (!in_realization || size[vector_dim] == 1)) {
            // Place the computation inside this loop
            PartialScheduleNode r = *this;
            r.compute_here(f, is_root() || constraints->may_subtile());
            if (!in_realization) {
                r.store_at.insert(f);
            } else {
                r.tileable = false;
            }

            result.emplace_back(std::move(r));
        }

        if (f->outgoing_edges.empty() || constraints->must_root(f)) {
            // Not permitted to compute at tiles of some consumer
            return result;
        }

        if (tileable) {
            // Generate a list of tile sizes to try
            auto tilings = generate_tilings(size, (int)(size.size() - 1), constraints->tiling_factor(), !in_realization, vector_dim, innermost ? vector_size : 1);

            for (auto t : tilings) {
                if (parent->is_root()) {
                    const auto &l = stage->loop;
                    // Skip root-level tilings that provide
                    // insufficient parallelism to avoid nested
                    // parallelism, and root-level tilings that would
                    // force serialization of dimensions we have
                    // decided to parallelize over in an earlier pass.
                    bool good = true;
                    int total = 1;
                    size_t idx = 0;
                    for (auto s : t) {
                        if (l[idx].pure) {
                            total *= s;
                        }
                        good &= (s == 1) || constraints->may_parallelize(stage, idx);
                        idx++;
                    }
                    if (!good || total < params.parallelism) continue;
                }



                // Skip tilings of the innermost loop that leave too few loop iterations to vectorize well
                /*
                if (innermost) {
                    const auto &l = stage->loop;
                    int innermost_pure_loop_extent = 1;
                    int innermost_split_factor = 1;
                    for (size_t i = 0; i < t.size(); i++) {
                        if (l[i].pure) {
                            innermost_pure_loop_extent = size[i];
                            innermost_split_factor = t[i];
                            break;
                        }
                    }
                    if (innermost_split_factor > 1 &&
                        innermost_pure_loop_extent / innermost_split_factor < 64) {
                        continue;
                    }
                }
                */

                // Tile this loop and place the computation at some coarser granularity
                PartialScheduleNode outer = *this;

                // First make an inner loop representing a 1x1x1... tile
                auto inner = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode);
                inner->size.resize(outer.size.size(), 1);
                inner->node = node;
                inner->stage = stage;
                inner->stage_idx = stage_idx;
                inner->innermost = innermost;
                inner->tileable = tileable && constraints->may_subtile();

                // Move the existing children and their bounds to the inner loop
                std::swap(inner->children, outer.children);
                std::swap(inner->inlined, outer.inlined);
                std::swap(inner->bounds, outer.bounds);
                std::swap(inner->store_at, outer.store_at);

                outer.bounds[node] = inner->bounds[node];
                outer.innermost = false;
                outer.tileable &= constraints->may_subtile();

                // Then move factors from the outer loop to the inner loop
                auto parent_bounds = parent->get_bounds(node);
                auto &b = outer.bounds[node];

                // We're within the computation of a single stage of a
                // Func, so the bounds should have empty regions and a
                // single loop nest
                internal_assert(b.region_required.empty());
                internal_assert(b.region_computed.empty());

                int64_t old_stage_iteration_domain_points = 1,
                    new_inner_iteration_domain_points = 1,
                    new_outer_iteration_domain_points = 1;

                for (size_t i = 0; i < t.size(); i++) {
                    old_stage_iteration_domain_points *= b.loops[stage_idx][i].second - b.loops[stage_idx][i].first + 1;
                    int factor = t[i];
                    inner->size[i] = (outer.size[i] + factor - 1) / factor;
                    outer.size[i] = factor;
                    int64_t min = parent_bounds.loops[stage_idx][i].first;
                    int64_t extent = parent_bounds.loops[stage_idx][i].second - min + 1;
                    extent = (extent + factor - 1) / factor;
                    b.loops[stage_idx][i] = {min, min + extent - 1};
                    new_outer_iteration_domain_points *= extent;
                    new_inner_iteration_domain_points *= factor;
                }

                // The number of points in an iteration domain is inclusive of children
                new_outer_iteration_domain_points *= new_inner_iteration_domain_points;

                b.iteration_domain_points += new_outer_iteration_domain_points - old_stage_iteration_domain_points;
                inner->bounds[node].iteration_domain_points = new_inner_iteration_domain_points;

                outer.children.push_back(inner);

                // Site the computation inside the outer loop
                PartialScheduleNode compute_at_here = outer;
                compute_at_here.compute_here(f, constraints->may_subtile());
                if (!in_realization) {
                    compute_at_here.store_at.insert(f);
                } else {
                    compute_at_here.tileable = false;
                }

                result.emplace_back(std::move(compute_at_here));

                bool may_slide = (!in_realization &&
                                  f->stages.size() == 1);
                if (may_slide) {
                    // Also consider just storing here, but computing
                    // further in. Currently don't have to worry about
                    // the constraints this places on parallelism, as
                    // we forced all the parallelism to the outer
                    // loop.
                    PartialScheduleNode store_at_here = std::move(outer);
                    store_at_here.store_at.insert(f);
                    auto v = inner->compute_in_tiles(f, &store_at_here, constraints, params, true);
                    for (PartialScheduleNode n : v) {
                        store_at_here.children.pop_back();
                        store_at_here.children.emplace_back(new PartialScheduleNode(std::move(n)));
                        result.push_back(store_at_here);
                    }
                }
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
                auto v = children[child]->compute_in_tiles(f, this, constraints, params, store_here);
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

        return result;
    }

    struct FuncVars {
        double num_cores = 0; // How much parallelism do we need to exploit with this Func?
        struct FuncVar {
            VarOrRVar orig;
            VarOrRVar var;
            int64_t extent = 0;
            bool outermost = false, parallel = false, exists = false;
            FuncVar() : orig(Var()), var(Var()) {}
        };
        vector<FuncVar> vars; // In order from innermost to outermost. Each group of d is one tiling.
    };

    void apply(LoopLevel here,
               map<const FunctionDAG::Node::Stage *, FuncVars> &vars_map,
               double num_cores,
               const PartialScheduleNode *parent) {
        if (is_root()) {
            for (auto &c : children) {
                Func(c->node->func).compute_root();
                c->apply(LoopLevel::root(), vars_map, num_cores, this);
            }
        } else {
            auto it = vars_map.find(stage);
            const auto &symbolic_loop = stage->loop;
            const auto &parent_bounds = parent->get_bounds(node);
            if (it == vars_map.end()) {
                FuncVars vars;
                vars.num_cores = num_cores;
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    FuncVars::FuncVar fv;
                    const auto &l = symbolic_loop[i];
                    fv.var = VarOrRVar(l.var, !l.pure);
                    fv.orig = fv.var;
                    fv.extent = parent_bounds.loops[stage_idx][i].second - parent_bounds.loops[stage_idx][i].first + 1;
                    fv.outermost = true;
                    fv.parallel = false;
                    fv.exists = true;
                    vars.vars.push_back(fv);
                }
                vars_map[stage] = vars;
            }
            auto &vars = vars_map[stage];

            debug(0) << "Scheduling " << node->func.name() << " stage " << stage << '\n';
            Stage s = Func(node->func);
            if (stage_idx > 0) {
                s = Func(node->func).update(stage_idx - 1);
            }

            if (stage_idx == 0 && parent->node != node) {
                // Pick a memory type
                double bytes = node->bytes_per_point;
                for (const auto &p : parent_bounds.region_computed) {
                    bytes *= p.second - p.first + 1;
                }
                if (bytes < 64000) {
                    Func(node->func).store_in(MemoryType::Stack);
                }
            }

            if (!size.empty()) {
                if (innermost) {
                    // Find the innermost var, and the innermost pure var
                    FuncVars::FuncVar *innermost_var = nullptr, *innermost_pure_var = nullptr;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
                        if (!vars.vars[i].exists) continue;
                        if (innermost_var == nullptr) {
                            innermost_var = &vars.vars[i];
                        }
                        if (innermost_pure_var == nullptr && symbolic_loop[i].pure) {
                            innermost_pure_var = &vars.vars[i];
                        }
                        if (innermost_var && innermost_pure_var) break;
                    }
                    internal_assert(innermost_var);
                    here = LoopLevel(node->func, innermost_var->var);

                    int vector_size = stage->vector_size;
                    if (innermost_pure_var && vector_size > 1) {
                        int split_factor = 1;
                        if (innermost_pure_var->extent >= vector_size) {
                            split_factor = vector_size;
                        } else if (innermost_pure_var->extent >= 16) {
                            split_factor = 16;
                        } else if (innermost_pure_var->extent >= 8) {
                            split_factor = 8;
                        } else if (innermost_pure_var->extent >= 4) {
                            split_factor = 4;
                        }
                        if (split_factor > 1) {
                            s.vectorize(innermost_pure_var->var, split_factor);
                        }
                        innermost_pure_var->extent += split_factor - 1;
                        innermost_pure_var->extent /= split_factor;
                    }
                } else {
                    // Do the implied splits
                    vector<FuncVars::FuncVar> new_inner;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
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
                            VarOrRVar outer(Var(parent.var.name() + "o"));
                            VarOrRVar inner(Var(parent.var.name() + "i"));
                            if (parent.var.is_rvar) {
                                outer = RVar(parent.var.name() + "o");
                                inner = RVar(parent.var.name() + "i");
                            }
                            debug(0) << "Splitting " << parent.var.name() << " by " << factor << '\n';
                            if (!parent.var.is_rvar && parent.extent % factor == 0 && stage == 0) {
                                // TODO: Use roundup if this is not the output and the loop nest is not reading any inputs
                                // otherwise must use guardwithif
                                s.split(parent.var, outer, inner, (int)factor, TailStrategy::Auto);
                            } else if (stage > 0) {
                                // Default is RoundUp, but that can create situations that read out of bounds on the input
                                s.split(parent.var, outer, inner, (int)factor, TailStrategy::GuardWithIf);
                            } else {
                                s.split(parent.var, outer, inner, (int)factor);
                            }
                            v = parent;
                            parent.var = outer;
                            parent.extent = size[i];
                            v.var = inner;
                            v.extent = factor;
                        }
                        new_inner.push_back(v);
                    }
                    for (int i = 0; i < node->func.dimensions(); i++) {
                        if (!vars.vars[i].exists) continue;
                        here = LoopLevel(node->func, vars.vars[i].var);
                        break;
                    }
                    vars.vars.insert(vars.vars.begin(), new_inner.begin(), new_inner.end());
                }
            }
            for (auto f : store_at) {
                Func(f->func).store_at(here);
            }
            for (auto s : size) {
                num_cores /= s;
            }
            for (auto &c : children) {
                if (c->node != node) {
                    Func(c->node->func).compute_at(here);
                }
                c->apply(here, vars_map, num_cores, this);
            }
        }
    }

};

struct State {
    PartialScheduleNode root;

    double cost = 0;

    int num_funcs_scheduled = 0;

    static int cost_calculations;

    uint64_t structural_hash(int depth) const {
        uint64_t h = 0;
        root.structural_hash(h, depth);
        return h;
    }

    bool calculate_cost(const FunctionDAG &dag, const MachineParams &params, ThroughputPredictorPipeline *throughput_predictor,  bool verbose = false) {
        map<const FunctionDAG::Node *, const PartialScheduleNode *> compute_site, store_site;
        map<const FunctionDAG::Node *, vector<ScheduleFeatures>> features;
        root.get_compute_sites(compute_site, store_site);
        root.compute_features(params, compute_site, store_site, 1, 1, nullptr, root, nullptr, &features);

        if (verbose) {
            for (const auto &n : dag.nodes) {
                const auto &sched_feat = features[&n];
                if (sched_feat.size() < n.stages.size()) {
                    // This Func hasn't been scheduled yet.
                    break;
                }
                for (size_t stage_idx = n.stages.size(); stage_idx > 0; stage_idx--) {
                    const auto &s = n.stages[stage_idx - 1];
                    debug(0) << "YYY ";
                    debug(0) << n.func.name() << ' ' << (stage_idx - 1) << ' ';
                    const int64_t *sched_stats = (const int64_t *)(&sched_feat[stage_idx - 1]);
                    for (size_t i = 0; i < sizeof(ScheduleFeatures) / sizeof(int64_t); i++) {
                        // The schedule-based features are all
                        // naturally multiplicative and have a very
                        // large dynamic range, so we emit them
                        // logged
                        debug(0) << std::log(1 + sched_stats[i]) << ' ';
                    }
                    const int *stats = (const int *)(&s.features);
                    for (size_t i = 0; i < sizeof(s.features) / sizeof(int); i++) {
                        debug(0) << stats[i] << ' ';
                    }
                    debug(0) << '\n';
                }
            }
        }


        cost = 0;

        // use either deep network or linear model to predict cost
        if (throughput_predictor) {
            // for complicated indexing reasons we do zero padding here
            // count number of scheduled stages
            int num_stages = 0;
            for (auto p : features) {
                num_stages += p.second.size();
            }

            const int pipeline_feat_size = 399;
            const int schedule_feat_size = 25;

            Buffer<float> pipeline_features, schedule_features;
            // Won't actually run anything until we call evaluate_costs...
            int batch_idx = throughput_predictor->enqueue(num_stages, &pipeline_features, &schedule_features, &cost);

            // index of current stage whose features we are reading
            int stage = 0;
            // load pipeline features into input buffer
            for (const auto &n : dag.nodes) {
                if (stage >= num_stages) break;
                for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
                    const auto &s = *it;
                    const int *pipeline_feats = (const int *)(&(s.features));

                    // skip the first 7 features
                    for (int i = 7; i < pipeline_feat_size-1; i++) {
                        int x = (i-7)/7;
                        int y = (i-7)%7;
                        pipeline_features(batch_idx, x, y, stage) = pipeline_feats[i];
                    }
                    stage += 1;
                }
            }

            stage = 0;

            // load schedule features into input buffer
            for (const auto &n : dag.nodes) {
                if (stage >= num_stages) break;
                const auto &feats = features.at(&n);
                for (auto it = feats.rbegin(); it != feats.rend(); it++) {
                    const auto &feat = *it;
                    if (feat.points_computed_total + feat.inlined_calls > 10*feat.points_computed_minimum) return false;
                    const int64_t *sched_stats = (const int64_t *)(&feat);
                    for (int i = 0; i < schedule_feat_size; i++) {
                        schedule_features(batch_idx, i, stage) = sched_stats[i];
                    }

                    stage += 1;
                }
            }
        } else {
            // We have no throughput predictor.
            for (auto p : features) {
                for (size_t s = 0; s < p.second.size(); s++) {
                    const auto &feat = p.second[s];
                    // Reject silly schedules. They're not even useful for
                    // training data, as they potentially take the age of
                    // the universe to benchmark. We define 'silly' as
                    // doing more than 10x redundant recompute for any one
                    // stage.
                    //if (feat.points_computed_total + feat.inlined_calls > 10*feat.points_computed_minimum) return false;

                    if (verbose) {
                        debug(0) << "Schedule features for " << p.first->func.name() << " stage " << s << "\n";
                        feat.dump();
                    }

                    auto &stage = p.first->stages[s];
                    double compute_cost = 0;
                    const int *pipeline_feat = (const int *)(&stage.features.op_histogram[0][0]);
                    double per_element_compute_cost = 0;
                    for (size_t i = 0; i < sizeof(stage.features.op_histogram) / sizeof(int); i++) {
                        per_element_compute_cost += pipeline_feat[i];
                    }

                    // We can only compute in multiples of the vector size
                    /*
                    if (feat.inlined_calls == 0) {
                        int64_t vectors = (feat.innermost_pure_loop_extent + stage.vector_size - 1) / stage.vector_size;
                        vectors *= feat.points_computed_total / feat.innermost_pure_loop_extent;
                        compute_cost = per_element_compute_cost * vectors * stage.vector_size;
                        // TODO: doesn't capture compute inflation on stages inlined into this one! Need vector overcompute as a feature, probably
                    }
                    */
                    compute_cost = per_element_compute_cost * feat.points_computed_total;

                    // Figure out vector overcompute
                    const int native_vector_size = feat.native_vector_size;
                    const double idle_simd_lanes = (double)native_vector_size / feat.vector_size;
                    const double vector_recompute = (double)feat.rounded_innermost_pure_loop_extent / feat.innermost_pure_loop_extent;

                    // Inlining saves a call node, which in our cost
                    // model costs...
                    const double per_element_compute_cost_of_memcpy = 1 + 2*p.first->func.dimensions();
                    const double per_element_compute_cost_inlined = std::max(0.0, per_element_compute_cost - per_element_compute_cost_of_memcpy);
                    const double compute_cost_inlined = per_element_compute_cost_inlined * feat.inlined_calls;
                    compute_cost += compute_cost_inlined;

                    compute_cost *= idle_simd_lanes * vector_recompute;

                    if (verbose) {
                        debug(0) << "idle_simd_lanes = " << idle_simd_lanes << "\n";
                        debug(0) << "vector_recompute = " << vector_recompute << "\n";
                    }

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

                    double cache_misses = 0, cost_of_miss = 0;
                    if (feat.inlined_calls == 0) {
                        // Estimate the number of cache misses on the data that this reads from and their cost
                        // Cost dominated by lines not bytes due to streaming prefetchers
                        cache_misses = feat.lines_read_per_realization + feat.bytes_read_per_realization * 1e-3;
                        cache_misses *= feat.num_realizations;
                        //int64_t footprint = std::min(feat.allocation_bytes_read_per_realization, feat.bytes_read_per_realization);
                        int64_t footprint = feat.allocation_bytes_read_per_realization;
                        //cost_of_miss = std::sqrt(footprint) * params.balance * 5e-3;
                        cost_of_miss = footprint * params.balance * 1e-6;
                    }

                    double memory_load_cost = cache_misses * cost_of_miss;

                    cache_misses = cost_of_miss = 0;
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
                        debug(0) << "Cost model for " << p.first->func.name()
                                 << " stage " << s << ": "
                                 << compute_cost << " + "
                                 << memory_load_cost << " + "
                                 << memory_store_cost << " + "
                                 << cost_of_mallocs << " + "
                                 << cost_of_working_set << '\n';
                    }

                    cost += compute_cost + memory_load_cost + memory_store_cost + cost_of_mallocs + cost_of_working_set;
                }
            }
        }
        cost_calculations++;
        return true;
    }



    void generate_children(const FunctionDAG &dag,
                           const MachineParams &params,
                           const Constraints *constraints,
                           ThroughputPredictorPipeline *throughput_predictor,
                           std::function<void(State *)> &accept_child) {
        internal_assert(root.is_root());

        if (num_funcs_scheduled == (int)dag.nodes.size()) {
            return;
        }

        // Enumerate all legal ways to schedule the next Func
        const FunctionDAG::Node *node = &dag.nodes[num_funcs_scheduled];
        for (const auto *e : node->outgoing_edges) {
            internal_assert(root.computes(e->consumer))
                << "Partially scheduled code doesn't compute " << e->consumer->func.name()
                << ", which is one of the consumers of " << node->func.name();
        }

        if (!node->outgoing_edges.empty() && !root.calls(node)) {
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
        if (!constraints->must_root(node) && constraints->may_inline(node)) {
            // 1) Inline it
            if (node->stages.size() == 1 && !node->outgoing_edges.empty()) {
                auto child = new State(*this);
                child->root = child->root.inline_func(node);
                child->num_funcs_scheduled++;
                if (child->calculate_cost(dag, params, throughput_predictor)) {
                    internal_assert(child->root.computes(node)) << "Failed to inline " << node->func.name() << '\n';
                    num_children++;
                    accept_child(child);
                }
            }
        }

        if (!constraints->must_inline(node)) {
            // 2) Realize it somewhere
            auto tile_options = root.compute_in_tiles(node, nullptr, constraints, params, false);
            for (PartialScheduleNode n : tile_options) {
                auto child = new State(*this);
                child->root = std::move(n);
                child->num_funcs_scheduled++;
                if (child->calculate_cost(dag, params, throughput_predictor)) {
                    internal_assert(child->root.computes(node)) << "Failed to inject realization of " << node->func.name() << '\n';
                    num_children++;
                    accept_child(child);
                }
            }
        }

        if (num_children == 0) root.dump("");
        internal_assert(num_children > 0) << "Could not find any legal way to schedule Func " << node->func.name() << '\n';
    }

    void dump() const {
        debug(0) << "State with cost " << cost/(1.0e9) << ":\n";
        root.dump("");
    }

    void apply_schedule(const MachineParams &params) {
        map<const FunctionDAG::Node::Stage *, PartialScheduleNode::FuncVars> vars_map;
        root.apply(LoopLevel::root(), vars_map, params.parallelism, nullptr);

        for (auto &p : vars_map) {
            Stage stage(p.first->stage);

            // Do all the reorders
            vector<VarOrRVar> vars;
            for (auto &v : p.second.vars) {
                if (v.exists) vars.push_back(v.var);
            }

            stage.reorder(vars);

            // Parallelize a loop and pull it outermost
            vector<VarOrRVar> parallel_vars;
            double num_cores = p.second.num_cores;
            for (int i = (int)p.second.vars.size() - 1; i >= 0 && num_cores > 1; i--) {
                auto &v = p.second.vars[i];
                if (!v.exists || v.var.is_rvar) continue;
                int64_t extent = v.extent;
                num_cores /= extent;
                if (num_cores > 1) {
                    debug(0) << "Parallelizing " << v.var.var.name() << " entirely\n";
                    stage.parallel(vars.back());
                    parallel_vars.push_back(vars.back());
                    vars.pop_back();
                    continue;
                }

                int task_size = 1;
                // Enqueue at most 128 x num_cores parallel tasks
                while (num_cores < 1.0 / 128) {
                    num_cores *= 2;
                    task_size *= 2;
                }
                debug(0) << "Task size for " << stage.name() << ": " << task_size << '\n';
                Var outer;
                stage.split(v.var, outer, v.var, task_size).parallel(outer);
                // Reorder the parallel portion outermost
                vars.push_back(outer);
                parallel_vars.push_back(outer);
                stage.reorder(vars);
            }

            // Fuse the parallel vars
            /*
            for (size_t i = 1; i < parallel_vars.size(); i++) {
                stage.fuse(parallel_vars[i], parallel_vars[i-1], parallel_vars[i]);
            }
            */
        }
    }
};

int State::cost_calculations = 0;

struct CompareStates {
    bool operator()(const std::shared_ptr<State> &a, const std::shared_ptr<State> &b) const {
        return a->cost > b->cost;
    }
};

State optimal_schedule_pass(FunctionDAG &dag,
                            vector<Function> outputs,
                            const MachineParams &params,
                            const Constraints *constraints,
                            ThroughputPredictorPipeline *throughput_predictor,
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

    // An unsorted staging area for before costs have been calculated
    vector<std::shared_ptr<State>> unevaluated_states;

    std::function<void(State *)> enqueue_new_children = [&](State *s) {

        //debug(0) << "\n** Generated child: ";
        //s->dump();
        // s->calculate_cost(dag, params, nullptr, true);

        tick(double(s->num_funcs_scheduled) / dag.nodes.size());
        unevaluated_states.push_back(std::shared_ptr<State>(s));
    };

    for (int i = 0; ; i++) {
        decltype(q) pending;
        q.swap(pending);

        // Apply cost penalties to the queue according to structural uniqueness at the root level
        /*
        std::map<uint64_t, int> hashes;
        for (int depth = 1; depth < 1; depth++) {
            hashes.clear();
            while (!pending.empty()) {
                auto state = pending.top();
                pending.pop();
                uint64_t h = state->structural_hash(depth);
                state->cost *= (1 + hashes[h]);
                hashes[h]++;
                q.push(state);
            }
            q.swap(pending);
        }
        */

        int expanded = 0;
        while (expanded < beam_size && !pending.empty()) {

            auto state = pending.top();
            pending.pop();

            if (pending.size() > 1 && random_dropout()) {
                debug(0) << "Dropping state\n";
                continue;
            }

            if (state->num_funcs_scheduled == (int)dag.nodes.size()) {
                debug(0) << '\n';

                /*
                debug(0) << "Optimal state?\n";
                state->dump();

                debug(0) << "Rest of queue:\n";
                while (!pending.empty()) {
                    pending.top()->calculate_cost(dag, params, nullptr, true);
                    pending.top()->dump();
                    pending.pop();
                }
                */

                return *state;
            }


            /*
            if (state->num_funcs_scheduled > 0 &&
                dag.nodes[state->num_funcs_scheduled-1].func.name() == "conv_r_x") {
                if (true) { //expanded + 20 >= beam_size) {
                    debug(0) << "\n\n**** End of beam: (" << expanded << "):\n";
                    state->dump();
                }
                if (expanded == 166 || expanded == 167) {
                    state->calculate_cost(dag, params, nullptr, true);
                }
            }
            */

            /*
              debug(0) << "Expanding state:";
              state->dump();
              state->calculate_cost(dag, params, nullptr, true);
            */

            state->generate_children(dag, params, constraints, throughput_predictor, enqueue_new_children);
            expanded++;
        }

        // Now evaluate all the costs and place them in the priority queue
        if (throughput_predictor) {
            throughput_predictor->evaluate_costs();
        }
        for (auto s : unevaluated_states) {
            q.push(s);
        }
        unevaluated_states.clear();
    }
}

State optimal_schedule(FunctionDAG &dag,
                       vector<Function> outputs,
                       const MachineParams &params,
                       ThroughputPredictorPipeline *throughput_predictor,
                       int beam_size) {
    FinePassConstraints fine;
    CoarsePassConstraints coarse(params);
    auto coarse_pass = optimal_schedule_pass(dag, outputs, params, &coarse, throughput_predictor, beam_size);

    debug(0) << "\nCoarse pass result:\n";
    coarse_pass.dump();

    // Respect which things were compute_root and which axes of those were parallelized for the fine pass
    debug(0) << "Deriving constraints from coarse pass:\n";
    for (auto c : coarse_pass.root.children) {
        fine.roots.insert(c->node);
        debug(0) << ' ' << c->node->func.name() << " is compute_root\n";
        for (int d = 0; d < (int)(c->size.size()); d++) {
            if (c->size[d] > 1) {
                fine.permit_parallelization(c->stage, d);
            }
        }
    }

    auto fine_pass = optimal_schedule_pass(dag, outputs, params, &fine, throughput_predictor, beam_size);

    debug(0) << "\nFine pass result:\n";
    fine_pass.dump();

    // It's not necessarily true that the fine_pass, with its larger
    // branching factor, ends up at a lower total cost than the coarse
    // pass.
    return coarse_pass.cost < fine_pass.cost ? coarse_pass : fine_pass;
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

    auto w = AutoScheduleModel::load_weights();
    auto stats = AutoScheduleModel::load_stats();

    ThroughputPredictorPipeline throughput_predictor(w, stats);
    ThroughputPredictorPipeline *tp = &throughput_predictor;
    if (get_env_variable("HL_USE_MANUAL_COST_MODEL") == "1") {
        tp = nullptr;
    }

    State optimal;

    if (time_limit) {
        // Use a fixed running time
        auto start = std::chrono::steady_clock::now();
        for (size_t beam_size = 1; ; beam_size *= 2) {
            State s = optimal_schedule(dag, outputs, params, tp, beam_size);
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
        optimal = optimal_schedule(dag, outputs, params, tp, beam_size);
    }

    debug(0) << "Cost evaluated this many times: " << State::cost_calculations << '\n';

    debug(0) << "** Optimal schedule:\n";
    optimal.dump();

    // Just to get the debugging prints to fire
    optimal.calculate_cost(dag, params, tp, true);

    // Apply the schedules
    optimal.apply_schedule(params);

    // Print out the predicted runtime of each Func, so we can compare them to a profile
    // optimal.print_predicted_runtimes(params);


    return "";
}

void test_convnet_correctness() {
    int n = 1;
    int stages = 10;

    Halide::Buffer<float> pipeline_features;
    Halide::Buffer<float> schedule_features;
    double cost;

    auto w = AutoScheduleModel::load_weights();
    auto stats = AutoScheduleModel::load_stats();

    ThroughputPredictorPipeline throughput_predictor(w, stats);

    throughput_predictor.enqueue(10, &pipeline_features, &schedule_features, &cost);

    std::default_random_engine generator;
    std::normal_distribution<float> distribution(0.0,1.0);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 56; j++) {
            for (int k = 0; k < 7; k++) {
                for (int l = 0; l < stages; l++) {
                    float val = distribution(generator);
                    pipeline_features(i, j, k, l) = val;
                }
            }
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 25; j++) {
            for (int k = 0; k < stages; k++) {
                float val = distribution(generator);
                schedule_features(i, j, k) = val;
            }
        }
    }

    FILE *fpipe = fopen("/private/home/karimacma/Halide/pipeline.data", "ab");
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 56; j++) {
            for (int k = 0; k < 7; k++) {
                for (int l = 0; l < stages; l++) {
                    fwrite(&(pipeline_features(i, j, k, l)), sizeof(float), 1, fpipe);
                }
            }
        }
    }
    fclose(fpipe);

    FILE *fsched = fopen("/private/home/karimacma/Halide/schedule.data", "ab");
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 25; j++) {
            for (int k = 0; k < stages; k++) {
                fwrite(&(schedule_features(i,j,k)), sizeof(float), 1, fsched);
            }
        }
    }
    fclose(fsched);

    throughput_predictor.evaluate_costs();

    FILE *fpred = fopen("/private/home/karimacma/Halide/prediction.data", "ab");
    for (int i = 0; i < n; i++) {
        float c = cost;
        fwrite(&c, sizeof(float), 1, fpred);
    }
    fclose(fpred);

    FILE *fstages = fopen("/private/home/karimacma/Halide/stages.data", "ab");
    fwrite(&stages, sizeof(int), 1, fstages);

    fwrite(&n, sizeof(int), 1, fstages);
    fclose(fstages);
}

void autoschedule_test() {
    // test_convnet_correctness();

    MachineParams params(16, 16 * 1024 * 1024, 40);
    size_t beam_size = 1;
    // Use a fixed target for the analysis to get consistent results from this test.
    Target target("x86-64-linux-sse41-avx-avx2");

    Weights w = load_weights();
    Stats stats = load_stats();

    ThroughputPredictorPipeline throughput_predictor(w, stats);

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

        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';

        optimal.apply_schedule(params);
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
        State optimal = optimal_schedule(dag, outputs, cheap_memory, &throughput_predictor, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';

        optimal.apply_schedule(params);
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
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';

        optimal.apply_schedule(params);
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
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';

        optimal.apply_schedule(params);
        h.realize(2048, 2048);

        // optimal.print_predicted_runtimes(dag, params);
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
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, 1);
        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';

        // optimal.apply_schedule(params);
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
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, beam_size);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';
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
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';
    }

    // A Func with multiple stages, some of which include additional loops
    {
        Buffer<float> a(1024, 1024);
        Func f("multiple_stages"), g("g"), h("h");
        Var x, y;
        h(x, y) = pow(x, y);
        f(x, y) = a(x, y) * 2;
        f(x, y) += 17;
        RDom r(0, 10);
        f(x, y) += r * h(x, y);
        f(x, y) *= 2;
        f(0, y) = 23.0f;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.estimate(x, 1, 1022).estimate(y, 1, 1022);

        vector<Function> outputs = {g.function()};
        FunctionDAG dag(outputs, params, target);
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, 4);

        dag.dump();

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';
    }

    {
        // A scan with pointwise stages before and after
        Buffer<float> a(1024, 1024);
        Func before[5];
        Func after[5];
        Func s("scan");
        Var x, y;
        before[0](x, y) = x + y;
        for (int i = 1; i < 5; i++) {
            before[i](x, y) = before[i-1](x, y) + 1;
        }
        RDom r(1, 1023);
        s(x, y) = before[4](x, y);
        s(r, y) += s(r-1, y);
        after[0](x, y) = s(x, y);
        for (int i = 1; i < 5; i++) {
            after[i](x, y) = after[i-1](x, y) + 1;
        }

        after[4].estimate(x, 0, 1024).estimate(y, 0, 1024);

        vector<Function> outputs = {after[4].function()};
        FunctionDAG dag(outputs, params, target);
        dag.dump();
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';
    }

    {
        // A scan in x followed by a downsample in y, with pointwise stuff in between
        const int N = 3;
        Buffer<float> a(1024, 1024);
        Func p1[N], p2[N], p3[N];
        Func s("scan");
        Var x, y;
        p1[0](x, y) = x + y;
        for (int i = 1; i < N; i++) {
            p1[i](x, y) = p1[i-1](x, y) + 1;
        }
        RDom r(1, 1023);
        s(x, y) = p1[N-1](x, y);
        s(r, y) += s(r-1, y);
        p2[0](x, y) = s(x, y);
        for (int i = 1; i < N; i++) {
            p2[i](x, y) = p2[i-1](x, y) + 1;
        }
        Func down("downsample");
        down(x, y) = p2[N-1](x, 2*y);
        p3[0](x, y) = down(x, y);
        for (int i = 1; i < N; i++) {
            p3[i](x, y) = p3[i-1](x, y) + 1;
        }

        p3[N-1].estimate(x, 0, 1024).estimate(y, 0, 1024);

        vector<Function> outputs = {p3[N-1].function()};
        FunctionDAG dag(outputs, params, target);
        dag.dump();
        State optimal = optimal_schedule(dag, outputs, params, nullptr, 1); //&throughput_predictor, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, nullptr, true); //&throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';
    }

    {
        Func f_u8("f_u8");
        Func f_u64_1("f_u64_1");
        Func f_u64_2("f_u64_2");
        Buffer<uint8_t> a(1024 * 1024 + 2);

        Var x;
        f_u8(x) = (min(a(x) + 1, 17) * a(x+1) + a(x+2)) * a(x) * a(x) * a(x + 1) * a(x + 1);
        f_u64_1(x) = cast<uint64_t>(f_u8(x)) + 1;
        f_u64_2(x) = f_u64_1(x) * 3;

        // Ignoring the types, it would make sense to inline
        // everything into f_64_2 but this would vectorize fairly
        // narrowly, which is a waste of work for the first Func.

        f_u64_2.estimate(x, 0, 1024 * 1024);

        vector<Function> outputs = {f_u64_2.function()};
        FunctionDAG dag(outputs, params, target);
        dag.dump();
        State optimal = optimal_schedule(dag, outputs, params, &throughput_predictor, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.calculate_cost(dag, params, &throughput_predictor, true);
        throughput_predictor.evaluate_costs();
        optimal.dump();
        debug(0) << '\n';

    }

}

}
}
