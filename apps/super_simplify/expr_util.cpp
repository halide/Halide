#include "expr_util.h"

#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::string;

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
