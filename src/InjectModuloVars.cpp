#include "InjectModuloVars.h"

#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Simplify.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Halide {
namespace Internal {

namespace {

// Reducing modulo m is only exact on integer types where overflow is
// undefined. On a type that wraps, a value and its reduction agree modulo m
// only if m divides the wrap-around, which isn't worth chasing.
bool suitable_type(Type t) {
    return t.is_int() && t.bits() >= 32;
}

// Note the check against zero: x % 0 is defined to be 0 rather than x, so it
// tells us nothing about x.
bool is_multiple_of(const Expr &e, int64_t m) {
    auto c = as_const_int(e);
    return c && *c != 0 && mod_imp(*c, m) == 0;
}

// Call f on every Variable sitting in a position where only its value modulo
// m matters. Addition, subtraction and multiplication all commute with
// reduction, a ramp's base and stride each contribute linearly, and a value
// already reduced modulo a multiple of m is congruent to the original.
// Division is not on the list, and anything else is opaque.
template<typename Fn>
void for_each_congruent_var(const Expr &e, int64_t m, Fn f) {
    if (const Add *op = e.as<Add>()) {
        for_each_congruent_var(op->a, m, f);
        for_each_congruent_var(op->b, m, f);
    } else if (const Sub *op = e.as<Sub>()) {
        for_each_congruent_var(op->a, m, f);
        for_each_congruent_var(op->b, m, f);
    } else if (const Mul *op = e.as<Mul>()) {
        for_each_congruent_var(op->a, m, f);
        for_each_congruent_var(op->b, m, f);
    } else if (const Ramp *op = e.as<Ramp>()) {
        for_each_congruent_var(op->base, m, f);
        for_each_congruent_var(op->stride, m, f);
    } else if (const Broadcast *op = e.as<Broadcast>()) {
        for_each_congruent_var(op->value, m, f);
    } else if (const Mod *op = e.as<Mod>()) {
        if (is_multiple_of(op->b, m)) {
            for_each_congruent_var(op->a, m, f);
        }
    } else if (const Variable *op = e.as<Variable>()) {
        f(op);
    }
}

// Rebuild e with the variables in those same positions replaced.
Expr substitute_congruent_vars(const Expr &e, int64_t m,
                               const std::map<std::string, Expr> &replacements) {
    if (const Add *op = e.as<Add>()) {
        return Add::make(substitute_congruent_vars(op->a, m, replacements),
                         substitute_congruent_vars(op->b, m, replacements));
    } else if (const Sub *op = e.as<Sub>()) {
        return Sub::make(substitute_congruent_vars(op->a, m, replacements),
                         substitute_congruent_vars(op->b, m, replacements));
    } else if (const Mul *op = e.as<Mul>()) {
        return Mul::make(substitute_congruent_vars(op->a, m, replacements),
                         substitute_congruent_vars(op->b, m, replacements));
    } else if (const Ramp *op = e.as<Ramp>()) {
        return Ramp::make(substitute_congruent_vars(op->base, m, replacements),
                          substitute_congruent_vars(op->stride, m, replacements),
                          op->lanes);
    } else if (const Broadcast *op = e.as<Broadcast>()) {
        return Broadcast::make(substitute_congruent_vars(op->value, m, replacements),
                               op->lanes);
    } else if (const Mod *op = e.as<Mod>()) {
        if (is_multiple_of(op->b, m)) {
            return Mod::make(substitute_congruent_vars(op->a, m, replacements), op->b);
        }
    } else if (const Variable *op = e.as<Variable>()) {
        auto it = replacements.find(op->name);
        if (it != replacements.end() && it->second.type() == op->type) {
            return it->second;
        }
    }
    return e;
}

template<typename V>
const V *find(const std::map<std::string, V> &m, const std::string &key) {
    auto it = m.find(key);
    return it == m.end() ? nullptr : &it->second;
}

// Which let-bound variables are needed modulo which constants, and what the
// lets bind them to.
class FindModuloUses : public IRVisitor {
    using IRVisitor::visit;

    void note(const Expr &e, int64_t m) {
        for_each_congruent_var(e, m, [&](const Variable *v) {
            if (let_values.count(v->name)) {
                required[v->name].insert(m);
            }
        });
    }

    void visit(const Mod *op) override {
        if (suitable_type(op->type.element_of())) {
            if (auto m = as_const_int(op->b); m && *m > 1) {
                note(op->a, *m);
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const Let *op) override {
        if (suitable_type(op->value.type().element_of())) {
            let_values[op->name] = op->value;
        }
        IRVisitor::visit(op);
    }

    void visit(const LetStmt *op) override {
        if (suitable_type(op->value.type().element_of())) {
            let_values[op->name] = op->value;
        }
        IRVisitor::visit(op);
    }

public:
    std::map<std::string, std::set<int64_t>> required;
    std::map<std::string, Expr> let_values;

    // A value that must be known modulo m needs the things it is built from
    // known modulo m too. Chase that to a fixed point.
    void close_over_let_values() {
        std::vector<std::pair<std::string, int64_t>> worklist;
        for (const auto &[name, moduli] : required) {
            for (int64_t m : moduli) {
                worklist.emplace_back(name, m);
            }
        }
        while (!worklist.empty()) {
            // Not a structured binding: the lambda below captures the modulus,
            // and capturing a structured binding is C++20.
            const std::pair<std::string, int64_t> item = worklist.back();
            worklist.pop_back();
            const int64_t m = item.second;
            for_each_congruent_var(let_values[item.first], m, [&](const Variable *v) {
                if (let_values.count(v->name) && required[v->name].insert(m).second) {
                    worklist.emplace_back(v->name, m);
                }
            });
        }
    }
};

// The bounds a scalar Parameter is constrained to. add_parameter_checks
// asserts these on entry, so the simplifier may rely on them, but by this
// point in lowering the constrained copy of the parameter is long gone.
class FindParamBounds : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (op->param.defined() && !op->param.is_buffer() &&
            !bounds.contains(op->name)) {
            Expr lo = op->param.min_value(), hi = op->param.max_value();
            if (lo.defined() || hi.defined()) {
                bounds.push(op->name, Interval(lo.defined() ? lo : Interval::neg_inf(),
                                               hi.defined() ? hi : Interval::pos_inf()));
            }
        }
    }

public:
    Scope<Interval> bounds;
};

class InjectModuloVars : public IRMutator {
    using IRMutator::visit;

    const std::map<std::string, std::set<int64_t>> &required;
    const std::map<std::string, Expr> &let_values;
    const Scope<Interval> &param_bounds;

    // How deep we're willing to look through lets while building a reduced
    // value, and how big we'll let the result get before giving up. Both only
    // bound work done to decide whether a reduction collapses; nothing this
    // large is ever added to the IR.
    static constexpr int max_inline_depth = 4;
    static constexpr int max_nodes = 1000;
    int nodes_built = 0;

    // Conditions asserted by statements that dominate the one we're in.
    // A require() on a parameter lands here, which is often what makes a
    // reduction collapse: nothing else tells the simplifier that an offset
    // is small enough to be its own remainder.
    std::vector<Expr> assumptions;

    // For a variable needed modulo m, the constant or variable it is congruent
    // to, keyed by the name of the variable we bound that to. Reductions that
    // didn't collapse aren't in here, and aren't bound at all.
    std::map<int64_t, std::map<std::string, Expr>> congruent_value;
    std::map<int64_t, std::map<std::string, Expr>> congruent_var;

    static std::string mod_var_name(const std::string &name, int64_t m) {
        return name + ".mod." + std::to_string(m);
    }

    // Build something congruent to e modulo m, in the hope that it collapses.
    // A variable in a congruence-preserving position becomes whatever we
    // already know it to be congruent to, or failing that its own let value,
    // so that terms which cancel get the chance to meet.
    Expr reduce(const Expr &e, int64_t m, int depth) {
        if (nodes_built++ > max_nodes) {
            return e;
        }
        if (const Add *op = e.as<Add>()) {
            return Add::make(reduce(op->a, m, depth), reduce(op->b, m, depth));
        } else if (const Sub *op = e.as<Sub>()) {
            return Sub::make(reduce(op->a, m, depth), reduce(op->b, m, depth));
        } else if (const Mul *op = e.as<Mul>()) {
            return Mul::make(reduce(op->a, m, depth), reduce(op->b, m, depth));
        } else if (const Ramp *op = e.as<Ramp>()) {
            return Ramp::make(reduce(op->base, m, depth), reduce(op->stride, m, depth), op->lanes);
        } else if (const Broadcast *op = e.as<Broadcast>()) {
            return Broadcast::make(reduce(op->value, m, depth), op->lanes);
        } else if (const Mod *op = e.as<Mod>()) {
            if (is_multiple_of(op->b, m)) {
                return Mod::make(reduce(op->a, m, depth), op->b);
            }
        } else if (const Variable *op = e.as<Variable>()) {
            if (const Expr *known = find(congruent_value[m], op->name)) {
                return *known;
            }
            if (depth < max_inline_depth) {
                if (const Expr *value = find(let_values, op->name)) {
                    if (value->type() == op->type) {
                        return reduce(*value, m, depth + 1);
                    }
                }
            }
        }
        return e;
    }

    // The reductions to bind just inside a let of this name and value, in
    // order. Records what they are congruent to so later reductions and use
    // sites can pick them up.
    std::vector<std::pair<std::string, Expr>> reductions_for(const std::string &name,
                                                             const Expr &value) {
        std::vector<std::pair<std::string, Expr>> result;
        auto it = required.find(name);
        if (it == required.end()) {
            return result;
        }
        for (int64_t m : it->second) {
            nodes_built = 0;
            Expr reduced = reduce(value, m, 0);
            reduced = simplify(Mod::make(reduced, make_const(value.type(), m)),
                               param_bounds, Scope<ModulusRemainder>::empty_scope(), assumptions);
            // Only worth binding if it collapsed to something the simplifier
            // will substitute back in at the use site.
            if (!is_const(reduced) && !reduced.as<Variable>()) {
                continue;
            }
            std::string new_name = mod_var_name(name, m);
            result.emplace_back(new_name, reduced);
            congruent_value[m][name] = reduced;
            congruent_var[m][name] = Variable::make(value.type(), new_name);
        }
        return result;
    }

    void forget(const std::string &name,
                const std::vector<std::pair<std::string, Expr>> &reductions) {
        for (const auto &[new_name, value] : reductions) {
            // Recover the modulus from the name we just made.
            for (auto &[m, names] : congruent_value) {
                if (mod_var_name(name, m) == new_name) {
                    names.erase(name);
                    congruent_var[m].erase(name);
                }
            }
        }
    }

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first);
        bool pushed = false;
        if (const AssertStmt *a = first.as<AssertStmt>()) {
            if (is_pure(a->condition)) {
                assumptions.push_back(a->condition);
                pushed = true;
            }
        }
        Stmt rest = mutate(op->rest);
        if (pushed) {
            assumptions.pop_back();
        }
        if (first.same_as(op->first) && rest.same_as(op->rest)) {
            return op;
        }
        return Block::make(first, rest);
    }

    Expr visit(const Mod *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);
        if (suitable_type(op->type.element_of())) {
            if (auto m = as_const_int(b); m && *m > 1) {
                auto it = congruent_var.find(*m);
                if (it != congruent_var.end()) {
                    a = substitute_congruent_vars(a, *m, it->second);
                }
            }
        }
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        }
        return Mod::make(a, b);
    }

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        auto reductions = reductions_for(op->name, value);
        Expr body = mutate(op->body);
        forget(op->name, reductions);
        for (auto it = reductions.rbegin(); it != reductions.rend(); it++) {
            body = Let::make(it->first, it->second, body);
        }
        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        }
        return Let::make(op->name, value, body);
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);
        auto reductions = reductions_for(op->name, value);
        Stmt body = mutate(op->body);
        forget(op->name, reductions);
        for (auto it = reductions.rbegin(); it != reductions.rend(); it++) {
            body = LetStmt::make(it->first, it->second, body);
        }
        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        }
        return LetStmt::make(op->name, value, body);
    }

public:
    InjectModuloVars(const std::map<std::string, std::set<int64_t>> &required,
                     const std::map<std::string, Expr> &let_values,
                     const Scope<Interval> &param_bounds)
        : required(required), let_values(let_values), param_bounds(param_bounds) {
    }

    using IRMutator::mutate;
};

}  // namespace

Stmt inject_modulo_vars(const Stmt &s) {
    FindModuloUses finder;
    s.accept(&finder);
    if (finder.required.empty()) {
        return s;
    }
    finder.close_over_let_values();

    FindParamBounds params;
    s.accept(&params);

    return InjectModuloVars(finder.required, finder.let_values, params.bounds).mutate(s);
}

}  // namespace Internal
}  // namespace Halide
