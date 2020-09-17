#include "FunctionDAG.h"

#include "ASLog.h"

namespace Halide {
namespace Internal {

template<>
RefCount &ref_count<Autoscheduler::BoundContents>(const Autoscheduler::BoundContents *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<Autoscheduler::BoundContents>(const Autoscheduler::BoundContents *t) {
    // Release it back into the memory pool to be reused
    t->layout->release(t);
}

namespace Autoscheduler {

namespace {

class Featurizer : public IRVisitor {
    using IRVisitor::visit;

    Function &func;
    FunctionDAG::Node::Stage &stage;

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
        }  // TODO: separate out different math calls a little better (sqrt vs sin vs lerp)
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
                // The same name can be encountered multiple times
                // (e.g. a+a, where a is a trivial function),
                // so we can't use std::move(matrix) here without making a copy
                vector<vector<OptionalRational>> copy = matrix;
                e->add_load_jacobian(std::move(copy));
            }
        }
    }

public:
    Featurizer(Function &func, FunctionDAG::Node::Stage &stage)
        : func(func), stage(stage) {
    }

    void visit_store_args(const std::string &name, Type t, vector<Expr> args) {
        for (auto &e : args) {
            e = common_subexpression_elimination(simplify(e));  // Get things into canonical form
        }
        visit_memory_access(name, t, args, PipelineFeatures::AccessType::Store);
    }
};

}  // namespace

void LoadJacobian::dump(const char *prefix) const {
    if (count() > 1) {
        aslog(0) << prefix << count() << " x\n";
    }
    for (size_t i = 0; i < producer_storage_dims(); i++) {
        aslog(0) << prefix << "  [";

        for (size_t j = 0; j < consumer_loop_dims(); j++) {
            const auto &c = (*this)(i, j);
            if (!c.exists) {
                aslog(0) << " _  ";
            } else if (c.denominator == 1) {
                aslog(0) << " " << c.numerator << "  ";
            } else {
                aslog(0) << c.numerator << "/" << c.denominator << " ";
            }
        }
        aslog(0) << "]\n";
    }
    aslog(0) << "\n";
}

void BoundContents::validate() const {
    for (int i = 0; i < layout->total_size; i++) {
        auto p = data()[i];
        if (p.max() < p.min()) {
            aslog(0) << "Bad bounds object:\n";
            for (int j = 0; j < layout->total_size; j++) {
                if (i == j) {
                    aslog(0) << "=> ";
                } else {
                    aslog(0) << "   ";
                }
                aslog(0) << j << ": " << data()[j].min() << ", " << data()[j].max() << "\n";
            }
            internal_error << "Aborting";
        }
    }
}

BoundContents::Layout::~Layout() {
    internal_assert(num_live == 0)
        << "Destroying a Layout without returning all the BoundContents. "
        << num_live << " are still live\n";
    for (auto *b : pool) {
        b->~BoundContents();
    }
    for (auto b : blocks) {
        free(b);
    }
}

void BoundContents::Layout::allocate_some_more() const {
    size_t size_of_one = sizeof(BoundContents) + total_size * sizeof(Span);
    const size_t number_per_block = std::max((size_t)8, 4096 / size_of_one);  // Make a page of them, or 8, whichever is larger.
    const size_t bytes_to_allocate = std::max(size_of_one * number_per_block, (size_t)4096);
    unsigned char *mem = (unsigned char *)malloc(bytes_to_allocate);

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

BoundContents *BoundContents::Layout::make() const {
    if (pool.empty()) {
        allocate_some_more();
    }
    BoundContents *b = pool.back();
    pool.pop_back();
    num_live++;
    return b;
}

void BoundContents::Layout::release(const BoundContents *b) const {
    internal_assert(b->layout == this) << "Releasing BoundContents onto the wrong pool!";
    b->~BoundContents();
    pool.push_back(const_cast<BoundContents *>(b));
    num_live--;
}

void FunctionDAG::Node::loop_nest_for_region(int stage_idx, const Span *computed, Span *loop) const {
    const auto &s = stages[stage_idx];
    map<string, Expr> computed_map;
    if (!s.loop_nest_all_common_cases) {
        for (int i = 0; i < func.dimensions(); i++) {
            computed_map[region_required[i].min.name()] = (int)computed[i].min();
            computed_map[region_required[i].max.name()] = (int)computed[i].max();
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
            internal_assert(imin && imax) << min << ", " << max << "\n";
            loop[i] = Span(*imin, *imax, false);
        }
    }
}

void FunctionDAG::Node::required_to_computed(const Span *required, Span *computed) const {
    map<string, Expr> required_map;
    if (!region_computed_all_common_cases) {
        // Make a binding for the value of each symbolic variable
        for (int i = 0; i < func.dimensions(); i++) {
            required_map[region_required[i].min.name()] = (int)required[i].min();
            required_map[region_required[i].max.name()] = (int)required[i].max();
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
            internal_assert(imin && imax) << min << ", " << max << "\n";
            computed[i] = Span(*imin, *imax, false);
        }
    }
}

FunctionDAG::Edge::BoundInfo::BoundInfo(const Expr &e, const Node::Stage &consumer)
    : expr(e) {
    // Do the analysis to detect if this is a simple case
    // that can be evaluated more cheaply. Currently this
    // acceleration recognises affine expressions. In the
    // future we may consider quasi-affine, or even
    // piecewise-quasi-affine. If the bounds are
    // non-affine, we use the symbolic expression.
    const Add *add = expr.as<Add>();
    const Mul *mul = add ? add->a.as<Mul>() : expr.as<Mul>();
    const IntImm *coeff_imm = mul ? mul->b.as<IntImm>() : nullptr;
    const IntImm *constant_imm = add ? add->b.as<IntImm>() : nullptr;
    // clang-format off
    Expr v = (mul ? mul->a :
              add ? add->a :
                    expr);
    // clang-format on
    const Variable *var = v.as<Variable>();

    if (const IntImm *c = e.as<IntImm>()) {
        affine = true;
        coeff = 0;
        constant = c->value;
    } else if (var && (!mul || coeff_imm) && (!add || constant_imm)) {
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
        aslog(2) << "Bound is affine: " << e << " == " << var->name << " * " << coeff << " + " << constant << "\n";
    } else {
        affine = false;
        aslog(2) << "Bound is non-affine: " << e << "\n";
    }
}

void FunctionDAG::Edge::add_load_jacobian(LoadJacobian j1) {
    for (auto &j2 : load_jacobians) {
        if (j2.merge(j1)) return;
    }
    load_jacobians.emplace_back(std::move(j1));
}

void FunctionDAG::Edge::expand_footprint(const Span *consumer_loop, Span *producer_required) const {
    // Create a map from the symbolic loop variables to the actual loop size
    const auto &symbolic_loop = consumer->loop;
    map<string, Expr> s;
    if (!all_bounds_affine) {
        for (size_t i = 0; i < symbolic_loop.size(); i++) {
            auto p = consumer_loop[i];
            const string &var = symbolic_loop[i].var;
            s[consumer->node->func.name() + "." + var + ".min"] = (int)p.min();
            s[consumer->node->func.name() + "." + var + ".max"] = (int)p.max();
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
                if (b.coeff == 0) {
                    return b.constant;
                } else {
                    const auto &src_pair = consumer_loop[b.consumer_dim];
                    int64_t src = b.uses_max ? src_pair.max() : src_pair.min();
                    bounds_are_constant &= src_pair.constant_extent();
                    return src * b.coeff + b.constant;
                }
            } else {
                Expr substituted = substitute(s, b.expr);
                Expr e = simplify(substituted);
                const int64_t *i = as_const_int(e);
                internal_assert(i) << "Should be constant: " << b.expr << " -> " << substituted << " -> " << e << "\n";
                bounds_are_constant = false;
                return *i;
            }
        };
        int64_t a = eval_bound(bounds[i].first);
        int64_t b = eval_bound(bounds[i].second);
        producer_required[i].union_with(Span(a, b, bounds_are_constant));
    }
}

FunctionDAG::FunctionDAG(const vector<Function> &outputs, const MachineParams &params, const Target &target) {
    map<string, Function> env;
    for (Function o : outputs) {
        populate_environment(o, env);
    }

    // A mutator to apply parameter estimates to the expressions
    // we encounter while constructing the graph.
    class ApplyParamEstimates : public IRMutator {
        using IRMutator::visit;

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
                internal_assert(expr.defined()) << "Missing estimate for " << op->name << "\n";
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
    map<Function, Node *, Function::Compare> node_map;
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
        Node &node = nodes[order.size() - i];
        Function consumer = node.func;
        Scope<Interval> scope;

        // Create a symbolic region for this Func.
        for (int j = 0; j < consumer.dimensions(); j++) {
            Halide::Var min_var(consumer.name() + "." + consumer.args()[j] + ".min");
            Halide::Var max_var(consumer.name() + "." + consumer.args()[j] + ".max");
            Interval interval(min_var, max_var);
            scope.push(consumer.args()[j], interval);
            node.region_required.emplace_back(SymbolicInterval{min_var, max_var});
        }

        auto pure_args = node.func.args();

        for (int s = 0; s <= (int)consumer.updates().size(); s++) {
            stage_count++;
            if (s == 0) {
                node.stages.emplace_back(Stage(consumer, consumer.definition(), 0));
            } else {
                node.stages.emplace_back(Stage(consumer, consumer.update(s - 1), s));
            }
        }

        for (int s = 0; s <= (int)consumer.updates().size(); s++) {
            auto &stage = node.stages[s];
            stage.node = &node;
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

            FuncValueBounds func_value_bounds = compute_function_value_bounds(order, env);
            for (int j = 0; j < consumer.dimensions(); j++) {
                // The region computed always uses the full extent of the rvars
                Interval in = bounds_of_expr_in_scope(def.args()[j], stage_scope_with_concrete_rvar_bounds, func_value_bounds);
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
            user_assert(sched.splits().empty())
                << "The Func \"" << consumer.name() << "\" has scheduling directive(s) "
                << "applied to it; you must remove these, or conditionalize them "
                << "using `if (!auto_schedule)`, to use the autoscheduler on this pipeline.";
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
                CheckTypes(Function f)
                    : func(f) {
                }
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
                map<string, Span> estimates;
                for (auto b : consumer.schedule().estimates()) {
                    int64_t i_min = *as_const_int(b.min);
                    int64_t i_extent = *as_const_int(b.extent);

                    if ((false)) {  // Intentional dead code. Extra parens to pacify clang-tidy.
                        // Some methods we compare to compile for
                        // statically known input/output sizes. We
                        // don't need to - we take estimates but
                        // the compiled code doesn't enforce
                        // them. If you want to make a comparison
                        // fair and target a fixed size, use this
                        // branch of the if. In practice we don't
                        // see a runtime difference, so we left it
                        // disabled. In theory, Sizes being
                        // constant makes it possible to do things
                        // like unroll across color channels, so
                        // it affects the scheduling space.
                        Func(node.func).bound(b.var, b.min, b.extent);
                        estimates[b.var] = Span(i_min, i_min + i_extent - 1, true);
                    } else {
                        estimates[b.var] = Span(i_min, i_min + i_extent - 1, false);
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
        auto &n = nodes[i - 1];
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

    // Compute the algorithm-specific features for the neural net
    featurize();
}

void FunctionDAG::featurize() {
    for (Node &node : nodes) {
        for (size_t stage_idx = 0; stage_idx < node.stages.size(); stage_idx++) {
            Node::Stage &stage = node.stages[stage_idx];

            Featurizer featurizer(node.func, stage);

            if (node.func.extern_definition_proxy_expr().get()) {
                // Extern function call with a proxy implementation specified: generate the featurization from the proxy
                Expr v = simplify(node.func.extern_definition_proxy_expr());
                v = common_subexpression_elimination(v);
                v.accept(&featurizer);
            } else {
                Definition def = node.func.definition();
                if (stage_idx > 0) {
                    def = node.func.updates()[stage_idx - 1];
                }
                stage.features = PipelineFeatures();

                for (auto v : def.values()) {
                    featurizer.visit_store_args(node.func.name(), v.type(), def.args());
                    v = common_subexpression_elimination(simplify(v));  // Get things into canonical form
                    v.accept(&featurizer);
                }
                for (auto v : def.args()) {
                    v = common_subexpression_elimination(simplify(v));  // Get things into canonical form
                    v.accept(&featurizer);
                }
            }
        }
    }
}

template<typename OS>
void FunctionDAG::dump_internal(OS &os) const {
    for (const Node &n : nodes) {
        os << "Node: " << n.func.name() << "\n"
           << "  Symbolic region required: \n";
        for (const SymbolicInterval &i : n.region_required) {
            os << "    " << i.min << ", " << i.max << "\n";
        }
        os << "  Region computed: \n";
        for (const auto &i : n.region_computed) {
            os << "    " << i.in.min << ", " << i.in.max << "\n";
        }
        for (size_t i = 0; i < n.stages.size(); i++) {
            os << "  Stage " << i << ":\n";
            for (const auto &l : n.stages[i].loop) {
                os << "    " << l.var << " " << l.min << " " << l.max << "\n";
            }
            n.stages[i].features.dump(os);
        }
        os << "  pointwise: " << n.is_pointwise
           << " boundary condition: " << n.is_boundary_condition
           << " wrapper: " << n.is_wrapper
           << " input: " << n.is_input
           << " output: " << n.is_output << "\n";
    }
    for (const Edge &e : edges) {
        os << "Edge: " << e.producer->func.name() << " -> " << e.consumer->name << "\n"
           << "  Footprint: \n";
        int j = 0;
        for (const auto &i : e.bounds) {
            os << "    Min " << j << ": " << i.first.expr << "\n";
            os << "    Max " << j << ": " << i.second.expr << "\n";
            j++;
        }

        os << "  Load Jacobians:\n";
        for (const auto &jac : e.load_jacobians) {
            jac.dump("  ");
        }
    }
}

void FunctionDAG::dump() const {
    auto os = aslog(0);
    dump_internal(os);
}

std::ostream &FunctionDAG::dump(std::ostream &os) const {
    dump_internal(os);
    return os;
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
