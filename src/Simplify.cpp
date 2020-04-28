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

#if (LOG_EXPR_MUTATIONS || LOG_STMT_MUTATIONS)
int Simplify::debug_indent = 0;
#endif

namespace {

bool get_use_synthesized_rules_from_environment() {
    static string env_var_value = get_env_variable("HL_USE_SYNTHESIZED_RULES");
    static bool enable = env_var_value == "1";
    static bool disable = env_var_value == "0";
    if (enable == disable) {
        // Nag people to set this one way or the other, to avoid
        // running the wrong experiment by mistake.
        user_warning << "HL_USE_SYNTHESIZED_RULES unset\n";
    }
    return enable;
}

}  // namespace

Simplify::Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai)
    : remove_dead_lets(r), no_float_simplify(false) {

    use_synthesized_rules = get_use_synthesized_rules_from_environment();

    // Only respect the constant bounds from the containing scope.
    if (bi) {
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
    }

    if (ai) {
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

void Simplify::learn_info(const Variable *v, const ExprInfo &info) {
    if (bounds_and_alignment_info.contains(v->name)) {
        bounds_and_alignment_info.ref(v->name).intersect(info);
    } else {
        bounds_and_alignment_info.push(v->name, info);
    }
}

void Simplify::ScopedFact::learn_info(const Variable *v, const ExprInfo &info) {
    ExprInfo b = info;
    if (simplify->bounds_and_alignment_info.contains(v->name)) {
        b.intersect(simplify->bounds_and_alignment_info.get(v->name));
    }
    simplify->bounds_and_alignment_info.push(v->name, b);
    bounds_pop_list.push_back(v);
}

namespace {

void learn_false_helper(const Expr &fact, Simplify *simplify, Simplify::ScopedFact *scoped);

void learn_true_helper(const Expr &fact, Simplify *simplify, Simplify::ScopedFact *scoped) {

    Simplify::VarInfo info;
    Simplify::ExprInfo expr_info;
    const Variable *v = nullptr;
    info.old_uses = info.new_uses = 0;
    if (const Variable *var = fact.as<Variable>()) {
        info.replacement = const_true(fact.type().lanes());
        simplify->var_info.push(var->name, info);
        if (scoped) {
            scoped->pop_list.push_back(var);
        }
    } else if (const NE *ne = fact.as<NE>()) {
        learn_false_helper(ne->a == ne->b, simplify, scoped);
    } else if (const EQ *eq = fact.as<EQ>()) {
        v = eq->a.as<Variable>();
        const Mod *m = eq->a.as<Mod>();
        const int64_t *modulus = m ? as_const_int(m->b) : nullptr;
        const int64_t *remainder = m ? as_const_int(eq->b) : nullptr;
        if (v) {
            if (is_const(eq->b)) {
                // TODO: consider other cases where we might want to entirely substitute
                info.replacement = eq->b;
                simplify->var_info.push(v->name, info);
                if (scoped) {
                    scoped->pop_list.push_back(v);
                }
            } else if (v->type.is_int()) {
                // Visit the rhs again to get bounds and alignment info to propagate to the LHS
                // TODO: Visiting it again is inefficient
                simplify->mutate(eq->b, &expr_info);
            }
        } else if (modulus && remainder && (v = m->a.as<Variable>())) {
            // Learn from expressions of the form x % 8 == 3
            expr_info.alignment.modulus = *modulus;
            expr_info.alignment.remainder = *remainder;
        }

        const Variable *var_b = eq->b.as<Variable>();
        if (var_b && var_b->type.is_int()) {
            // Learn from x % 8 == y
            Simplify::ExprInfo i;
            simplify->mutate(eq->a, &i);
            if (scoped) {
                scoped->learn_info(var_b, i);
            } else {
                simplify->learn_info(var_b, i);
            }
        }

    } else if (const LT *lt = fact.as<LT>()) {
        v = lt->a.as<Variable>();
        const int64_t *i = as_const_int(lt->b);
        if (v && i) {
            // v < i
            expr_info.max_defined = true;
            expr_info.max = *i - 1;
        } else {
            v = lt->b.as<Variable>();
            i = as_const_int(lt->a);
            if (v && i) {
                // i < v
                expr_info.min_defined = true;
                expr_info.min = *i + 1;
            }
        }
    } else if (const LE *le = fact.as<LE>()) {
        v = le->a.as<Variable>();
        const int64_t *i = as_const_int(le->b);
        if (v && i) {
            // v <= i
            expr_info.max_defined = true;
            expr_info.max = *i;
        } else {
            v = le->b.as<Variable>();
            i = as_const_int(le->a);
            if (v && i) {
                // i <= v
                expr_info.min_defined = true;
                expr_info.min = *i;
            }
        }
    } else if (const And *a = fact.as<And>()) {
        // Both must be true
        learn_true_helper(a->a, simplify, scoped);
        learn_true_helper(a->b, simplify, scoped);
    } else if (const Not *n = fact.as<Not>()) {
        learn_false_helper(n->a, simplify, scoped);
    }

    if (simplify->truths.insert(fact).second && scoped) {
        scoped->truths.push_back(fact);
    }

    if (v && (expr_info.min_defined ||
              expr_info.max_defined ||
              expr_info.alignment.modulus != 1)) {
        if (scoped) {
            scoped->learn_info(v, expr_info);
        } else {
            simplify->learn_info(v, expr_info);
        }
    }
}

void learn_false_helper(const Expr &fact, Simplify *simplify, Simplify::ScopedFact *scoped) {
    Simplify::VarInfo info;
    Simplify::ExprInfo expr_info;
    const Variable *v = nullptr;
    info.old_uses = info.new_uses = 0;
    if (const Variable *var = fact.as<Variable>()) {
        info.replacement = const_false(fact.type().lanes());
        simplify->var_info.push(var->name, info);
        if (scoped) {
            scoped->pop_list.push_back(var);
        }
    } else if (const NE *ne = fact.as<NE>()) {
        v = ne->a.as<Variable>();
        if (v && is_const(ne->b)) {
            info.replacement = ne->b;
            simplify->var_info.push(v->name, info);
            if (scoped) {
                scoped->pop_list.push_back(v);
            }
        }

        const Variable *var_b = ne->b.as<Variable>();
        if (var_b && var_b->type.is_int()) {
            // Learn from !(x % 8 != y)
            Simplify::ExprInfo i;
            simplify->mutate(ne->a, &i);
            if (scoped) {
                scoped->learn_info(var_b, i);
            } else {
                simplify->learn_info(var_b, i);
            }
        }

    } else if (const LT *lt = fact.as<LT>()) {
        v = lt->a.as<Variable>();
        const int64_t *i = as_const_int(lt->b);
        if (v && i) {
            // !(v < i)
            expr_info.min_defined = true;
            expr_info.min = *i;
        }
        v = lt->b.as<Variable>();
        i = as_const_int(lt->a);
        if (v && i) {
            // !(i < v)
            expr_info.max_defined = true;
            expr_info.max = *i;
        }
    } else if (const LE *le = fact.as<LE>()) {
        v = le->a.as<Variable>();
        const int64_t *i = as_const_int(le->b);
        if (v && i) {
            // !(v <= i)
            expr_info.min_defined = true;
            expr_info.min = *i + 1;
        }
        v = le->b.as<Variable>();
        i = as_const_int(le->a);
        if (v && i) {
            // !(i <= v)
            expr_info.max_defined = true;
            expr_info.max = *i - 1;
        }
    } else if (const Or *o = fact.as<Or>()) {
        // Both must be false
        learn_false_helper(o->a, simplify, scoped);
        learn_false_helper(o->b, simplify, scoped);
    } else if (const Not *n = fact.as<Not>()) {
        learn_true_helper(n->a, simplify, scoped);
    }

    if (simplify->falsehoods.insert(fact).second && scoped) {
        scoped->falsehoods.push_back(fact);
    }

    if (v && (expr_info.min_defined ||
              expr_info.max_defined ||
              expr_info.alignment.modulus != 1)) {
        if (scoped) {
            scoped->learn_info(v, expr_info);
        } else {
            simplify->learn_info(v, expr_info);
        }
    }
}
}  // namespace

void Simplify::ScopedFact::learn_true(const Expr &fact) {
    learn_true_helper(fact, simplify, this);
}

void Simplify::learn_true(const Expr &fact) {
    learn_true_helper(fact, this, nullptr);
}

void Simplify::ScopedFact::learn_false(const Expr &fact) {
    learn_false_helper(fact, simplify, this);
}

void Simplify::learn_false(const Expr &fact) {
    learn_false_helper(fact, this, nullptr);
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

Expr simplify(const Expr &e, bool remove_dead_let_stmts,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(remove_dead_let_stmts, &bounds, &alignment).mutate(e, nullptr);
}

Stmt simplify(const Stmt &s, bool remove_dead_let_stmts,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(remove_dead_let_stmts, &bounds, &alignment).mutate(s);
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

        // A very common case where random probing fails to find a
        // counter-example is var != obscure_value. Just cull it here.
        if (const NE *ne = e.as<NE>()) {
            if (ne->a.as<Variable>() && is_const(ne->b)) {
                return false;
            }
        }

        if (get_compiler_logger()) {
            get_compiler_logger()->record_failed_to_prove(e, orig);
        }

        debug(1) << "Failed to prove, but could not find a counter-example:\n "
                 << e << "\n"
                 << "Original expression:\n"
                 << orig << "\n";
        return false;
    }

    return is_one(e);
}

}  // namespace Internal
}  // namespace Halide
