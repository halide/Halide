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

        // The amount of compute done per point evaluated if
        // inlined. Only relevant for single-stage Fucs.
        double compute_if_inlined;

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

        struct Features {
            // A featurization of the compute done by this Func, to
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
                    debug(0) << "    Featurization for type " << type_names[i] << '\n'
                             << "     Op histogram: "
                             << "      Constant: " << op_histogram[(int)OpType::Const][i] << '\n'
                             << "      Cast: " << op_histogram[(int)OpType::Cast][i] << '\n'
                             << "      Variable: " << op_histogram[(int)OpType::Variable][i] << '\n'
                             << "      Param: " << op_histogram[(int)OpType::Param][i] << '\n'
                             << "      Add: " << op_histogram[(int)OpType::Add][i] << '\n'
                             << "      Sub: " << op_histogram[(int)OpType::Sub][i] << '\n'
                             << "      Mod: " << op_histogram[(int)OpType::Mod][i] << '\n'
                             << "      Mul: " << op_histogram[(int)OpType::Mul][i] << '\n'
                             << "      Div: " << op_histogram[(int)OpType::Div][i] << '\n'
                             << "      Min: " << op_histogram[(int)OpType::Min][i] << '\n'
                             << "      Max: " << op_histogram[(int)OpType::Max][i] << '\n'
                             << "      EQ: " << op_histogram[(int)OpType::EQ][i] << '\n'
                             << "      NE: " << op_histogram[(int)OpType::NE][i] << '\n'
                             << "      LT: " << op_histogram[(int)OpType::LT][i] << '\n'
                             << "      LE: " << op_histogram[(int)OpType::LE][i] << '\n'
                             << "      And: " << op_histogram[(int)OpType::And][i] << '\n'
                             << "      Or: " << op_histogram[(int)OpType::Or][i] << '\n'
                             << "      Not: " << op_histogram[(int)OpType::Not][i] << '\n'
                             << "      Select: " << op_histogram[(int)OpType::Select][i] << '\n'
                             << "      ImageCall: " << op_histogram[(int)OpType::ImageCall][i] << '\n'
                             << "      FuncCall: " << op_histogram[(int)OpType::FuncCall][i] << '\n'
                             << "      SelfCall: " << op_histogram[(int)OpType::SelfCall][i] << '\n'
                             << "      ExternCall: " << op_histogram[(int)OpType::ExternCall][i] << '\n'
                             << "      Let: " << op_histogram[(int)OpType::Let][i] << '\n'
                             << "     Call types. Subclasses are Func, Self, Image, Store\n"
                             << "      Pointwise: " << pointwise_accesses[0][i] << ' ' << pointwise_accesses[1][i] << ' ' << pointwise_accesses[2][i] << ' ' << pointwise_accesses[3][i] << '\n'
                             << "      Transpose: " << transpose_accesses[0][i] << ' ' << transpose_accesses[1][i] << ' ' << transpose_accesses[2][i] << ' ' << transpose_accesses[3][i] << '\n'
                             << "      Broadcast: " << broadcast_accesses[0][i] << ' ' << broadcast_accesses[1][i] << ' ' << broadcast_accesses[2][i] << ' ' << broadcast_accesses[3][i] << '\n'
                             << "      Slice: " << slice_accesses[0][i] << ' ' << slice_accesses[1][i] << ' ' << slice_accesses[2][i] << ' ' << slice_accesses[3][i] << '\n'
                             << "      Vectorizable: " << vectorizable_accesses[0][i] << ' ' << vectorizable_accesses[1][i] << ' ' << vectorizable_accesses[2][i] << ' ' << vectorizable_accesses[3][i] << '\n'
                             << "      Strided: " << strided_accesses[0][i] << ' ' << strided_accesses[1][i] << ' ' << strided_accesses[2][i] << ' ' << strided_accesses[3][i] << '\n'
                             << "      Scalar: " << scalar_accesses[0][i] << ' ' << scalar_accesses[1][i] << ' ' << scalar_accesses[2][i] << ' ' << scalar_accesses[3][i] << '\n'
                             << "      Gather_Scatter: " << gather_scatter_accesses[0][i] << ' ' << gather_scatter_accesses[1][i] << ' ' << gather_scatter_accesses[2][i] << ' ' << gather_scatter_accesses[3][i] << '\n';
                }
            }

        };

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
            Features features;
        };
        vector<Stage> stages;

        // Max vector size across the stages
        int vector_size;

    };

    struct Edge {
        Function producer, consumer;
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
    map<Function, vector<const Edge *>, Function::Compare> outgoing_edges, incoming_edges;
    map<Function, const Node *, Function::Compare> node_map;

    // Create the function DAG, and do all the dependency and cost
    // analysis. This is done once up-front before the tree search.
    FunctionDAG(const vector<Function> &outputs, const MachineParams &params, const Target &target) {
        map<string, Function> env;
        for (Function o : outputs) {
            populate_environment(o, env);
        }

        // A mutator to apply parameter estimates to the expressions
        // we encounter while constructing the graph.
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
                internal_assert(expr.defined()) << "Missing estimate for " << op->name << '\n';
            }
        } apply_param_estimates;

        // Compute a realization order
        vector<string> order = topological_order(outputs, env);

        for (size_t i = order.size(); i > 0; i--) {
            Function consumer = env[order[i-1]];

            Node node;
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
                Node::Stage stage;

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
                stage.compute = counter.leaves;
                if (s == 0) {
                    node.compute_if_inlined = std::max(0, counter.leaves - 3 * consumer.dimensions());
                }

                int bytes_per_point = 0;
                for (const auto &e : def.values()) {
                    bytes_per_point += e.type().bytes();
                }
                // Assume things vectorize OK, so bill more for wider types that have lower vector throughput
                stage.compute *= bytes_per_point;
                if (s == 0) {
                    node.compute_if_inlined *= bytes_per_point;
                    node.bytes_per_point = bytes_per_point;
                }

                stage.vector_size = target.natural_vector_size(counter.narrowest_type);

                if (s == 0) {
                    node.vector_size = stage.vector_size;
                } else {
                    node.vector_size = std::max(node.vector_size, stage.vector_size);
                }

                node.stages.emplace_back(std::move(stage));

                debug(0) << "Exprs in stage " << s << " " << exprs << '\n';

                debug(0) << def.values()[0] << '\n';

                // Now create the edges that lead to this func
                for (auto p : boxes_required(exprs, stage_scope)) {
                    auto it = env.find(p.first);
                    if (it != env.end() && p.first != consumer.name()) {
                        // Discard loads from input images and self-loads
                        Edge edge;
                        edge.consumer = consumer;
                        edge.consumer_stage = s;
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

        // Compute features for the neural net
        featurize();
    }

    class Featurizer : public IRVisitor {
        Function &func;
        Node::Stage &stage;
        size_t vector_dim;

        Node::Features::ScalarType classify_type(Type t) {
            if (t.is_float() && t.bits() > 32) {
                return Node::Features::ScalarType::Double;
            } else if (t.is_float()) {
                return Node::Features::ScalarType::Float;
            } else if (t.bits() == 1) {
                return Node::Features::ScalarType::Bool;
            } else if (t.bits() <= 8) {
                return Node::Features::ScalarType::UInt8;
            } else if (t.bits() <= 16) {
                return Node::Features::ScalarType::UInt16;
            } else if (t.bits() <= 32) {
                return Node::Features::ScalarType::UInt32;
            } else {
                return Node::Features::ScalarType::UInt64;
            }
        }
        void visit(const Variable *op) override {
            if (op->param.defined()) {
                stage.features.op_histogram[(int)Node::Features::OpType::Param][(int)classify_type(op->type)]++;
            } else {
                stage.features.op_histogram[(int)Node::Features::OpType::Variable][(int)classify_type(op->type)]++;
            }
        }
        void visit(const IntImm *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Const][(int)classify_type(op->type)]++;
        }
        void visit(const UIntImm *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Const][(int)classify_type(op->type)]++;
        }
        void visit(const FloatImm *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Const][(int)classify_type(op->type)]++;
        }
        void visit(const Add *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Add][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Sub *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Sub][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Mul *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Mul][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Mod *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Mod][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Div *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Div][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Min *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Min][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Max *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Max][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const EQ *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::EQ][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const NE *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::NE][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const LT *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::LT][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const LE *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::LE][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const GT *op) override {
            // Treat as a flipped LT
            stage.features.op_histogram[(int)Node::Features::OpType::LT][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const GE *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::LE][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const And *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::And][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Or *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Or][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Not *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Not][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Select *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Select][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Let *op) override {
            stage.features.op_histogram[(int)Node::Features::OpType::Let][(int)classify_type(op->type)]++;
            IRVisitor::visit(op);
        }
        void visit(const Call *op) override {
            auto t = classify_type(op->type);
            if (op->call_type == Call::Halide) {
                if (op->name == func.name()) {
                    visit_memory_access(op->type, op->args, Node::Features::AccessType::LoadSelf);
                    stage.features.op_histogram[(int)Node::Features::OpType::SelfCall][(int)t]++;
                } else {
                    visit_memory_access(op->type, op->args, Node::Features::AccessType::LoadFunc);
                    stage.features.op_histogram[(int)Node::Features::OpType::FuncCall][(int)t]++;
                }
            } else if (op->call_type == Call::Extern || op->call_type == Call::PureExtern) {
                stage.features.op_histogram[(int)Node::Features::OpType::ExternCall][(int)t]++;
            } else if (op->call_type == Call::Image) {
                visit_memory_access(op->type, op->args, Node::Features::AccessType::LoadImage);
                stage.features.op_histogram[(int)Node::Features::OpType::ImageCall][(int)t]++;
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

        void visit_memory_access(Type t, const vector<Expr> &args, Node::Features::AccessType type) {
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
                    if (i == vector_dim) {
                        is_vector &= (j == 0 ? deriv.is_one() : deriv.is_zero());
                        is_strided &= (j == 0 ? deriv.is_small_integer() : deriv.is_zero());
                        is_scalar &= deriv.is_zero();
                    }
                    debug(0) << "Derivative of " << args[i]
                             << " w.r.t. " << stage.loop[j].var
                             << " = " << deriv.exists << " " << deriv.numerator << "/" << deriv.denominator << '\n';
                    (void)vector_dim;
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

            debug(0) << "Classified memory access:\n"
                     << " pointwise: " << is_pointwise << '\n'
                     << " transpose: " << is_transpose << '\n'
                     << " broadcast: " << is_broadcast << '\n'
                     << " slice: " << is_slice << '\n'
                     << " vectorizable: " << is_vector << '\n'
                     << " strided: " << is_strided << '\n'
                     << " scalar: " << is_scalar << '\n'
                     << " gather/scatter: " << is_gather_scatter << '\n';

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
            visit_memory_access(t, args, Node::Features::AccessType::Store);
        }
    };

    // Compute the featurization for the entire DAG
    void featurize() {
        for (Node &node : nodes) {
            for (size_t stage_idx = 0; stage_idx < node.stages.size(); stage_idx++) {
                Node::Stage &stage = node.stages[stage_idx];

                // Pick a dimension to vectorize over
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

    void dump() {
        for (const Node &n : nodes) {
            debug(0) << "Node: " << n.func.name() << '\n'
                     << "  Inlined cost: " << n.compute_if_inlined << '\n'
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
                debug(0) << "    Arithmetic cost: " << n.stages[i].compute << '\n';
                for (const auto &l : n.stages[i].loop) {
                    debug(0) << "    " << l.var << " " << l.min << " " << l.max << '\n';
                }
                n.stages[i].features.dump();
            }
        }
        for (const Edge &e : edges) {
            debug(0) << "Edge: " << e.producer.name() << " -> " << e.consumer.name() << '\n'
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

vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, bool allow_splits, int vector_dim, int vector_size) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        auto v = generate_tilings(s, d - 1, allow_splits, vector_dim, vector_size);
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
                    if (outer > inner || (d == vector_dim && inner < vector_size)) break;
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
    int stage;

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

    // TODO: Should stash pointers to the relevant dag objects here to
    // avoid have to look them up all the time.

    double cost(const FunctionDAG &dag,
                const MachineParams &params,
                map<Function, const PartialScheduleNode *, Function::Compare> &compute_site,
                map<Function, double, Function::Compare> &overcompute,
                int64_t instances,
                const PartialScheduleNode *parent,
                const PartialScheduleNode &root,
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
                const auto &s = dag.node_map.at(func)->stages[stage];
                int vector_size = s.vector_size;
                int64_t ideal_subinstances = subinstances;
                // We're going to vectorize the innermost pure loop
                size_t p = 0;
                while (!s.loop[p].pure && p < s.loop.size()) p++;
                if (p < s.loop.size()) {
                    subinstances /= size[p];
                    subinstances *= ((size[p] + vector_size - 1) / vector_size) * vector_size;

                    // Record overcompute due to vectorization
                    double factor = double(subinstances) / ideal_subinstances;

                    // Take the max over the stages
                    overcompute[func] = std::max(overcompute[func], factor);
                } else {
                    // No pure vars. Not expected to vectorize. Doesn't count as overcompute.
                    overcompute[func] = 1;
                }
            }
        }

        for (auto c : children) {
            result += c->cost(dag, params, compute_site, overcompute, subinstances, this, root, node_costs, edge_costs, inlined_costs);
        }

        // Bill compute and memory costs for all Funcs realized within this loop
        for (Function f : store_at) {
            const auto &bounds_realized = get_bounds(f, dag);

            const auto *node = dag.node_map.at(f);
            double compute_cost = 0;
            double points_computed = 1;
            for (auto p : bounds_realized.region_computed) {
                points_computed *= p.second - p.first + 1;
            }
            for (size_t s = 0; s < node->stages.size(); s++) {
                const auto &stage = node->stages[s];
                double stage_points = 1;
                for (const auto &p : bounds_realized.loops[s]) {
                    stage_points *= (p.second - p.first + 1);
                }
                double stage_compute_cost = stage.compute * stage_points;

                if (!bounds_realized.loops[s].empty()) {
                    // Penalize small inner loops
                    int64_t innermost_loop_extent = bounds_realized.loops[s][0].second - bounds_realized.loops[s][0].first + 1;
                    stage_compute_cost *= (innermost_loop_extent + 10.0) / innermost_loop_extent;
                }

                compute_cost += stage_compute_cost;
            }
            compute_cost *= subinstances;

            // Most recompute occurs due to there being multiple
            // overlapping realizations of a Func. However, we
            // must also account for recompute within a single
            // realization due to vectorization of the innermost
            // loop. Assume all other potential recompute is
            // avoided by sliding.
            compute_cost *= overcompute[f];

            // Now incorporate overcompute due to overlapping
            // realizations, so that we can print it as a feature for
            // the learned model and use it for early rejection of
            // schedules that do silly amounts of overcompute.
            const auto &root_bounds = root.get_bounds(f, dag);
            overcompute[f] *= (bounds_realized.iteration_domain_points * subinstances) / root_bounds.iteration_domain_points;

            // Compute a locality discount due to assumed storage folding.
            auto it = compute_site.find(f);
            internal_assert(it != compute_site.end());

            bool folded_over_vector_dim = false;

            double discount = 1;
            if (it->second != this) {
                const auto &bounds_computed = it->second->get_bounds(f, dag);
                for (size_t i = bounds_realized.region_computed.size(); i > 0; i--) {
                    auto r = bounds_realized.region_computed[i-1];
                    auto c = bounds_computed.region_computed[i-1];
                    int64_t er = r.second - r.first + 1;
                    int64_t ec = c.second - c.first + 1;
                    if (ec == er) {
                        continue;
                    }
                    discount = (double)ec / er;
                    if (i == 1) {
                        // By folding on the innermost dimension, you just broke vectorization. Don't do that.
                        folded_over_vector_dim = true;
                        if (node_costs) {
                            debug(0) << "Warning: Folding over innermost dimension: " << f.name() << '\n';
                        }

                        // Assume worst-case full scalarization.
                        const int vector_size = dag.node_map.at(f)->vector_size;
                        compute_cost *= vector_size;
                    }
                    break;
                }
                if (node_costs) {
                    debug(0) << "Folding discount for " << f.name() << ": " << discount << '\n';
                }

                // Flat penalty for storage folding. Only do it if there's a benefit.
                compute_cost += 1;
            }

            if (node_costs) {
                // TODO: Should this include inlined Funcs?
                (*node_costs)[node] = compute_cost;
            }

            result += compute_cost;

            // The memory cost is the number of cold loads times (in bytes) times the
            // cost per cold load. The discount reduces the cost per
            // cold load, but not the number of cold loads.
            double bytes_per_point = node->bytes_per_point;
            double allocation_size = bytes_per_point * points_computed * discount;
            double bytes_cold_loaded = bytes_per_point * points_computed;
            double mem_cost = subinstances * bytes_cold_loaded * cost_of_cold_load(allocation_size, params);

            if (node_costs) {
                // Outputs the featurization for this Func


                debug(0) << "YYY "
                         << (node - dag.nodes.data()) << " " // Topological order
                         << f.name() << " "                  // Func name
                         << compute_cost << " "              // Hand-designed compute cost
                         << mem_cost << " "                  // Hand-designed memory cost
                         << subinstances << " "              // Number of times the Func is realized
                    // TODO: figure out featurization that captures all stages
                         << node->stages[0].compute << " " // compute_cost per point
                         << node->compute_if_inlined << " "  // compute_cost per point when inlined
                    //                         << innermost_compute_extent << " "  // Innermost dimension of region over which it is realized
                         << overcompute[f] << " "            // Fraction of overcompute
                         << points_computed << " "             // Number of points in the region
                         << bytes_cold_loaded << " "         // Number of bytes in the region before folding
                         << allocation_size << " "           // Number of bytes in the region after folding
                         << "0 " // number of inlined calls per 'realization'
                         << (int)folded_over_vector_dim << '\n';

            }

            for (const auto *e : dag.outgoing_edges.at(f)) {
                if (edge_costs) {
                    (*edge_costs)[e] = mem_cost;
                }
                result += mem_cost;
            }
        }

        // Bill compute cost for all Funcs inlined in this loop
        for (auto p : inlined) {
            const auto *node = dag.node_map.at(p.first);
            double c = node->compute_if_inlined * subinstances * p.second;
            // debug(0) << "Inlined Func " << p.first.name() << " has compute cost " << c << '\n';
            result += c;
            if (inlined_costs) {
                (*inlined_costs)[p.first] += c;
            }

            // For inlineable Funcs, the points required is equal to
            // the iteration domain points (we don't inline things with
            // RDoms), so we can divide to get the overcompute.

            const auto &root_bounds = root.get_bounds(p.first, dag);
            overcompute[p.first] = (double)(subinstances * p.second) / root_bounds.iteration_domain_points;

            /*
            debug(0) << subinstances << " " << p.second << '\n';
            for (auto s : root_bounds.region) {
                debug(0) << s.first << " " << s.second << '\n';
            }
            */

            if (node_costs) {
                debug(0) << "YYY "
                         << (node - dag.nodes.data()) << " "       // Topological order
                         << p.first.name() << " "
                         << c << " "              // Hand-designed compute cost
                         << "0 "                  // Hand-designed memory cost
                         << subinstances << " "   // Number of times the Func is realized
                    // TODO: Figure out featurization that captures all stages (though we probably won't inline things with update defs)
                         << node->stages[0].compute << " "  // compute_cost per point
                         << node->compute_if_inlined << " "  // compute_cost per point when inlined
                         << "0 "                  // Innermost dimension of region over which it is realized
                         << overcompute[p.first] << " "  // Fraction of overcompute
                         << "0 "                  // Number of points in the region
                         << "0 "                  // Number of bytes in the region before folding
                         << "0 "                  // Number of bytes in the region after folding
                         << p.second << " "       // number of inlined calls per 'realization'
                         << "0\n";                // Did we fold over the vectorized dimension
                    }

        }

        return result;
    }

    bool is_root() const {
        return !func.get_contents().defined();
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
    mutable map<Function, Bound, Function::Compare> bounds;
    const Bound &get_bounds(Function f, const FunctionDAG &dag) const {
        // debug(0) << "get_bounds of " << f.name() << " in loop over " << (is_root() ? "root" : func.name()) << '\n';
        auto it = bounds.find(f);
        if (it != bounds.end()) {
            return it->second;
        }
        Bound bound;
        // Compute the region required
        if (dag.outgoing_edges.at(f).empty() && is_root()) {
            // It's an output.
            // Use the bounds estimate
            bound.iteration_domain_points = 1;
            map<string, pair<int64_t, int64_t>> estimates;
            for (auto b : f.schedule().estimates()) {
                int64_t i_min = *as_const_int(b.min);
                int64_t i_extent = *as_const_int(b.extent);
                estimates[b.var] = {i_min, i_min + i_extent - 1};
            }
            // Set the bounds using the estimates
            for (int i = 0; i < f.dimensions(); i++) {
                auto it = estimates.find(f.args()[i]);
                user_assert(it != estimates.end())
                    << "Need an estimate on dimension " << i << " of \"" << f.name() << "\"";
                bound.iteration_domain_points *= it->second.second - it->second.first + 1;
                bound.region_required.push_back(it->second);
            }
        } else {
            internal_assert(!dag.outgoing_edges.at(f).empty())
                << "No consumers of " << f.name()
                << " at loop over " << (is_root() ? "root" : func.name()) << '\n';
            for (const auto *e : dag.outgoing_edges.at(f)) {
                if (!bounds.count(e->consumer) && !calls(e->consumer, dag)) {
                    // debug(0) << "Skipping over " << e->consumer.name() << '\n';
                    continue;
                }
                const auto &c_bounds = get_bounds(e->consumer, dag);
                const auto *c_node = dag.node_map.at(e->consumer);
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
                    s[e->consumer.name() + "." + var + ".min"] = (int)p.first;
                    s[e->consumer.name() + "." + var + ".max"] = (int)p.second;
                }
                // Apply that map to the bounds relationship encoded
                // in the edge to expand the bounds of the producer to
                // satisfy the consumer
                for (int i = 0; i < f.dimensions(); i++) {
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
            internal_assert(bound.region_required.size() == (size_t)f.dimensions()) << is_root() << " " << f.name() << '\n';
        }

        // Use the region required and the dag to compute the region computed and the iteration domain
        const auto *node = dag.node_map.at(f);
        map<string, Expr> required_map;
        for (int i = 0; i < f.dimensions(); i++) {
            required_map[node->region_required[i].min.as<Variable>()->name] = (int)bound.region_required[i].first;
            required_map[node->region_required[i].max.as<Variable>()->name] = (int)bound.region_required[i].second;
        }
        for (int i = 0; i < f.dimensions(); i++) {
            Interval in = node->region_computed[i];
            in.min = simplify(substitute(required_map, in.min));
            in.max = simplify(substitute(required_map, in.max));
            const int64_t *imin = as_const_int(in.min);
            const int64_t *imax = as_const_int(in.max);
            internal_assert(imin && imax) << in.min << ", " << in.max << '\n';
            bound.region_computed.push_back({*imin, *imax});
        }
        bound.iteration_domain_points = 0;
        for (const auto &s : node->stages) {
            vector<pair<int64_t, int64_t>> loop;
            int64_t prod = 1;
            for (const auto &l : s.loop) {
                Expr min = simplify(substitute(required_map, l.min));
                Expr max = simplify(substitute(required_map, l.max));
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
            debug(0) << '\n';
        }
        for (auto p : store_at) {
            debug(0) << prefix << "realize: " << p.name() << '\n';
        }
        for (size_t i = children.size(); i > 0; i--) {
            children[i-1]->dump(prefix);
        }
        for (auto p : inlined) {
            debug(0) << prefix << "inlined: " << p.first.name() << " " << p.second << '\n';
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

    int64_t calls_per_instance(Function f, const FunctionDAG &dag) const {
        int64_t result = 0;
        for (const auto &c : children) {
            result += c->calls(f, dag);
        }
        for (const auto *e : dag.outgoing_edges.at(f)) {
            if (e->consumer.same_as(func) && e->consumer_stage == stage) {
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
        for (int s = (int)f.updates().size(); s >= 0; s--) {
            auto node = std::shared_ptr<PartialScheduleNode>(new PartialScheduleNode);
            node->func = f;
            node->stage = s;
            node->innermost = true;
            // TODO: rvars are not tileable
            node->tileable = true;
            Bound single_point;
            single_point.loops.resize(f.updates().size() + 1);
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
    vector<PartialScheduleNode> compute_in_tiles(Function f, const FunctionDAG &dag,
                                                 const PartialScheduleNode *parent,
                                                 const MachineParams &params,
                                                 bool in_realization) const {
        vector<PartialScheduleNode> result;

        // Is it worth descending into this loop? If we don't end up doing less work, it's pointless.
        if (parent) {
            int64_t parent_points = parent->get_bounds(f, dag).iteration_domain_points;
            int64_t in_loop_points = get_bounds(f, dag).iteration_domain_points;
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
            } else {
                r.tileable = false;
            }
            result.emplace_back(std::move(r));
        }

        if (dag.outgoing_edges.at(f).empty()) {
            // Can't tile outputs
            return result;
        }

        int vector_size = is_root() ? 1 : dag.node_map.at(func)->stages[stage].vector_size;

        if (tileable) {
            // Generate a list of tile sizes to try
            int vector_dim = 0;
            const auto &l = dag.node_map.at(func)->stages[stage].loop;
            while (vector_dim < (int)l.size() && !l[vector_dim].pure) vector_dim++;
            auto tilings = generate_tilings(size, (int)(size.size() - 1), !in_realization, vector_dim, vector_size);

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
                inner->stage = stage;
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

                // We're within the computation of a single stage of a
                // Func, so the bounds should have empty regions and a
                // single loop nest
                internal_assert(b.region_required.empty());
                internal_assert(b.region_computed.empty());

                int64_t old_stage_iteration_domain_points = 1,
                    new_inner_iteration_domain_points = 1,
                    new_outer_iteration_domain_points = 1;

                for (size_t i = 0; i < t.size(); i++) {
                    old_stage_iteration_domain_points *= b.loops[stage][i].second - b.loops[stage][i].first + 1;
                    int factor = t[i];
                    inner->size[i] = (outer.size[i] + factor - 1) / factor;
                    outer.size[i] = factor;
                    int64_t min = parent_bounds.loops[stage][i].first;
                    int64_t extent = parent_bounds.loops[stage][i].second - min + 1;
                    extent = (extent + factor - 1) / factor;
                    b.loops[stage][i] = {min, min + extent - 1};
                    new_outer_iteration_domain_points *= extent;
                    new_inner_iteration_domain_points *= factor;
                }
                // The number of points in an iteration domain is inclusive of children
                new_outer_iteration_domain_points *= new_inner_iteration_domain_points;

                b.iteration_domain_points += new_outer_iteration_domain_points - old_stage_iteration_domain_points;
                inner->bounds[func].iteration_domain_points = new_inner_iteration_domain_points;

                outer.children.push_back(inner);

                // Site the computation inside the outer loop
                PartialScheduleNode compute_at_here = outer;
                compute_at_here.compute_here(f, dag);
                if (!in_realization) {
                    compute_at_here.store_at.insert(f);
                } else {
                    compute_at_here.tileable = false;
                }
                result.emplace_back(std::move(compute_at_here));

                if (!in_realization && !f.has_update_definition()) {
                    // Also consider just storing here, but computing
                    // further in. Currently don't have to worry about
                    // the constraints this places on parallelism, as
                    // we forced all the parallelism to the outer
                    // loop.
                    PartialScheduleNode store_at_here = std::move(outer);
                    store_at_here.store_at.insert(f);
                    auto v = inner->compute_in_tiles(f, dag, &store_at_here, params, true);
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
            int num_ones = 0;
            for (auto s : size) {
                num_ones += (s == 1) ? 1 : 0;
            }
            bool may_slide = !is_root() && (num_ones == ((int)size.size() - 1)) && !f.has_update_definition();
            for (int store_here = 0; store_here < 2; store_here++) {
                if (store_here && !may_slide) {
                    // We place all our parallel loops at the root
                    // level, so this would constrain parallelism.
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

        internal_assert(!result.empty());

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

    struct FuncVarMapCompare {
        bool operator()(const pair<Function, int> &a, const pair<Function, int> &b) const {
            if (a.second < b.second) return true;
            if (a.second > b.second) return false;
            return Function::Compare()(a.first, b.first);
        }
    };

    void apply(LoopLevel here, const FunctionDAG &dag,
               map<pair<Function, int>, FuncVars, FuncVarMapCompare> &vars_map,
               double num_cores,
               const PartialScheduleNode *parent) {
        if (is_root()) {
            for (auto &c : children) {
                Func(c->func).compute_root();
                c->apply(LoopLevel::root(), dag, vars_map, num_cores, this);
            }
        } else {
            auto key = std::make_pair(func, stage);
            auto it = vars_map.find(key);
            const auto &symbolic_loop = dag.node_map.at(func)->stages[stage].loop;
            if (it == vars_map.end()) {
                const auto &parent_bounds = parent->get_bounds(func, dag);
                FuncVars vars;
                vars.num_cores = num_cores;
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    FuncVars::FuncVar fv;
                    const auto &l = symbolic_loop[i];
                    fv.var = VarOrRVar(l.var, !l.pure);
                    fv.extent = parent_bounds.loops[stage][i].second - parent_bounds.loops[stage][i].first + 1;
                    fv.outermost = true;
                    fv.parallel = false;
                    fv.exists = true;
                    vars.vars.push_back(fv);
                }
                vars_map[key] = vars;
            }
            auto &vars = vars_map[key];

            debug(0) << "Scheduling " << func.name() << " stage " << stage << '\n';
            Stage s = Func(func);
            if (stage > 0) {
                s = Func(func).update(stage - 1);
            }

            if (!size.empty()) {
                if (innermost) {
                    // Find the innermost var, and the innermost pure var
                    FuncVars::FuncVar innermost_var, innermost_pure_var;
                    bool found_innermost = false, found_innermost_pure = false;;
                    for (size_t i = 0; i < symbolic_loop.size() && !(found_innermost && found_innermost_pure); i++) {
                        if (vars.vars[i].exists) {
                            if (!found_innermost) {
                                found_innermost = true;
                                innermost_var = vars.vars[i];
                            }
                            if (!found_innermost_pure && symbolic_loop[i].pure) {
                                found_innermost_pure = true;
                                innermost_pure_var = vars.vars[i];
                            }
                        }
                    }
                    internal_assert(found_innermost);
                    here = LoopLevel(func, innermost_var.var);

                    if (found_innermost_pure) {
                        int vector_size = dag.node_map.at(func)->stages[stage].vector_size;
                        if (innermost_pure_var.extent >= 2 * vector_size &&
                            (((innermost_pure_var.extent + vector_size - 1) / vector_size) & 1) == 0) {
                            s.vectorize(innermost_pure_var.var, 2 * vector_size);
                        } else if (innermost_pure_var.extent >= vector_size) {
                            s.vectorize(innermost_pure_var.var, vector_size);
                        } else if (innermost_pure_var.extent >= 16) {
                            s.vectorize(innermost_pure_var.var, 16);
                        } else if (innermost_pure_var.extent >= 8) {
                            s.vectorize(innermost_pure_var.var, 8);
                        } else if (innermost_pure_var.extent >= 4) {
                            s.vectorize(innermost_pure_var.var, 4);
                        }
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
                            Var outer(parent.var.name() + "o"), inner(parent.var.name() + "i");
                            debug(0) << "Splitting " << parent.var.name() << " by " << factor << '\n';
                            if (parent.extent % factor == 0 && stage == 0) {
                                // TODO: If the actual size doesn't match the estimates, this could make some bad assumptions.
                                s.split(parent.var, outer, inner, (int)factor, TailStrategy::RoundUp);
                            } else {
                                s.split(parent.var, outer, inner, (int)factor, TailStrategy::GuardWithIf);
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

    static int cost_calculations;

    bool calculate_cost(const FunctionDAG &dag, const MachineParams &params) {
        map<Function, const PartialScheduleNode *, Function::Compare> compute_site;
        map<Function, double, Function::Compare> overcompute;
        cost = root.cost(dag, params, compute_site, overcompute, 1, nullptr, root);

        for (const auto &p : overcompute) {
            // Reject more than 50% overcompute out of hand for things
            // that are realized. Inlined Funcs get a pass
            // Compute_root is always legal.
            auto it = compute_site.find(p.first);
            if (it != compute_site.end() &&
                !it->second->is_root() &&
                p.second > 1.5) return false;
        }

        cost_calculations++;
        return true;
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

        int num_children = 0;

        // 1) Inline it
        if (!f.has_update_definition() && !dag.outgoing_edges.at(f).empty()) {
            auto child = new State(*this);
            child->root = child->root.inline_func(f, dag);
            child->num_funcs_scheduled++;
            if (child->calculate_cost(dag, params)) {
                internal_assert(child->root.computes(f)) << "Failed to inline " << f.name() << '\n';
                num_children++;
                accept_child(child);
            }
        }


        // 2) Realize it somewhere
        auto tile_options = root.compute_in_tiles(f, dag, nullptr, params, false);
        for (PartialScheduleNode n : tile_options) {
            auto child = new State(*this);
            child->root = std::move(n);
            child->num_funcs_scheduled++;
            if (child->calculate_cost(dag, params)) {
                internal_assert(child->root.computes(f)) << "Failed to inject realization of " << f.name() << '\n';
                num_children++;
                accept_child(child);
            }
        }

        internal_assert(num_children > 0) << "Could not find any legal way to schedule Func " << f.name() << '\n';
    }

    void dump() const {
        debug(0) << "State with cost " << cost << ":\n";
        root.dump("");
    }

    void apply_schedule(const FunctionDAG &dag, const MachineParams &params) {
        map<pair<Function, int>, PartialScheduleNode::FuncVars, PartialScheduleNode::FuncVarMapCompare> vars_map;
        root.apply(LoopLevel::root(), dag, vars_map, params.parallelism, nullptr);

        for (auto &p : vars_map) {
            Func f(p.first.first);
            int s = p.first.second;
            Stage stage(f);
            if (s > 0) {
                stage = f.update(s - 1);
            }

            // Do all the reorders
            vector<VarOrRVar> vars;
            for (auto &v : p.second.vars) {
                if (v.exists) vars.push_back(v.var);
            }
            stage.reorder(vars);

            // Figure out which dimensions are parallel and fuse them
            // into a single parallel outer loop (TODO: What if
            // something is compute_at in between two parallel vars?
            // Can't currently happen because we enforce adequately
            // large outer loops).
            double num_cores = p.second.num_cores;
            // VarOrRVar fused {Var()};
            bool any_parallel = false;
            for (int i = (int)p.second.vars.size() - 1; i >= 0 && num_cores > 1; i--) {
                auto &v = p.second.vars[i];
                if (!v.exists) continue;
                int64_t extent = v.extent;
                num_cores /= extent;
                if (num_cores < 0.125) {
                    // Should probably do another split and only mark the outer one as parallel
                    int task_size = std::floor(1 / num_cores);
                    debug(0) << "Task size for " << f.name() << ": " << task_size << '\n';
                    stage.parallel(v.var, task_size);
                } else {
                    stage.parallel(v.var);
                }
                if (!any_parallel) {
                    // fused = v.var;
                    any_parallel = true;
                } else if (i > 1) {
                    // Use the inner name, to not invalidate any compute_ats
                    // f.fuse(v.var, fused, v.var); // To consider: fuse may break loop partitioning. Check for likelies
                    // fused = v.var;
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
        root.cost(dag, params, compute_site, overcompute, 1, nullptr, root, &node_costs, &edge_costs, &inlined_costs);

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
                     << compute_cost << " + " << mem_cost << '\n';
        }
    }
};

int State::cost_calculations = 0;

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
        /*
        debug(0) << "\n** Generated child: ";
        s->dump();
        s->print_predicted_runtimes(dag, params);
        */

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
              debug(0) << "** Queue top: ";
              state->dump();
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

    debug(0) << "Cost evaluated this many times: " << State::cost_calculations << '\n';

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
        debug(0) << '\n';

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
        debug(0) << '\n';

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
        debug(0) << '\n';

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
        debug(0) << '\n';

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
        debug(0) << '\n';

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
        State optimal = optimal_schedule(dag, outputs, params, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << '\n';

        optimal.print_predicted_runtimes(dag, params);
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
        State optimal = optimal_schedule(dag, outputs, params, 4);

        dag.dump();

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << '\n';

        optimal.print_predicted_runtimes(dag, params);
    }

    {
        // A scan
        Buffer<float> a(1024, 1024);
        Func s("scan"), c("consumer");
        Var x, y;
        RDom r(1, 1023);
        s(x, y) = undef<float>();
        s(0, y) = a(0, y);
        s(r, y) += s(r-1, y);
        c(x, y) = s(x, y);

        c.estimate(x, 0, 1024).estimate(y, 0, 1024);

        vector<Function> outputs = {c.function()};
        FunctionDAG dag(outputs, params, target);
        dag.dump();
        State optimal = optimal_schedule(dag, outputs, params, 1);

        debug(0) << "** Optimal schedule:\n";
        optimal.dump();
        debug(0) << '\n';

        optimal.print_predicted_runtimes(dag, params);
    }

}

}
}
