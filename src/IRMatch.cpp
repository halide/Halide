#include <iostream>
#include <map>
#include <utility>

#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

void expr_match_test() {
    vector<Expr> matches;
    Expr w = Variable::make(Int(32), "*");
    Expr fw = Variable::make(Float(32), "*");
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Expr fx = Variable::make(Float(32), "fx");
    Expr fy = Variable::make(Float(32), "fy");

    Expr vec_wild = Variable::make(Int(32, 4), "*");

    internal_assert(expr_match(w, 3, matches) &&
                    equal(matches[0], 3));

    internal_assert(expr_match(w + 3, (y * 2) + 3, matches) &&
                    equal(matches[0], y * 2));

    internal_assert(expr_match(fw * 17 + cast<float>(w + cast<int>(fw)),
                               (81.0f * fy) * 17 + cast<float>(x / 2 + cast<int>(x + 4.5f)), matches) &&
                    matches.size() == 3 &&
                    equal(matches[0], 81.0f * fy) &&
                    equal(matches[1], x / 2) &&
                    equal(matches[2], x + 4.5f));

    internal_assert(!expr_match(fw + 17, fx + 18, matches) &&
                    matches.empty());
    internal_assert(!expr_match((w * 2) + 17, fx + 17, matches) &&
                    matches.empty());
    internal_assert(!expr_match(w * 3, 3 * x, matches) &&
                    matches.empty());

    internal_assert(expr_match(vec_wild * 3, Ramp::make(x, y, 4) * 3, matches));

    std::cout << "expr_match test passed\n";
}

namespace {

class IRMatch : public IRVisitor {
public:
    bool result;
    vector<Expr> *matches;
    map<string, Expr> *var_matches;
    Expr expr;

    IRMatch(Expr e, vector<Expr> &m)
        : result(true), matches(&m), var_matches(nullptr), expr(std::move(e)) {
    }
    IRMatch(Expr e, map<string, Expr> &m)
        : result(true), matches(nullptr), var_matches(&m), expr(std::move(e)) {
    }

    using IRVisitor::visit;

    bool types_match(Type pattern_type, Type expr_type) {
        bool bits_matches = (pattern_type.bits() == 0) || (pattern_type.bits() == expr_type.bits());
        bool lanes_matches = (pattern_type.lanes() == 0) || (pattern_type.lanes() == expr_type.lanes());
        bool code_matches = (pattern_type.code() == expr_type.code());
        return bits_matches && lanes_matches && code_matches;
    }

    void visit(const IntImm *op) override {
        const IntImm *e = expr.as<IntImm>();
        if (!e ||
            e->value != op->value ||
            !types_match(op->type, e->type)) {
            result = false;
        }
    }

    void visit(const UIntImm *op) override {
        const UIntImm *e = expr.as<UIntImm>();
        if (!e ||
            e->value != op->value ||
            !types_match(op->type, e->type)) {
            result = false;
        }
    }

    void visit(const FloatImm *op) override {
        const FloatImm *e = expr.as<FloatImm>();
        // Note we use uint64_t equality instead of double equality to
        // catch NaNs. We're checking for the same bits.
        if (!e ||
            reinterpret_bits<uint64_t>(e->value) !=
                reinterpret_bits<uint64_t>(op->value) ||
            !types_match(op->type, e->type)) {
            result = false;
        }
    }

    void visit(const Cast *op) override {
        const Cast *e = expr.as<Cast>();
        if (result && e && types_match(op->type, e->type)) {
            expr = e->value;
            op->value.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Reinterpret *op) override {
        const Reinterpret *e = expr.as<Reinterpret>();
        if (result && e && types_match(op->type, e->type)) {
            expr = e->value;
            op->value.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Variable *op) override {
        if (!result) {
            return;
        }

        if (!types_match(op->type, expr.type())) {
            result = false;
        } else if (matches) {
            if (op->name == "*") {
                matches->push_back(expr);
            } else {
                const Variable *e = expr.as<Variable>();
                result = e && (e->name == op->name);
            }
        } else if (var_matches) {
            Expr &match = (*var_matches)[op->name];
            if (match.defined()) {
                result = equal(match, expr);
            } else {
                match = expr;
            }
        }
    }

    template<typename T>
    void visit_binary_operator(const T *op) {
        const T *e = expr.as<T>();
        if (result && e) {
            expr = e->a;
            op->a.accept(this);
            expr = e->b;
            op->b.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Add *op) override {
        visit_binary_operator(op);
    }
    void visit(const Sub *op) override {
        visit_binary_operator(op);
    }
    void visit(const Mul *op) override {
        visit_binary_operator(op);
    }
    void visit(const Div *op) override {
        visit_binary_operator(op);
    }
    void visit(const Mod *op) override {
        visit_binary_operator(op);
    }
    void visit(const Min *op) override {
        visit_binary_operator(op);
    }
    void visit(const Max *op) override {
        visit_binary_operator(op);
    }
    void visit(const EQ *op) override {
        visit_binary_operator(op);
    }
    void visit(const NE *op) override {
        visit_binary_operator(op);
    }
    void visit(const LT *op) override {
        visit_binary_operator(op);
    }
    void visit(const LE *op) override {
        visit_binary_operator(op);
    }
    void visit(const GT *op) override {
        visit_binary_operator(op);
    }
    void visit(const GE *op) override {
        visit_binary_operator(op);
    }
    void visit(const And *op) override {
        visit_binary_operator(op);
    }
    void visit(const Or *op) override {
        visit_binary_operator(op);
    }

    void visit(const Not *op) override {
        const Not *e = expr.as<Not>();
        if (result && e) {
            expr = e->a;
            op->a.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Select *op) override {
        const Select *e = expr.as<Select>();
        if (result && e) {
            expr = e->condition;
            op->condition.accept(this);
            expr = e->true_value;
            op->true_value.accept(this);
            expr = e->false_value;
            op->false_value.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Load *op) override {
        const Load *e = expr.as<Load>();
        if (result && e && types_match(op->type, e->type) && e->name == op->name && e->alignment == op->alignment) {
            expr = e->predicate;
            op->predicate.accept(this);
            expr = e->index;
            op->index.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Ramp *op) override {
        const Ramp *e = expr.as<Ramp>();
        if (result && e && e->lanes == op->lanes) {
            expr = e->base;
            op->base.accept(this);
            expr = e->stride;
            op->stride.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Broadcast *op) override {
        const Broadcast *e = expr.as<Broadcast>();
        if (result && e && types_match(op->type, e->type)) {
            expr = e->value;
            op->value.accept(this);
        } else if (op->lanes == 0 && types_match(op->value.type(), expr.type())) {
            // zero lanes means any number of lanes, so match scalars too.
            op->value.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Call *op) override {
        const Call *e = expr.as<Call>();
        if (result && e &&
            types_match(op->type, e->type) &&
            e->name == op->name &&
            e->value_index == op->value_index &&
            e->call_type == op->call_type &&
            e->args.size() == op->args.size()) {
            for (size_t i = 0; result && (i < e->args.size()); i++) {
                expr = e->args[i];
                op->args[i].accept(this);
            }
        } else {
            result = false;
        }
    }

    void visit(const Let *op) override {
        const Let *e = expr.as<Let>();
        if (result && e && e->name == op->name) {
            expr = e->value;
            op->value.accept(this);
            expr = e->body;
            op->body.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const VectorReduce *op) override {
        const VectorReduce *e = expr.as<VectorReduce>();
        if (result && e && op->op == e->op && types_match(op->type, e->type)) {
            expr = e->value;
            op->value.accept(this);
        } else {
            result = false;
        }
    }
};

}  // namespace

bool expr_match(const Expr &pattern, const Expr &expr, vector<Expr> &matches) {
    matches.clear();
    if (!pattern.defined() && !expr.defined()) {
        return true;
    }
    if (!pattern.defined() || !expr.defined()) {
        return false;
    }

    IRMatch eq(expr, matches);
    pattern.accept(&eq);
    if (eq.result) {
        return true;
    } else {
        matches.clear();
        return false;
    }
}

bool expr_match(const Expr &pattern, const Expr &expr, map<string, Expr> &matches) {
    // Explicitly don't clear matches. This allows usages to pre-match
    // some variables.

    if (!pattern.defined() && !expr.defined()) {
        return true;
    }
    if (!pattern.defined() || !expr.defined()) {
        return false;
    }

    IRMatch eq(expr, matches);
    pattern.accept(&eq);
    if (eq.result) {
        return true;
    } else {
        matches.clear();
        return false;
    }
}

namespace {

class WithLanes : public IRMutator {
    using IRMutator::visit;

    int lanes;

    Type with_lanes(Type t) const {
        return t.with_lanes(lanes);
    }

    Expr visit(const Cast *op) override {
        if (op->type.lanes() != lanes) {
            return Cast::make(with_lanes(op->type), mutate(op->value));
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Variable *op) override {
        if (op->type.lanes() != lanes) {
            return Variable::make(with_lanes(op->type), op->name);
        } else {
            return op;
        }
    }

    Expr visit(const Broadcast *op) override {
        if (op->type.lanes() != lanes) {
            return Broadcast::make(op->value, lanes);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic() && (op->type.lanes() != lanes)) {
            auto new_args = mutate_with_changes(op->args).first;
            return Call::make(with_lanes(op->type), op->name, new_args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    WithLanes(int lanes)
        : lanes(lanes) {
    }
};

}  // namespace

Expr with_lanes(const Expr &x, int lanes) {
    return WithLanes(lanes).mutate(x);
}

}  // namespace Internal
}  // namespace Halide
