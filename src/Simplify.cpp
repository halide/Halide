#include "Simplify.h"
#include "Simplify_Internal.h"

#include "IRMutator.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

#if LOG_EXPR_MUTATIONS || LOG_STMT_MUTATIONS
int Simplify::debug_indent = 0;
#endif

Simplify::Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai) :
    remove_dead_lets(r), no_float_simplify(false) {

    // Only respect the constant bounds from the containing scope.
    for (auto iter = bi->cbegin(); iter != bi->cend(); ++iter) {
        ExprInfo bounds;
        if (const int64_t *i_min = as_const_int(iter.value().min)) {
            bounds.min_defined = true;
            bounds.min = *i_min;
        }
        if (const int64_t *i_max = as_const_int(iter.value().max)) {
            bounds.max_defined = true;
            bounds.max = *i_max;
        }

        if (ai->contains(iter.name())) {
            bounds.alignment = ai->get(iter.name());
        }

        if (bounds.min_defined || bounds.max_defined || bounds.alignment.modulus != 1) {
            bounds_and_alignment_info.push(iter.name(), bounds);
        }


    }

    for (auto iter = ai->cbegin(); iter != ai->cend(); ++iter) {
        if (bounds_and_alignment_info.contains(iter.name())) {
            // Already handled
            continue;
        }
        ExprInfo bounds;
        bounds.alignment = iter.value();
        bounds_and_alignment_info.push(iter.name(), bounds);
    }

}

void Simplify::found_buffer_reference(const string &name, size_t dimensions) {
    for (size_t i = 0; i < dimensions; i++) {
        string stride = name + ".stride." + std::to_string(i);
        if (var_info.contains(stride)) {
            var_info.ref(stride).old_uses++;
        }

        string min = name + ".min." + std::to_string(i);
        if (var_info.contains(min)) {
            var_info.ref(min).old_uses++;
        }
    }

    if (var_info.contains(name)) {
        var_info.ref(name).old_uses++;
    }
}

bool Simplify::const_float(const Expr &e, double *f) {
    if (e.type().is_vector()) {
        return false;
    } else if (const double *p = as_const_float(e)) {
        *f = *p;
        return true;
    } else {
        return false;
    }
}

bool Simplify::const_int(const Expr &e, int64_t *i) {
    if (e.type().is_vector()) {
        return false;
    } else if (const int64_t *p = as_const_int(e)) {
        *i = *p;
        return true;
    } else {
        return false;
    }
}

bool Simplify::const_uint(const Expr &e, uint64_t *u) {
    if (e.type().is_vector()) {
        return false;
    } else if (const uint64_t *p = as_const_uint(e)) {
        *u = *p;
        return true;
    } else {
        return false;
    }
}

void Simplify::ScopedFact::learn_false(const Expr &fact) {
    Simplify::VarInfo info;
    info.old_uses = info.new_uses = 0;
    if (const Variable *v = fact.as<Variable>()) {
        info.replacement = const_false(fact.type().lanes());
        simplify->var_info.push(v->name, info);
        pop_list.push_back(v);
    } else if (const NE *ne = fact.as<NE>()) {
        const Variable *v = ne->a.as<Variable>();
        if (v && is_const(ne->b)) {
            info.replacement = ne->b;
            simplify->var_info.push(v->name, info);
            pop_list.push_back(v);
        }
    } else if (const LT *lt = fact.as<LT>()) {
        const Variable *v = lt->a.as<Variable>();
        const int64_t *i = as_const_int(lt->b);
        if (v && i) {
            // !(v < i)
            learn_lower_bound(v, *i);
        }
        v = lt->b.as<Variable>();
        i = as_const_int(lt->a);
        if (v && i) {
            // !(i < v)
            learn_upper_bound(v, *i);
        }
    } else if (const LE *le = fact.as<LE>()) {
        const Variable *v = le->a.as<Variable>();
        const int64_t *i = as_const_int(le->b);
        if (v && i) {
            // !(v <= i)
            learn_lower_bound(v, *i + 1);
        }
        v = le->b.as<Variable>();
        i = as_const_int(le->a);
        if (v && i) {
            // !(i <= v)
            learn_upper_bound(v, *i - 1);
        }
    } else if (const Or *o = fact.as<Or>()) {
        // Both must be false
        learn_false(o->a);
        learn_false(o->b);
    } else if (const Not *n = fact.as<Not>()) {
        learn_true(n->a);
    } else if (simplify->falsehoods.insert(fact).second) {
        falsehoods.push_back(fact);
    }
}

void Simplify::ScopedFact::learn_upper_bound(const Variable *v, int64_t val) {
    ExprInfo b;
    b.max_defined = true;
    b.max = val;
    if (simplify->bounds_and_alignment_info.contains(v->name)) {
        b.intersect(simplify->bounds_and_alignment_info.get(v->name));
    }
    simplify->bounds_and_alignment_info.push(v->name, b);
    bounds_pop_list.push_back(v);
}

void Simplify::ScopedFact::learn_lower_bound(const Variable *v, int64_t val) {
    ExprInfo b;
    b.min_defined = true;
    b.min = val;
    if (simplify->bounds_and_alignment_info.contains(v->name)) {
        b.intersect(simplify->bounds_and_alignment_info.get(v->name));
    }
    simplify->bounds_and_alignment_info.push(v->name, b);
    bounds_pop_list.push_back(v);
}

void Simplify::ScopedFact::learn_true(const Expr &fact) {
    Simplify::VarInfo info;
    info.old_uses = info.new_uses = 0;
    if (const Variable *v = fact.as<Variable>()) {
        info.replacement = const_true(fact.type().lanes());
        simplify->var_info.push(v->name, info);
        pop_list.push_back(v);
    } else if (const EQ *eq = fact.as<EQ>()) {
        const Variable *v = eq->a.as<Variable>();
        const Mod *m = eq->a.as<Mod>();
        const int64_t *modulus = m ? as_const_int(m->b) : nullptr;
        const int64_t *remainder = m ? as_const_int(eq->b) : nullptr;
        if (v) {
            if (is_const(eq->b)) {
                info.replacement = eq->b;
                simplify->var_info.push(v->name, info);
                pop_list.push_back(v);
            } else if (v->type.is_int()) {
                // Visit the rhs again to get bounds and alignment info to propagate to the LHS
                // TODO: Visiting it again is inefficient
                Simplify::ExprInfo expr_info;
                simplify->mutate(eq->b, &expr_info);
                if (simplify->bounds_and_alignment_info.contains(v->name)) {
                    // We already know something about this variable and don't want to suppress it.
                    auto existing_knowledge = simplify->bounds_and_alignment_info.get(v->name);
                    expr_info.intersect(existing_knowledge);
                }
                simplify->bounds_and_alignment_info.push(v->name, expr_info);
                bounds_pop_list.push_back(v);
            }
        } else if (modulus && remainder && (v = m->a.as<Variable>())) {
            // Learn from expressions of the form x % 8 == 3
            Simplify::ExprInfo expr_info;
            expr_info.alignment.modulus = *modulus;
            expr_info.alignment.remainder = *remainder;
            if (simplify->bounds_and_alignment_info.contains(v->name)) {
                // We already know something about this variable and don't want to suppress it.
                auto existing_knowledge = simplify->bounds_and_alignment_info.get(v->name);
                expr_info.intersect(existing_knowledge);
            }
            simplify->bounds_and_alignment_info.push(v->name, expr_info);
            bounds_pop_list.push_back(v);
        }
    } else if (const LT *lt = fact.as<LT>()) {
        const Variable *v = lt->a.as<Variable>();
        const int64_t *i = as_const_int(lt->b);
        if (v && i) {
            // v < i
            learn_upper_bound(v, *i - 1);
        }
        v = lt->b.as<Variable>();
        i = as_const_int(lt->a);
        if (v && i) {
            // i < v
            learn_lower_bound(v, *i + 1);
        }
    } else if (const LE *le = fact.as<LE>()) {
        const Variable *v = le->a.as<Variable>();
        const int64_t *i = as_const_int(le->b);
        if (v && i) {
            // v <= i
            learn_upper_bound(v, *i);
        }
        v = le->b.as<Variable>();
        i = as_const_int(le->a);
        if (v && i) {
            // i <= v
            learn_lower_bound(v, *i);
        }
    } else if (const And *a = fact.as<And>()) {
        // Both must be true
        learn_true(a->a);
        learn_true(a->b);
    } else if (const Not *n = fact.as<Not>()) {
        learn_false(n->a);
    } else if (simplify->truths.insert(fact).second) {
        truths.push_back(fact);
    }
}

Simplify::ScopedFact::~ScopedFact() {
    for (auto v : pop_list) {
        simplify->var_info.pop(v->name);
    }
    for (auto v : bounds_pop_list) {
        simplify->bounds_and_alignment_info.pop(v->name);
    }
    for (const auto &e : truths) {
        simplify->truths.erase(e);
    }
    for (const auto &e : falsehoods) {
        simplify->falsehoods.erase(e);
    }
}

Expr simplify(Expr e, bool remove_dead_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(remove_dead_lets, &bounds, &alignment).mutate(e, nullptr);
}

Stmt simplify(Stmt s, bool remove_dead_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(remove_dead_lets, &bounds, &alignment).mutate(s);
}

class SimplifyExprs : public IRMutator2 {
public:
    using IRMutator2::mutate;
    Expr mutate(const Expr &e) override {
        return simplify(e);
    }
};

Stmt simplify_exprs(Stmt s) {
    return SimplifyExprs().mutate(s);
}

bool can_prove(Expr e, const Scope<Interval> &bounds) {
    internal_assert(e.type().is_bool())
        << "Argument to can_prove is not a boolean Expr: " << e << "\n";

    // Remove likelies
    struct RemoveLikelies : public IRMutator2 {
        using IRMutator2::visit;
        Expr visit(const Call *op) override {
            if (op->is_intrinsic(Call::likely) ||
                op->is_intrinsic(Call::likely_if_innermost)) {
                return mutate(op->args[0]);
            } else {
                return IRMutator2::visit(op);
            }
        }
    };
    e = RemoveLikelies().mutate(e);

    e = simplify(e, true, bounds);

    // Take a closer look at all failed proof attempts to hunt for
    // simplifier weaknesses
    if (debug::debug_level() > 0 && !is_const(e)) {
        struct RenameVariables : public IRMutator2 {
            using IRMutator2::visit;

            Expr visit(const Variable *op) override {
                auto it = vars.find(op->name);
                if (lets.contains(op->name)) {
                    return Variable::make(op->type, lets.get(op->name));
                } else if (it == vars.end()) {
                    std::string name = "v" + std::to_string(count++);
                    vars[op->name] = name;
                    out_vars.emplace_back(op->type, name);
                    return Variable::make(op->type, name);
                } else {
                    return Variable::make(op->type, it->second);
                }
            }

            Expr visit(const Let *op) override {
                std::string name = "v" + std::to_string(count++);
                ScopedBinding<string> bind(lets, op->name, name);
                return Let::make(name, mutate(op->value), mutate(op->body));
            }

            int count = 0;
            map<string, string> vars;
            Scope<string> lets;
            std::vector<pair<Type, string>> out_vars;
        } renamer;

        e = renamer.mutate(e);

        // Look for a concrete counter-example with random probing
        static std::mt19937 rng(0);
        for (int i = 0; i < 100; i++) {
            map<string, Expr> s;
            for (auto p : renamer.out_vars) {
                if (p.first.is_handle()) {
                    // This aint gonna work
                    return false;
                }
                s[p.second] = make_const(p.first, (int)(rng() & 0xffff) - 0x7fff);
            }
            Expr probe = simplify(substitute(s, e));
            if (const Call *c = probe.as<Call>()) {
                if (c->is_intrinsic(Call::likely) ||
                    c->is_intrinsic(Call::likely_if_innermost)) {
                    probe = c->args[0];
                }
            }
            if (!is_one(probe)) {
                // Found a counter-example, or something that fails to fold
                return false;
            }
        }

        debug(0) << "Failed to prove, but could not find a counter-example:\n " << e << "\n";
        return false;
    }

    return is_one(e);
}

}  // namespace Internal
}  // namespace Halide
