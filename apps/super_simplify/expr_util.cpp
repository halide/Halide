#include "expr_util.h"

#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::string;
using std::vector;

class FindVars : public IRVisitor {
    Scope<> lets;

    void visit(const Variable *op) override {
        if (!lets.contains(op->name)) {
            auto &v = vars[op->name];
            v.second++;
            v.first = op;
        }
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        {
            ScopedBinding<> bind(lets, op->name);
            op->body.accept(this);
        }
    }

public:
    std::map<std::string, std::pair<Expr, int>> vars;
};

std::map<std::string, std::pair<Expr, int>> find_vars(const Expr &e) {
    FindVars f;
    e.accept(&f);
    return f.vars;
}

template<typename Op>
bool more_general_than(const Expr &a, const Op *b, map<string, Expr> &bindings, bool entered_a) {
    if (!entered_a) {
        map<string, Expr> backup = bindings;
        if (more_general_than(a, b->a, bindings)) {
            return true;
        }
        bindings = backup;

        if (more_general_than(a, b->b, bindings)) {
            return true;
        }
        bindings = backup;
    }

    if (const Op *op_a = a.as<Op>()) {
        return (more_general_than(op_a->a, b->a, bindings, true) &&
                more_general_than(op_a->b, b->b, bindings, true));
    }
    return false;
}

bool more_general_than(const Expr &a, const Expr &b, map<string, Expr> &bindings, bool entered_a) {
    if (const Variable *var = a.as<Variable>()) {
        const Variable *var_b = b.as<Variable>();
        auto it = bindings.find(var->name);
        if (it != bindings.end()) {
            return equal(it->second, b);
        } else {
            bool const_wild = var->name[0] == 'c';
            bool b_const_wild = var_b && (var_b->name[0] == 'c');
            bool b_const = is_const(b);
            bool may_bind = !const_wild || (const_wild && (b_const_wild || b_const));
            if (may_bind) {
                bindings[var->name] = b;
                return true;
            } else {
                return false;
            }
        }
    }

    if (is_const(a) && is_const(b)) {
        return equal(a, b);
    }

    if (const And *op = b.as<And>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Or *op = b.as<Or>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Min *op = b.as<Min>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Max *op = b.as<Max>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Add *op = b.as<Add>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Sub *op = b.as<Sub>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Mul *op = b.as<Mul>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Div *op = b.as<Div>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Mod *op = b.as<Mod>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const LE *op = b.as<LE>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const LT *op = b.as<LT>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const EQ *op = b.as<EQ>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const NE *op = b.as<NE>()) {
        return more_general_than(a, op, bindings, entered_a);
    }

    if (const Not *op = b.as<Not>()) {
        if (!entered_a) {
            map<string, Expr> backup = bindings;
            if (more_general_than(a, op->a, bindings, entered_a)) {
                return true;
            }
            bindings = backup;
        }

        const Not *op_a = a.as<Not>();
        return (op_a &&
                more_general_than(op_a->a, op->a, bindings, true));
    }

    if (const Select *op = b.as<Select>()) {
        if (!entered_a) {
            map<string, Expr> backup = bindings;
            if (more_general_than(a, op->condition, bindings, entered_a)) {
                return true;
            }
            bindings = backup;

            if (more_general_than(a, op->true_value, bindings, entered_a)) {
                return true;
            }
            bindings = backup;

            if (more_general_than(a, op->false_value, bindings, entered_a)) {
                return true;
            }
            bindings = backup;
        }

        const Select *op_a = a.as<Select>();
        return (op_a &&
                more_general_than(op_a->condition, op->condition, bindings, true) &&
                more_general_than(op_a->true_value, op->true_value, bindings, true) &&
                more_general_than(op_a->false_value, op->false_value, bindings, true));
    }

    return false;
}

class FindCommutativeOps : public IRVisitor {
    template<typename Op>
    void visit_commutative_op(const Op *op) {
        const Variable *var_a = op->a.template as<Variable>();
        const Variable *var_b = op->b.template as<Variable>();
        const Call *call_b = op->b.template as<Call>();
        if ((var_b && var_b->name[0] == 'c') ||
            is_const(op->b) ||
            (call_b && call_b->name == "fold")) {
            op->a.accept(this);
            return;
        }
        if (var_a || var_b) {
            commutative_ops.push_back(Expr(op));
        }
        IRVisitor::visit(op);
    }

    void visit(const Add *op) override {
        visit_commutative_op(op);
    }
    void visit(const Mul *op) override {
        visit_commutative_op(op);
    }
    void visit(const Min *op) override {
        visit_commutative_op(op);
    }
    void visit(const Max *op) override {
        visit_commutative_op(op);
    }
    void visit(const EQ *op) override {
        visit_commutative_op(op);
    }
    void visit(const NE *op) override {
        visit_commutative_op(op);
    }
    void visit(const And *op) override {
        visit_commutative_op(op);
    }
    void visit(const Or *op) override {
        visit_commutative_op(op);
    }

public:
    vector<Expr> commutative_ops;
};

class Commute : public IRMutator {
    template<typename Op>
    Expr visit_commutative_op(const Op *op) {
        if (to_commute.same_as(op)) {
            return Op::make(op->b, op->a);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Add *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const Mul *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const Min *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const Max *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const EQ *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const NE *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const And *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const Or *op) override {
        return visit_commutative_op(op);
    }

    Expr to_commute;

public:
    Commute(Expr c)
        : to_commute(c) {
    }
};

vector<Expr> generate_commuted_variants(const Expr &expr) {
    FindCommutativeOps finder;
    expr.accept(&finder);

    vector<Expr> exprs;
    exprs.push_back(expr);

    for (const Expr &e : finder.commutative_ops) {
        Commute commuter(e);
        vector<Expr> new_exprs = exprs;
        for (const Expr &l : exprs) {
            new_exprs.push_back(commuter.mutate(l));
        }
        exprs.swap(new_exprs);
    }
    return exprs;
}

vector<Expr> generate_reassociated_variants(const Expr &e);

struct LinearTerm {
    bool positive;
    Expr e;
};

// This function is very very exponential
void all_possible_exprs_that_compute_sum(const vector<LinearTerm> &terms, vector<Expr> *result) {
    // The number of results is at least n factorial times the (n-1)th catalan
    // number. Let's throw an error rather than trying to produce too
    // much stuff.
    if (terms.size() >= 8) {
        std::cerr << "Too many terms passed to all_possible_exprs_that_compute_sum. "
                  << "Would OOM. Just generating one and not recursing on leaves.\n";
        Expr pos, neg;
        for (auto t : terms) {
            if (t.positive) {
                if (pos.defined()) {
                    pos += t.e;
                } else {
                    pos = t.e;
                }
            } else {
                if (neg.defined()) {
                    neg += t.e;
                } else {
                    neg = t.e;
                }
            }
        }
        if (!pos.defined()) {
            pos = 0;
        }
        if (neg.defined()) {
            pos -= neg;
        }
        result->push_back(pos);
        return;
    }

    if (terms.size() == 1) {
        if (terms[0].positive) {
            vector<Expr> variants = generate_reassociated_variants(terms[0].e);
            result->insert(result->end(), variants.begin(), variants.end());
        }
        return;
    }

    for (size_t i = 1; i < (size_t)((1 << terms.size()) - 1); i++) {
        vector<LinearTerm> left, right;
        for (size_t j = 0; j < terms.size(); j++) {
            if (i & (1 << j)) {
                left.push_back(terms[j]);
            } else {
                right.push_back(terms[j]);
            }
        }
        vector<Expr> left_exprs, right_exprs, right_exprs_negated;
        all_possible_exprs_that_compute_sum(left, &left_exprs);
        all_possible_exprs_that_compute_sum(right, &right_exprs);
        for (auto &t : right) {
            t.positive = !t.positive;
        }
        all_possible_exprs_that_compute_sum(right, &right_exprs_negated);

        for (auto &l : left_exprs) {
            for (auto &r : right_exprs) {
                result->push_back(l + r);
            }
            for (auto &r : right_exprs_negated) {
                result->push_back(l - r);
            }
        }
    }
}

Expr make_binop(IRNodeType t, Expr l, Expr r) {
    if (t == IRNodeType::Min) {
        return min(l, r);
    } else if (t == IRNodeType::Max) {
        return max(l, r);
    } else {
        std::cerr << "Unsupported binop in make_binop: " << t << "\n";
        abort();
    }
}

template<typename Op>
void all_possible_exprs_that_compute_associative_op_helper(const Expr &e,
                                                           vector<Expr> *result) {
    if (!e.as<Op>()) {
        vector<Expr> variants = generate_reassociated_variants(e);
        result->insert(result->end(), variants.begin(), variants.end());
        return;
    }

    vector<Expr> terms = unpack_binary_op<Op>(e);
    for (size_t i = 1; i < (size_t)((1 << terms.size()) - 1); i++) {
        vector<Expr> left, right;
        for (size_t j = 0; j < terms.size(); j++) {
            if (i & (1 << j)) {
                left.push_back(terms[j]);
            } else {
                right.push_back(terms[j]);
            }
        }
        assert(left.size() < terms.size());
        assert(right.size() < terms.size());
        vector<Expr> left_exprs, right_exprs;
        all_possible_exprs_that_compute_associative_op_helper<Op>(pack_binary_op<Op>(left), &left_exprs);
        all_possible_exprs_that_compute_associative_op_helper<Op>(pack_binary_op<Op>(right), &right_exprs);
        for (auto &l : left_exprs) {
            for (auto &r : right_exprs) {
                // Skip non-canonical ones
                if (!l.as<Variable>() &&
                    !r.as<Variable>() &&
                    r.node_type() > l.node_type()) {
                    continue;
                }
                result->push_back(Op::make(l, r));
            }
        }
    }
}

template<typename Op>
void all_possible_exprs_that_compute_associative_op(const Op *op,
                                                    vector<Expr> *result) {
    all_possible_exprs_that_compute_associative_op_helper<Op>(Expr(op), result);
}

template<typename Op>
void all_possible_exprs_that_compute_non_associative_op(const Op *op,
                                                        vector<Expr> *result) {
    for (const Expr &e1 : generate_reassociated_variants(op->a)) {
        for (const Expr &e2 : generate_reassociated_variants(op->b)) {
            result->emplace_back(Op::make(e1, e2));
        }
    }
}

vector<Expr> generate_reassociated_variants(const Expr &e) {
    if (e.as<Add>() || e.as<Sub>()) {
        vector<LinearTerm> terms, pending;
        pending.emplace_back(LinearTerm{true, e});
        while (!pending.empty()) {
            auto next = pending.back();
            pending.pop_back();
            if (const Add *add = next.e.as<Add>()) {
                pending.emplace_back(LinearTerm{next.positive, add->a});
                pending.emplace_back(LinearTerm{next.positive, add->b});
            } else if (const Sub *sub = next.e.as<Sub>()) {
                pending.emplace_back(LinearTerm{next.positive, sub->a});
                pending.emplace_back(LinearTerm{!next.positive, sub->b});
            } else {
                terms.push_back(next);
            }
        }

        // We now have a linear combination of terms and need to
        // generate all possible trees that compute it. We'll generate
        // all possible partitions, then generate all reassociated
        // variants of the left and right, then combine them.
        vector<Expr> result;
        all_possible_exprs_that_compute_sum(terms, &result);
        return result;
    } else if (const Min *op = e.as<Min>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op(op, &result);
        return result;
    } else if (const Max *op = e.as<Max>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op(op, &result);
        return result;
    } else if (const And *op = e.as<And>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op(op, &result);
        return result;
    } else if (const Or *op = e.as<Or>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op(op, &result);
        return result;
    } else if (const Mul *op = e.as<Mul>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op(op, &result);
        return result;
    } else if (const LT *op = e.as<LT>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_non_associative_op(op, &result);
        return result;
    } else if (const LE *op = e.as<LE>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_non_associative_op(op, &result);
        return result;
    } else if (const EQ *op = e.as<EQ>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_non_associative_op(op, &result);
        return result;
    } else if (const NE *op = e.as<NE>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_non_associative_op(op, &result);
        return result;
    } else if (const Div *op = e.as<Div>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_non_associative_op(op, &result);
        return result;
    } else if (const Mod *op = e.as<Mod>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_non_associative_op(op, &result);
        return result;
    } else if (const Select *op = e.as<Select>()) {
        vector<Expr> result;
        for (const Expr &e1 : generate_reassociated_variants(op->condition)) {
            for (const Expr &e2 : generate_reassociated_variants(op->true_value)) {
                for (const Expr &e3 : generate_reassociated_variants(op->false_value)) {
                    result.emplace_back(select(e1, e2, e3));
                }
            }
        }
        return result;
    } else if (const Not *op = e.as<Not>()) {
        vector<Expr> result = generate_reassociated_variants(op->a);
        for (Expr &e : result) {
            e = !e;
        }
        return result;
    } else {
        if (!e.as<Variable>() && !is_const(e) && !e.as<Call>()) {
            // Don't descend into calls (they're folds)
            std::cout << "Warning. Don't know how to reassociate: " << e << "\n";
        }
    }

    return {e};
}
