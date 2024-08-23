#include "Simplify.h"
#include "Simplify_Internal.h"

#include "CSE.h"
#include "CompilerLogger.h"
#include "IRMutator.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

Simplify::Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai)
    : remove_dead_code(r) {

    // Only respect the constant bounds from the containing scope.
    for (auto iter = bi->cbegin(); iter != bi->cend(); ++iter) {
        ExprInfo info;
        if (const int64_t *i_min = as_const_int(iter.value().min)) {
            info.bounds.min_defined = true;
            info.bounds.min = *i_min;
        }
        if (const int64_t *i_max = as_const_int(iter.value().max)) {
            info.bounds.max_defined = true;
            info.bounds.max = *i_max;
        }

        if (const auto *a = ai->find(iter.name())) {
            info.alignment = *a;
        }

        if (info.bounds.min_defined ||
            info.bounds.max_defined ||
            info.alignment.modulus != 1) {
            bounds_and_alignment_info.push(iter.name(), info);
        }
    }

    for (auto iter = ai->cbegin(); iter != ai->cend(); ++iter) {
        if (bounds_and_alignment_info.contains(iter.name())) {
            // Already handled
            continue;
        }
        ExprInfo info;
        info.alignment = iter.value();
        bounds_and_alignment_info.push(iter.name(), info);
    }
}

std::pair<std::vector<Expr>, bool> Simplify::mutate_with_changes(const std::vector<Expr> &old_exprs) {
    vector<Expr> new_exprs(old_exprs.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < old_exprs.size(); i++) {
        const Expr &old_e = old_exprs[i];
        Expr new_e = mutate(old_e, nullptr);
        if (!new_e.same_as(old_e)) {
            changed = true;
        }
        new_exprs[i] = std::move(new_e);
    }

    return {std::move(new_exprs), changed};
}

void Simplify::found_buffer_reference(const string &name, size_t dimensions) {
    for (size_t i = 0; i < dimensions; i++) {
        string stride = name + ".stride." + std::to_string(i);
        if (auto *info = var_info.shallow_find(stride)) {
            info->old_uses++;
        }

        string min = name + ".min." + std::to_string(i);
        if (auto *info = var_info.shallow_find(min)) {
            info->old_uses++;
        }
    }

    if (auto *info = var_info.shallow_find(name)) {
        info->old_uses++;
    }
}

bool Simplify::const_float(const Expr &e, double *f) {
    if (const double *p = as_const_float(e)) {
        *f = *p;
        return true;
    } else {
        return false;
    }
}

bool Simplify::const_int(const Expr &e, int64_t *i) {
    if (const int64_t *p = as_const_int(e)) {
        *i = *p;
        return true;
    } else {
        return false;
    }
}

bool Simplify::const_uint(const Expr &e, uint64_t *u) {
    if (const uint64_t *p = as_const_uint(e)) {
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
        Simplify::ExprInfo i;
        if (v) {
            simplify->mutate(lt->b, &i);
            if (i.bounds.min_defined) {
                // !(v < i)
                learn_lower_bound(v, i.bounds.min);
            }
        }
        v = lt->b.as<Variable>();
        if (v) {
            simplify->mutate(lt->a, &i);
            if (i.bounds.max_defined) {
                // !(i < v)
                learn_upper_bound(v, i.bounds.max);
            }
        }
    } else if (const LE *le = fact.as<LE>()) {
        const Variable *v = le->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(le->b, &i);
            if (i.bounds.min_defined) {
                // !(v <= i)
                learn_lower_bound(v, i.bounds.min + 1);
            }
        }
        v = le->b.as<Variable>();
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(le->a, &i);
            if (i.bounds.max_defined) {
                // !(i <= v)
                learn_upper_bound(v, i.bounds.max - 1);
            }
        }
    } else if (const Call *c = Call::as_tag(fact)) {
        learn_false(c->args[0]);
        return;
    } else if (const Or *o = fact.as<Or>()) {
        // Both must be false
        learn_false(o->a);
        learn_false(o->b);
        return;
    } else if (const Not *n = fact.as<Not>()) {
        learn_true(n->a);
        return;
    }
    if (simplify->falsehoods.insert(fact).second) {
        falsehoods.insert(fact);
    }
}

void Simplify::ScopedFact::learn_upper_bound(const Variable *v, int64_t val) {
    ExprInfo b;
    b.bounds = ConstantInterval::bounded_above(val);
    if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
        b.intersect(*info);
    }
    simplify->bounds_and_alignment_info.push(v->name, b);
    bounds_pop_list.push_back(v);
}

void Simplify::ScopedFact::learn_lower_bound(const Variable *v, int64_t val) {
    ExprInfo b;
    b.bounds = ConstantInterval::bounded_below(val);
    if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
        b.intersect(*info);
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
            if (is_const(eq->b) || eq->b.as<Variable>()) {
                // TODO: consider other cases where we might want to entirely substitute
                info.replacement = eq->b;
                simplify->var_info.push(v->name, info);
                pop_list.push_back(v);
            } else if (v->type.is_int()) {
                // Visit the rhs again to get bounds and alignment info to propagate to the LHS
                // TODO: Visiting it again is inefficient
                Simplify::ExprInfo expr_info;
                simplify->mutate(eq->b, &expr_info);
                if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
                    // We already know something about this variable and don't want to suppress it.
                    expr_info.intersect(*info);
                }
                simplify->bounds_and_alignment_info.push(v->name, expr_info);
                bounds_pop_list.push_back(v);
            }
        } else if (const Variable *vb = eq->b.as<Variable>()) {
            // y % 2 == x
            // We know that LHS is not a const due to
            // canonicalization, and that the LHS is not a variable or
            // the case above would have triggered. Learn from the
            // bounds and alignment of the LHS.
            // TODO: Visiting it again is inefficient
            Simplify::ExprInfo expr_info;
            simplify->mutate(eq->a, &expr_info);
            if (const auto *info = simplify->bounds_and_alignment_info.find(vb->name)) {
                // We already know something about this variable and don't want to suppress it.
                expr_info.intersect(*info);
            }
            simplify->bounds_and_alignment_info.push(vb->name, expr_info);
            bounds_pop_list.push_back(vb);
        } else if (modulus && remainder && (v = m->a.as<Variable>())) {
            // Learn from expressions of the form x % 8 == 3
            Simplify::ExprInfo expr_info;
            expr_info.alignment.modulus = *modulus;
            expr_info.alignment.remainder = *remainder;
            if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
                // We already know something about this variable and don't want to suppress it.
                expr_info.intersect(*info);
            }
            simplify->bounds_and_alignment_info.push(v->name, expr_info);
            bounds_pop_list.push_back(v);
        }
    } else if (const LT *lt = fact.as<LT>()) {
        const Variable *v = lt->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(lt->b, &i);
            if (i.bounds.max_defined) {
                // v < i
                learn_upper_bound(v, i.bounds.max - 1);
            }
        }
        v = lt->b.as<Variable>();
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(lt->a, &i);
            if (i.bounds.min_defined) {
                // i < v
                learn_lower_bound(v, i.bounds.min + 1);
            }
        }
    } else if (const LE *le = fact.as<LE>()) {
        const Variable *v = le->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v) {
            simplify->mutate(le->b, &i);
            if (i.bounds.max_defined) {
                // v <= i
                learn_upper_bound(v, i.bounds.max);
            }
        }
        v = le->b.as<Variable>();
        if (v) {
            simplify->mutate(le->a, &i);
            if (i.bounds.min_defined) {
                // i <= v
                learn_lower_bound(v, i.bounds.min);
            }
        }
    } else if (const Call *c = Call::as_tag(fact)) {
        learn_true(c->args[0]);
        return;
    } else if (const And *a = fact.as<And>()) {
        // Both must be true
        learn_true(a->a);
        learn_true(a->b);
        return;
    } else if (const Not *n = fact.as<Not>()) {
        learn_false(n->a);
        return;
    }
    if (simplify->truths.insert(fact).second) {
        truths.insert(fact);
    }
}

namespace {
template<typename T>
T substitute_facts_impl(const T &t,
                        const std::set<Expr, IRDeepCompare> &truths,
                        const std::set<Expr, IRDeepCompare> &falsehoods) {
    class Substitutor : public IRMutator {
        const std::set<Expr, IRDeepCompare> &truths, &falsehoods;

    public:
        using IRMutator::mutate;
        Expr mutate(const Expr &e) override {
            if (!e.type().is_bool()) {
                return IRMutator::mutate(e);
            } else if (truths.count(e)) {
                return make_one(e.type());
            } else if (falsehoods.count(e)) {
                return make_zero(e.type());
            } else {
                return IRMutator::mutate(e);
            }
        }

        Substitutor(const std::set<Expr, IRDeepCompare> &t,
                    const std::set<Expr, IRDeepCompare> &f)
            : truths(t), falsehoods(f) {
        }
    } substitutor(truths, falsehoods);

    return substitutor.mutate(t);
}
}  // namespace

Expr Simplify::ScopedFact::substitute_facts(const Expr &e) {
    return substitute_facts_impl(e, truths, falsehoods);
}

Stmt Simplify::ScopedFact::substitute_facts(const Stmt &s) {
    return substitute_facts_impl(s, truths, falsehoods);
}

Simplify::ScopedFact::~ScopedFact() {
    for (const auto *v : pop_list) {
        simplify->var_info.pop(v->name);
    }
    for (const auto *v : bounds_pop_list) {
        simplify->bounds_and_alignment_info.pop(v->name);
    }
    for (const auto &e : truths) {
        simplify->truths.erase(e);
    }
    for (const auto &e : falsehoods) {
        simplify->falsehoods.erase(e);
    }
}

Expr simplify(const Expr &e, bool remove_dead_let_stmts,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment,
              const std::vector<Expr> &assumptions) {
    Simplify m(remove_dead_let_stmts, &bounds, &alignment);
    std::vector<Simplify::ScopedFact> facts;
    for (const Expr &a : assumptions) {
        facts.push_back(m.scoped_truth(a));
    }
    Expr result = m.mutate(e, nullptr);
    if (m.in_unreachable) {
        return unreachable(e.type());
    }
    return result;
}

Stmt simplify(const Stmt &s, bool remove_dead_let_stmts,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment,
              const std::vector<Expr> &assumptions) {
    Simplify m(remove_dead_let_stmts, &bounds, &alignment);
    std::vector<Simplify::ScopedFact> facts;
    for (const Expr &a : assumptions) {
        facts.push_back(m.scoped_truth(a));
    }
    Stmt result = m.mutate(s);
    if (m.in_unreachable) {
        return Evaluate::make(unreachable());
    }
    return result;
}

class SimplifyExprs : public IRMutator {
public:
    using IRMutator::mutate;
    Expr mutate(const Expr &e) override {
        return simplify(e);
    }
};

Stmt simplify_exprs(const Stmt &s) {
    return SimplifyExprs().mutate(s);
}

bool can_prove(Expr e, const Scope<Interval> &bounds) {
    internal_assert(e.type().is_bool())
        << "Argument to can_prove is not a boolean Expr: " << e << "\n";

    e = remove_likelies(e);
    e = common_subexpression_elimination(e);

    Expr orig = e;

    e = simplify(e, true, bounds);

    // Take a closer look at all failed proof attempts to hunt for
    // simplifier weaknesses
    const bool check_failed_proofs = debug::debug_level() > 0 || get_compiler_logger() != nullptr;
    if (check_failed_proofs && !is_const(e)) {
        struct RenameVariables : public IRMutator {
            using IRMutator::visit;

            Expr visit(const Variable *op) override {
                auto it = vars.find(op->name);
                if (const std::string *n = lets.find(op->name)) {
                    return Variable::make(op->type, *n);
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
            for (const auto &p : renamer.out_vars) {
                if (p.first.is_handle()) {
                    // This aint gonna work
                    return false;
                }
                s[p.second] = make_const(p.first, (int)(rng() & 0xffff) - 0x7fff);
            }
            Expr probe = unwrap_tags(simplify(substitute(s, e)));
            if (!is_const_one(probe)) {
                // Found a counter-example, or something that fails to fold
                return false;
            }
        }

        if (get_compiler_logger()) {
            get_compiler_logger()->record_failed_to_prove(e, orig);
        }

        debug(1) << "Failed to prove, but could not find a counter-example:\n " << e << "\n";
        debug(1) << "Original expression:\n"
                 << orig << "\n";
        return false;
    }

    return is_const_one(e);
}

}  // namespace Internal
}  // namespace Halide
