#include "RotateRegisters.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

#include <optional>

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

int64_t euclidean_mod(int64_t a, int64_t b) {
    int64_t r = a % b;
    return r < 0 ? r + b : r;
}

bool uses_other_vars(const Expr &e, const Scope<> &allowed) {
    bool result = false;
    visit_with(e, [&](auto *, const Variable *op) {
        result |= !allowed.contains(op->name);
    });
    return result;
}

bool has_loads(const Expr &e) {
    bool result = false;
    visit_with(
        e,
        [&](auto *, const Load *) { result = true; },
        [&](auto *self, const Call *op) {
            result |= (op->call_type == Call::Halide || op->call_type == Call::Image ||
                       op->call_type == Call::Extern || op->call_type == Call::ExternCPlusPlus);
            self->visit_base(op);
        });
    return result;
}

// Work out how a dimension of a realization in registers would have to rotate.
// A loop advances a coordinate by k each iteration if every access inside it
// does. The rotation at any point is then the sum over the enclosing loops of
// k times the iteration count, plus an offset common to the whole
// realization, and an access at coordinate c uses index (c - rotation) mod the
// extent. Bailing out leaves the dimension indexed modulo the extent, which is
// equally correct.
class PlanRotation : public IRVisitor {
    const string &name;
    int dim;
    int64_t extent;

    struct Loop {
        const For *op;
        // A variable standing for the loop var minus the loop min.
        string iteration;
        // The iterations of unrolled and vectorized loops become constants.
        bool becomes_constant;
    };
    vector<Loop> loops;

    // Lets and loop vars, in terms of the iteration variables and names
    // defined outside the realization.
    map<string, Expr> values;

    template<typename Fn>
    void with_value(const string &n, const Expr &value, Fn &&fn) {
        auto it = values.find(n);
        std::optional<Expr> old;
        if (it != values.end()) {
            old = it->second;
        }
        values[n] = value;
        fn();
        if (old) {
            values[n] = *old;
        } else {
            values.erase(n);
        }
    }

    // The part of the rotation that no loop inside the realization changes.
    Expr offset;

    int accesses = 0;
    map<const For *, int> accesses_inside;
    map<const For *, bool> enclosed;

    using IRVisitor::visit;

    Expr expand(const Expr &e) {
        return simplify(remove_likelies(remove_promises(substitute(values, substitute_in_all_lets(e)))));
    }

    // How much an expression changes per iteration of a loop.
    std::optional<int64_t> step(const Expr &e, const Loop &l) {
        Expr it = Variable::make(Int(32), l.iteration);
        return as_const_int(simplify(substitute(l.iteration, it + 1, e) - e));
    }

    bool steps_by_constants(const Expr &e) {
        for (const Loop &l : loops) {
            if (!l.becomes_constant && !step(e, l)) {
                return false;
            }
        }
        return true;
    }

    void access(const IRNode *node, const vector<Expr> &args) {
        accesses++;
        if (failed) {
            return;
        }
        internal_assert(dim < (int)args.size());
        Expr c = args[dim];
        if (const Mod *m = c.as<Mod>()) {
            if (is_const(m->b, extent)) {
                // The coordinate is in range, so it's already equal to itself
                // modulo the extent. This is what storage folding left behind.
                folded = true;
                c = m->a;
            }
        }
        Expr e = expand(c);
        if (has_loads(e)) {
            failed = true;
            return;
        }

        Expr residual = e;
        map<string, Expr> zero;
        Scope<> constant_vars, all_iterations;
        for (const Loop &l : loops) {
            Expr it = Variable::make(Int(32), l.iteration);
            zero[l.iteration] = 0;
            all_iterations.push(l.iteration);
            if (l.becomes_constant) {
                constant_vars.push(l.iteration);
                continue;
            }
            auto kc = step(e, l);
            if (!kc) {
                failed = true;
                return;
            }
            auto [step, inserted] = steps.emplace(l.op, *kc);
            if (!inserted && euclidean_mod(step->second - *kc, extent) != 0) {
                failed = true;
                return;
            }
            residual -= make_const(Int(32), *kc) * it;
        }
        residual = simplify(residual);
        Expr o = simplify(substitute(zero, residual));
        Expr rest = simplify(residual - o);
        if (expr_uses_vars(o, all_iterations) || uses_other_vars(rest, constant_vars)) {
            failed = true;
            return;
        }
        if (!offset.defined()) {
            offset = o;
        }
        auto delta = as_const_int(simplify(o - offset));
        if (!delta) {
            failed = true;
            return;
        }

        map<string, Expr> in_loop_vars;
        for (const Loop &l : loops) {
            if (l.becomes_constant) {
                in_loop_vars[l.iteration] = Variable::make(Int(32), l.op->name) - l.op->min;
            }
        }
        Expr index = simplify(substitute(in_loop_vars, rest) + make_const(Int(32), *delta));
        index = simplify(Mod::make(index, make_const(Int(32), extent)));
        auto [it, inserted] = indices.emplace(node, index);
        if (!inserted && !equal(it->second, index)) {
            failed = true;
        }
    }

    void visit(const Provide *op) override {
        IRVisitor::visit(op);
        if (op->name == name) {
            access(op, op->args);
        }
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->call_type == Call::Halide && op->name == name) {
            func = op->func;
            access(op, op->args);
        }
    }

    void visit(const Variable *op) override {
        // Something like an extern stage sees the whole buffer.
        failed |= op->name == name + ".buffer";
    }

    void visit(const For *op) override {
        op->min.accept(this);
        op->max.accept(this);
        string iteration = unique_name(op->name + ".iteration");
        bool becomes_constant = (op->for_type == ForType::Unrolled ||
                                 op->for_type == ForType::Vectorized);
        enclosed[op] = !loops.empty();
        Expr min = expand(op->min), max = expand(op->max);
        int before = accesses;
        if (can_prove(max <= min)) {
            // A loop that runs at most once has no back edge, and its var is
            // both its min and its max whenever it runs.
            Expr var = steps_by_constants(min) ? min : max;
            with_value(op->name, var, [&]() { op->body.accept(this); });
        } else {
            Expr var = min + Variable::make(Int(32), iteration);
            loops.push_back({op, iteration, becomes_constant});
            with_value(op->name, var, [&]() { op->body.accept(this); });
            loops.pop_back();
        }
        accesses_inside[op] = accesses - before;
    }

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op) {
        op->value.accept(this);
        with_value(op->name, expand(op->value), [&]() { op->body.accept(this); });
    }

    void visit(const Let *op) override {
        visit_let(op);
    }

    void visit(const LetStmt *op) override {
        visit_let(op);
    }

public:
    PlanRotation(const string &name, int dim, int64_t extent)
        : name(name), dim(dim), extent(extent) {
    }

    int dimension() const {
        return dim;
    }

    int64_t size() const {
        return extent;
    }

    bool failed = false;
    bool folded = false;
    FunctionPtr func;

    // How far each loop moves the coordinate per iteration.
    map<const For *, int64_t> steps;

    // The new index for each access.
    map<const IRNode *, Expr> indices;

    bool worth_doing() const {
        if (failed || !folded || !func.defined()) {
            return false;
        }
        bool any = false;
        for (const auto &[loop, k] : steps) {
            if (euclidean_mod(k, extent) != 0) {
                if (loop->for_type != ForType::Serial) {
                    // Can't rotate between the iterations of a parallel loop.
                    return false;
                }
                any = true;
            }
        }
        return any;
    }

    // Whether anything outside the loop might see the rotation it leaves
    // behind on exit.
    bool needs_restoring(const For *loop) const {
        return enclosed.at(loop) || accesses_inside.at(loop) != accesses;
    }
};

class ApplyRotation : public IRMutator {
    const Realize *realize;
    const vector<PlanRotation> &plans;

    using IRMutator::visit;

    // Move every value in a dimension down by the given number of slots,
    // wrapping around.
    Stmt shift(const PlanRotation &plan, int64_t amount) {
        int dim = plan.dimension();
        int64_t extent = plan.size();
        vector<Expr> args(realize->bounds.size());
        vector<std::pair<string, Range>> loops;
        for (size_t i = 0; i < realize->bounds.size(); i++) {
            if ((int)i == dim) {
                continue;
            }
            const Range &r = realize->bounds[i];
            if (is_const_one(r.extent)) {
                args[i] = r.min;
            } else {
                string n = unique_name(realize->name + ".rotate." + std::to_string(i));
                args[i] = Variable::make(Int(32), n);
                loops.emplace_back(n, r);
            }
        }

        const vector<Type> &types = realize->types;
        vector<vector<string>> slot_names(extent);
        vector<std::pair<string, Expr>> lets;
        for (int64_t q = 0; q < extent; q++) {
            args[dim] = make_const(Int(32), q);
            for (size_t v = 0; v < types.size(); v++) {
                string n = unique_name(realize->name + ".slot");
                slot_names[q].push_back(n);
                lets.emplace_back(n, Call::make(types[v], realize->name, args, Call::Halide,
                                                plan.func, (int)v));
            }
        }
        vector<Stmt> stores;
        for (int64_t p = 0; p < extent; p++) {
            args[dim] = make_const(Int(32), p);
            int64_t q = (p + amount) % extent;
            vector<Expr> values;
            for (size_t v = 0; v < types.size(); v++) {
                values.push_back(Variable::make(types[v], slot_names[q][v]));
            }
            stores.push_back(Provide::make(realize->name, values, args, const_true()));
        }
        Stmt s = Block::make(stores);
        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            s = LetStmt::make(it->first, it->second, s);
        }
        for (size_t i = 0; i < loops.size(); i++) {
            const auto &[n, r] = loops[i];
            // Move whole vectors along the innermost dimension.
            ForType t = i == 0 ? ForType::Vectorized : ForType::Unrolled;
            s = For::make(n, r.min, simplify(r.min + r.extent - 1), t,
                          Partition::Never, DeviceAPI::None, s);
        }
        return s;
    }

    Stmt rotate(const PlanRotation &plan, const Expr &amount) {
        int64_t extent = plan.size();
        Expr a = simplify(Mod::make(amount, make_const(Int(32), extent)));
        if (auto c = as_const_int(a)) {
            return *c == 0 ? Stmt() : shift(plan, *c);
        }
        // Rotate by each bit of the amount in turn, so that the indices stay
        // constant.
        string n = unique_name(realize->name + ".rotation");
        Expr var = Variable::make(Int(32), n);
        vector<Stmt> steps;
        for (int64_t b = 1; b < extent; b *= 2) {
            Expr bit_set = (var / make_const(Int(32), b)) % 2 == 1;
            steps.push_back(IfThenElse::make(bit_set, shift(plan, b)));
        }
        return LetStmt::make(n, a, Block::make(steps));
    }

    template<typename T>
    vector<Expr> new_args(const T *original, vector<Expr> args) {
        for (const PlanRotation &plan : plans) {
            auto it = plan.indices.find(original);
            if (it != plan.indices.end()) {
                args[plan.dimension()] = it->second;
            }
        }
        return args;
    }

    Expr visit(const Call *op) override {
        Expr e = IRMutator::visit(op);
        if (op->call_type == Call::Halide && op->name == realize->name) {
            const Call *c = e.as<Call>();
            internal_assert(c);
            e = c->with(new_args(op, c->args));
        }
        return e;
    }

    Stmt visit(const Provide *op) override {
        Stmt s = IRMutator::visit(op);
        if (op->name == realize->name) {
            const Provide *p = s.as<Provide>();
            internal_assert(p);
            s = p->with(p->values, new_args(op, p->args), p->predicate);
        }
        return s;
    }

    Stmt visit(const For *op) override {
        Stmt body = mutate(op->body);
        vector<Stmt> restores;
        for (const PlanRotation &plan : plans) {
            auto it = plan.steps.find(op);
            int64_t k = it == plan.steps.end() ? 0 : euclidean_mod(it->second, plan.size());
            if (k == 0) {
                continue;
            }
            body = Block::make(body, shift(plan, k));
            if (plan.needs_restoring(op)) {
                Stmt restore = rotate(plan, -make_const(Int(32), k) * op->extent());
                if (restore.defined()) {
                    restores.push_back(restore);
                }
            }
        }
        Stmt s = op->with(op->min, op->max, body);
        if (!restores.empty()) {
            s = Block::make(s, Block::make(restores));
        }
        return s;
    }

public:
    ApplyRotation(const Realize *realize, const vector<PlanRotation> &plans)
        : realize(realize), plans(plans) {
    }
};

class RotateRegisters : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Realize *op) override {
        Stmt body = mutate(op->body);
        if (op->memory_type == MemoryType::Register) {
            bool other_extents_constant = true;
            for (const Range &r : op->bounds) {
                other_extents_constant &= is_const(r.extent);
            }
            vector<PlanRotation> plans;
            for (size_t d = 0; other_extents_constant && d < op->bounds.size(); d++) {
                auto extent = as_const_int(op->bounds[d].extent);
                if (!is_const_zero(op->bounds[d].min) || *extent < 2) {
                    continue;
                }
                PlanRotation plan(op->name, (int)d, *extent);
                body.accept(&plan);
                if (plan.worth_doing()) {
                    debug(3) << "Rotating dimension " << d << " of " << op->name << "\n";
                    plans.push_back(std::move(plan));
                }
            }
            if (!plans.empty()) {
                body = ApplyRotation(op, plans)(body);
            }
        }
        if (body.same_as(op->body)) {
            return op;
        }
        return op->with(op->bounds, op->condition, body);
    }
};

}  // namespace

Stmt rotate_registers(const Stmt &s) {
    return RotateRegisters()(s);
}

}  // namespace Internal
}  // namespace Halide
