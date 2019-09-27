#include "StoreWith.h"

#include "CSE.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "PartitionLoops.h"
#include "Simplify.h"
#include "Simplify_Internal.h"
#include "Substitute.h"
#include "Solve.h"
#include "UniquifyVariableNames.h"
#include "Var.h"

#include <memory>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;
using std::set;

namespace {

// Make an auxiliary variable
Expr aux() {
    Var k(unique_name('k'));
    return k;
}

// One dimension of a polyhedral time vector
struct ClockDim {
    Expr t;
    ForType loop_type;
    ClockDim(const Expr &t, ForType loop_type) : t(t), loop_type(loop_type) {}
};


// A mostly-linear constraint. Represented as a linear combination
// of terms that sum to zero. The terms are usually Variables, but
// may be non-linear functions of Variables too.
struct Equality {

    // We keep the terms unique by storing them in a map sorted by
    // deep equality on the Exprs.
    std::map<Expr, int, IRDeepCompare> terms;

    // Track the number of terms that are just Variable
    // nodes. Useful for prioritizing work.
    int num_vars = 0;

    // Recursively extract all the linear terms from an Expr
    void find_terms(const Expr &e, int c) {
        if (c == 0) return;
        if (is_zero(e)) return;
        const Add *add = e.as<Add>();
        const Sub *sub = e.as<Sub>();
        const Mul *mul = e.as<Mul>();
        const int64_t *coeff = (mul ? as_const_int(mul->b) : nullptr);
        if (coeff && mul_would_overflow(64, c, *coeff)) {
            coeff = nullptr;
        }
        if (add) {
            find_terms(add->a, c);
            find_terms(add->b, c);
        } else if (sub) {
            find_terms(sub->a, c);
            find_terms(sub->b, -c);
        } else if (mul && coeff) {
            find_terms(mul->a, c * (int)(*coeff));
        } else if (mul) {
            // Apply distributive law to non-linear terms
            const Add *a_a = mul->a.as<Add>();
            const Sub *s_a = mul->a.as<Sub>();
            const Add *a_b = mul->b.as<Add>();
            const Sub *s_b = mul->b.as<Sub>();
            if (a_a) {
                find_terms(a_a->a * mul->b, c);
                find_terms(a_a->b * mul->b, c);
            } else if (s_a) {
                find_terms(s_a->a * mul->b, c);
                find_terms(s_a->b * mul->b, -c);
            } else if (a_b) {
                find_terms(mul->a * a_b->a, c);
                find_terms(mul->a * a_b->b, c);
            } else if (s_b) {
                find_terms(mul->a * s_b->a, c);
                find_terms(mul->a * s_b->b, -c);
            } else {
                add_term(e, c);
            }
        } else {
            add_term(e, c);
        }
    }

    void add_term(const Expr &e, int c) {
        auto p = terms.emplace(e, c);
        if (!p.second) {
            p.first->second += c;
            if (p.first->second == 0) {
                terms.erase(p.first);
                if (e.as<Variable>()) {
                    num_vars--;
                }
            }
        } else if (e.as<Variable>()) {
            num_vars++;
        }
    }


    Equality(const EQ *eq) {
        find_terms(eq->a, 1);
        find_terms(eq->b, -1);
    }
    Equality() = default;

    bool uses_var(const std::string &name) const {
        for (const auto &p : terms) {
            if (expr_uses_var(p.second, name)) {
                return true;
            }
        }
        return false;
    }

    // Convert this constraint back to a boolean Expr by putting
    // all the positive coefficients on one side and all the
    // negative coefficients on the other.
    Expr to_expr() const {
        Expr lhs, rhs;
        auto accum = [](Expr &a, Expr e, int c) {
            Expr t = e;
            if (c != 1) {
                t *= c;
            }
            if (a.defined()) {
                a += t;
            } else {
                a = t;
            }
        };
        for (auto p : terms) {
            if (p.second > 0) {
                accum(lhs, p.first, p.second);
            } else {
                accum(rhs, p.first, -p.second);
            }
        }
        if (!lhs.defined()) {
            lhs = 0;
        }
        if (!rhs.defined()) {
            rhs = 0;
        }
        return lhs == rhs;
    }
};

// A system of constraints. We're going to construct systems of
// constraints that have solutions that are all of the correctness
// violations (places where one Func clobbers a value in the
// shared buffer that the other Func still needs), and then try to
// prove that these systems have no solutions by finding a
// sequence of variable substitutions that turns one of the terms
// into the constant false.
struct System {

    // A bunch of equalities
    vector<Equality> equalities;

    // A shared simplifier instance, which slowly accumulates
    // knowledge about the bounds of different variables. We
    // prefer to encode constraints this way whenever we can,
    // because then they get automatically exploited whenever we
    // simplify something. Ultimately the way we prove things
    // infeasible is by slowly deducing bounds on the free
    // variables and then finding that one of our equalities above
    // can't be satisfied given the bounds we have learned.
    Simplify *simplifier;

    // The most-recently-performed substition, for debugging
    Expr most_recent_substitution;

    // An additional arbitrary term to place non-linear constraints
    Expr non_linear_term;

    // A heuristic for how close we are to finding infeasibility
    float c = 0;

    // unique IDs for each system for debugging and training a good heuristic
    static uint64_t id_counter;
    uint64_t id, parent_id;

    System(Simplify *s, Expr subs, int pid) :
        simplifier(s), most_recent_substitution(subs),
        id(id_counter++), parent_id(pid) {}

    void add_equality(const EQ *eq) {
        equalities.emplace_back(eq);
    }

    void add_non_linear_term(const Expr &e) {
        internal_assert(e.type().is_bool()) << e << "\n";
        if (is_zero(e) || !non_linear_term.defined()) {
            non_linear_term = e;
        } else {
            non_linear_term = non_linear_term && e;
        }
    }

    void add_term(const Expr &e) {
        const EQ *eq = e.as<EQ>();
        const LT *lt = e.as<LT>();
        const LE *le = e.as<LE>();
        if (eq && eq->a.type() == Int(32)) {
            add_equality(eq);
        } else if (const And *a = e.as<And>()) {
            add_term(a->a);
            add_term(a->b);
        } else if (const GT *gt = e.as<GT>()) {
            add_term(gt->b < gt->a);
        } else if (const GE *ge = e.as<GE>()) {
            add_term(ge->b <= ge->a);
        } else if (le && le->a.type() == Int(32)) {
            const Variable *va = le->a.as<Variable>();
            const Variable *vb = le->b.as<Variable>();
            if (const Min *min_b = le->b.as<Min>()) {
                // x <= min(y, z) -> x <= y && x <= z
                add_term(le->a <= min_b->a);
                add_term(le->a <= min_b->b);
            } else if (const Max *max_a = le->a.as<Max>()) {
                // max(x, y) <= z -> x <= z && y <= z
                add_term(max_a->a <= le->b);
                add_term(max_a->b <= le->b);
            } else if (is_const(le->a) && vb) {
                simplifier->learn_true(e);
            } else if (is_const(le->b) && va) {
                simplifier->learn_true(e);
            } else {
                Expr v = aux();
                simplifier->learn_true(-1 < v);
                add_term(le->a + v == le->b);
            }
        } else if (lt && lt->a.type() == Int(32)) {
            const Variable *va = lt->a.as<Variable>();
            const Variable *vb = lt->b.as<Variable>();
            if (const Min *min_b = lt->b.as<Min>()) {
                // x <= min(y, z) -> x <= y && x <= z
                add_term(lt->a < min_b->a);
                add_term(lt->a < min_b->b);
            } else if (const Max *max_a = lt->a.as<Max>()) {
                // max(x, y) <= z -> x <= z && y <= z
                add_term(max_a->a < lt->b);
                add_term(max_a->b < lt->b);
            } else if (is_const(lt->a) && vb) {
                simplifier->learn_true(e);
            } else if (is_const(lt->b) && va) {
                simplifier->learn_true(e);
            } else {
                Expr v = aux();
                simplifier->learn_true(0 < v);
                add_term(lt->a + v == lt->b);
            }
        } else if (const Let *l = e.as<Let>()) {
            // Treat lets as equality constraints in the new variable.
            if (l->value.type().is_bool()) {
                // We want to examine booleans more directly, so
                // substitute them in.
                add_term(substitute(l->name, l->value, l->body));
            } else {
                Expr eq = Variable::make(l->value.type(), l->name) == l->value;
                simplifier->learn_true(eq);
                add_term(eq);
                add_term(l->body);
            }
        } else if (is_one(e)) {
            // There's nothing we can learn from a tautology
        } else {
            // If all else fails, treat it as a non-linearity
            add_non_linear_term(e);
        }
    }

    void dump() const {
        if (most_recent_substitution.defined()) {
            debug(0) << "Substitution: " << most_recent_substitution << "\n";
        }
        for (auto &e : equalities) {
            debug(0) << " " << e.to_expr() << "\n";
        }
        if (non_linear_term.defined()) {
            debug(0) << " non-linear: " << non_linear_term << "\n";
        }
        const auto &info = simplifier->bounds_and_alignment_info;
        for (auto it = info.cbegin(); it != info.cend(); ++it){
            bool used = false;
            for (auto &e : equalities) {
                used |= expr_uses_var(e.to_expr(), it.name());
            }
            if (non_linear_term.defined()) {
                used |= expr_uses_var(non_linear_term, it.name());
            }
            if (!used) continue;
            if (it.value().min_defined && it.value().max_defined) {
                debug(0) << " " << it.value().min << " <= " << it.name()
                         << " <= " << it.value().max << "\n";
            } else if (it.value().min_defined) {
                debug(0) << " " << it.value().min << " <= " << it.name() << "\n";
            } else if (it.value().max_defined) {
                debug(0) << " " << it.name() << " <= " << it.value().max << "\n";
            }
        }
    }

    bool infeasible() const {
        // Check if any of the equalities or the non-linear term
        // are unsatisfiable or otherwise simplify to const false
        // given all the knowledge we have accumulated into the
        // simplifier instance.
        for (auto &e : equalities) {
            if (is_zero(simplifier->mutate(e.to_expr(), nullptr))) {
                return true;
            }
        }
        if (non_linear_term.defined() && is_zero(simplifier->mutate(non_linear_term, nullptr))) {
            return true;
        }
        return false;
    }

    void finalize() {
        // We'll preferentially find substitutions from the
        // earlier equations, so sort the system, putting low
        // term-count expressions with lots of naked vars first
        std::stable_sort(equalities.begin(), equalities.end(),
                         [](const Equality &a, const Equality &b) {
                             if (a.terms.size() < b.terms.size()) return true;
                             if (a.terms.size() > b.terms.size()) return false;
                             return a.num_vars < b.num_vars;
                         });
        compute_complexity();
    }

    // Compute our heuristic for which systems are closest to infeasible
    void compute_complexity() {
        std::map<std::string, int> inequalities;
        int non_linear_terms = 0, num_terms = 0;
        std::set<std::string> wild_constant_terms;
        for (auto &e : equalities) {
            for (auto &p : e.terms) {
                Simplify::ExprInfo info;
                simplifier->mutate(p.first, &info);
                if (const Variable *var = p.first.as<Variable>()) {
                    inequalities[var->name] = (int)info.max_defined + (int)info.min_defined;
                    if (var->name[0] == 'c') {
                        wild_constant_terms.insert(var->name);
                    }
                } else if (!is_const(p.first)) {
                    non_linear_terms++;
                }
                num_terms++;
            }
        }
        int unconstrained_vars = 0;
        int semi_constrained_vars = 0;
        int totally_constrained_vars = 0;
        int num_constraints = (int)equalities.size() + (int)non_linear_term.defined();
        for (const auto &p : inequalities) {
            if (p.second == 0) {
                unconstrained_vars++;
            } else if (p.second == 1) {
                semi_constrained_vars++;
            } else {
                totally_constrained_vars++;
            }
        }
        // debug(0) << "FEATURES " << id << " " << parent_id << " " << non_linear_terms << " " << unconstrained_vars << " " << semi_constrained_vars << " " << totally_constrained_vars << " " << num_terms << " " << num_constraints << "\n";
        int terms[] = {non_linear_terms, unconstrained_vars, semi_constrained_vars,
                       totally_constrained_vars, num_terms, num_constraints};
        // Use a linear combination of these features to decide
        // which stats are the most promising to explore. Trained
        // by tracking which states lead to success in the
        // store_with test and minimizing cross-entropy loss on a
        // linear classifier.
        float coeffs[] = {0.0006f,  0.3839f,  0.1992f,  0.0388f, -0.0215f, -0.4192f};
        c = 0.0f;
        for (int i = 0; i < 6; i++) {
            c += terms[i] * coeffs[i];
        }
        // HACK
        c -= (int)(wild_constant_terms.size());
    }

    float complexity() const {
        return c;
    }

    Expr exact_divide(const Expr &e, const std::string &v) {
        if (const Variable *var = e.as<Variable>()) {
            if (var->name == v) {
                return make_one(e.type());
            } else {
                return Expr();
            }
        } else if (const Mul *mul = e.as<Mul>()) {
            Expr a = exact_divide(mul->a, v);
            if (a.defined()) {
                return a * mul->b;
            }
            Expr b = exact_divide(mul->b, v);
            if (b.defined()) {
                return mul->a * b;
            }
        }
        return Expr();
    }

    void make_children(std::deque<std::unique_ptr<System>> &result) {

        size_t old_size = result.size();

        // Eliminate divs and mods by introducing new variables
        for (int i = 0; i < (int)equalities.size(); i++) {
            Expr lhs, rhs;
            for (auto &p : equalities[i].terms) {
                const Mod *mod = p.first.as<Mod>();
                const Div *div = p.first.as<Div>();
                const Mul *mul = p.first.as<Mul>();
                if (mod) {
                    lhs = mod->a;
                    rhs = mod->b;
                } else if (div) {
                    lhs = div->a;
                    rhs = div->b;
                } else if (mul) {
                    lhs = mul->a;
                    rhs = mul->b;
                }

                if (is_const(rhs)) {
                    internal_assert(mul == nullptr);
                    break;
                } else if (const Variable *v = rhs.as<Variable>()) {
                    // HACK for constant vars
                    if (mul) {
                        div = mul->a.as<Div>();
                        if (div) {
                            lhs = div->a;
                        }
                    }
                    if (starts_with(v->name, "c") &&
                        (!mul || (div && equal(div->b, mul->b))) &&
                        is_one(simplifier->mutate(rhs > 0, nullptr))) {
                        break;
                    }
                }

                lhs = rhs = Expr();
            }
            if (lhs.defined()) {
                Expr k1 = aux(), k2 = aux();
                Expr replacement = simplifier->mutate(k1 + k2 * rhs, nullptr);
                auto subs = [&](Expr e) {
                    e = substitute(lhs % rhs, k1, e);
                    e = substitute(lhs / rhs, k2, e);
                    return simplifier->mutate(e, nullptr);
                };
                std::unique_ptr<System> new_system(new System(simplifier, lhs == rhs, id));
                if (non_linear_term.defined()) {
                    new_system->add_term(subs(non_linear_term));
                }
                for (int j = 0; j < (int)equalities.size(); j++) {
                    new_system->add_term(subs(equalities[j].to_expr()));
                }
                new_system->add_term(lhs == replacement);
                simplifier->learn_true(-1 < k1);
                if (is_const(rhs)) {
                    simplifier->learn_true(k1 < rhs);
                } else {
                    // TODO: only if we know RHS is positive.
                    new_system->add_term(k1 < rhs);
                }
                new_system->finalize();
                result.emplace_back(std::move(new_system));
                return;
            }
        }

        // Divide through by common factors
        for (int i = 0; i < (int)equalities.size(); i++) {
            std::map<std::string, int> factors;
            for (auto &p : equalities[i].terms) {
                vector<Expr> pending { p.first };
                while (!pending.empty()) {
                    Expr next = pending.back();
                    pending.pop_back();
                    if (const Mul *m = next.as<Mul>()) {
                        pending.push_back(m->a);
                        pending.push_back(m->b);
                    } else if (const Variable *v = next.as<Variable>()) {
                        factors[v->name]++;
                    }
                }
            }
            for (const auto &f : factors) {
                /*
                if (f.second == 1) {
                    // Not a repeated factor
                    continue;
                }
                */
                debug(0) << "Attempting to eliminate: " << f.first << "\n";
                Expr factor_expr = Variable::make(Int(32), f.first);
                Expr terms_with_factor = 0;
                Expr terms_without_factor = 0;
                for (auto &p : equalities[i].terms) {
                    Expr e = exact_divide(p.first, f.first);
                    if (e.defined()) {
                        terms_with_factor += e * p.second;
                    } else {
                        terms_without_factor += p.first * p.second;
                    }
                }
                terms_with_factor = simplifier->mutate(terms_with_factor, nullptr);
                debug(0) << "With/without: " << terms_with_factor << ", " << terms_without_factor << "\n";
                if (is_zero(simplifier->mutate(terms_without_factor == 0, nullptr))) {
                    // If the sum of the terms that do not reference
                    // the factor can't be zero, then the factor can't
                    // be zero either, so it's safe to divide
                    // by. Furthermore, this implies that the terms
                    // with the factor can't sum to zero.
                    std::unique_ptr<System> new_system(new System(simplifier, Expr(), id));
                    if (non_linear_term.defined()) {
                        new_system->add_term(non_linear_term);
                    }
                    for (int j = 0; j < (int)equalities.size(); j++) {
                        if (i != j) {
                            new_system->add_term(equalities[j].to_expr());
                        }
                    }
                    new_system->add_term(terms_with_factor != 0);
                    new_system->finalize();
                    result.emplace_back(std::move(new_system));
                }
            }
        }

        // Replace repeated non-linear terms with new variables
        std::map<Expr, int, IRDeepCompare> nonlinear_terms;
        for (int i = 0; i < (int)equalities.size(); i++) {
            for (auto &p : equalities[i].terms) {
                if (!p.first.as<Variable>() && !is_const(p.first)) {
                    nonlinear_terms[p.first]++;
                }
            }
        }

        for (auto p : nonlinear_terms) {
            if (p.second > 1) {
                // It's a repeated non-linearity. Replace it with an opaque variable.
                Var t(unique_name('n'));

                debug(0) << "Repeated non-linear term: " << t << " == " << p.first << "\n";

                auto subs = [&](Expr e) {
                    e = substitute(p.first, t, e);
                    return e;
                };

                std::unique_ptr<System> new_system(new System(simplifier, t == p.first, id));
                if (non_linear_term.defined()) {
                    new_system->add_term(subs(non_linear_term));
                }
                for (int j = 0; j < (int)equalities.size(); j++) {
                    new_system->add_term(subs(equalities[j].to_expr()));
                }

                // Carry over any bounds on the non-linear term to a bound on the new variable.
                Simplify::ExprInfo bounds;
                simplifier->mutate(p.first, &bounds);
                if (bounds.min_defined) {
                    simplifier->learn_true(t >= (int)bounds.min);
                }
                if (bounds.max_defined) {
                    simplifier->learn_true(t <= (int)bounds.max);
                }

                new_system->finalize();
                result.emplace_back(std::move(new_system));
                return;
            }
        }

        // Which equations should we mine for
        // substitutions. Initially all of them are promising.
        vector<bool> interesting(equalities.size(), true);

        // A list of all variables we could potentially eliminate
        set<string> eliminable_vars;
        for (int i = 0; i < (int)equalities.size(); i++) {
            for (const auto &p : equalities[i].terms) {
                const Variable *var = p.first.as<Variable>();
                // HACK: forbid use of constant wildcards.
                // if (var && starts_with(var->name, "c")) continue;
                if (var && (p.second == 1 || p.second == -1)) {
                    eliminable_vars.insert(var->name);
                }
            }
        }

        if (!equalities.empty() && eliminable_vars.empty()) {
            debug(0) << "NO ELIMINABLE VARS:\n";
            dump();
        }

        /*
          for (const auto &v : eliminable_vars) {
          debug(0) << "Eliminable var: " << v << "\n";
          }
        */

        // A mapping from eliminable variables to the equalities that reference them.
        map<string, vector<int>> eqs_that_reference_var;
        for (int i = 0; i < (int)equalities.size(); i++) {
            Expr eq = equalities[i].to_expr();
            for (const auto &v : eliminable_vars) {
                if (expr_uses_var(eq, v)) {
                    eqs_that_reference_var[v].push_back(i);
                }
            }
        }

        // The set of pairs of equations that share a common
        // eliminable variable
        set<pair<int, int>> has_common_variable;
        for (auto p : eqs_that_reference_var) {
            for (int i : p.second) {
                for (int j : p.second) {
                    has_common_variable.insert({i, j});
                }
            }
        }

        // Eliminate a variable
        for (int i = 0; i < (int)equalities.size(); i++) {
            if (equalities[i].num_vars == 0) {
                // We're not going to be able to find an
                // elimination from something with no naked vars.
                continue;
            }

            if (!interesting[i]) {
                // We've decided that this equation isn't one we want to mine.
                continue;
            }

            for (const auto &p : equalities[i].terms) {
                const Variable *var = p.first.as<Variable>();

                if (var) {

                    Expr rhs = 0, rhs_remainder = 0;
                    for (const auto &p2 : equalities[i].terms) {
                        // Every term on the RHS has to be either
                        // divisible by p.first, or in total
                        // bounded by p.first
                        if (p2.first.same_as(p.first)) {
                            // This is the LHS
                        } else if (p2.second % p.second == 0) {
                            rhs -= p2.first * (p2.second / p.second);
                        } else {
                            rhs_remainder -= p2.first * p2.second;
                        }
                    }

                    // We have:
                    // p.first * p.second == rhs * p.second + rhs_remainder

                    Simplify::ExprInfo remainder_bounds;
                    rhs_remainder = simplifier->mutate(rhs_remainder, &remainder_bounds);
                    rhs = simplifier->mutate(rhs, nullptr);

                    if (remainder_bounds.max_defined &&
                        remainder_bounds.max < std::abs(p.second) &&
                        remainder_bounds.min_defined &&
                        remainder_bounds.min > -std::abs(p.second)) {
                        // We have:
                        // p.first == rhs && 0 == rhs_remainder
                    } else {
                        // We don't have a substitution
                        continue;
                    }

                    if (expr_uses_var(rhs, var->name)) {
                        // Didn't successfully eliminate it - it
                        // still occurs inside a non-linearity on
                        // the right.
                        continue;
                    }

                    // Tell the simplifier that LHS == RHS. This
                    // may give it tighter bounds for the LHS
                    // variable based on what is currently known
                    // about the bounds of the RHS. This is the
                    // primary mechanism by which the simplifier
                    // instance learns things - not from the
                    // substitutions we actually perform, but from
                    // every potential substitution. Avoid telling
                    // the simplifier that x == x.
                    if (!equal(p.first, rhs)) {
                        simplifier->learn_true(p.first == rhs);
                    }

                    //debug(0) << "Attempting elimination of " << var->name << "\n";

                    // We have a candidate for elimination. Rule
                    // out searching all equalities that don't
                    // share a common variable with this one,
                    // because we equally could have done any
                    // substitutions resulting from those first
                    // without affecting this substitution, and
                    // doing things in a canonical order avoids
                    // exploring the same states an exponential
                    // number of times.
                    for (int j = 0; j < (int)equalities.size(); j++) {
                        if (interesting[j]) {
                            interesting[j] = has_common_variable.count({i, j});
                        }
                    }

                    // If the RHS is just a constant or variable then
                    // we'll just greedily perform this elimination -
                    // there's no reason to need to backtrack on it,
                    // so nuke all other candidate children. There
                    // typically won't be any because x == y will sort
                    // to the front of the list of equalities.
                    bool greedy = false;
                    if (rhs.as<Variable>() || is_const(rhs)) {
                        greedy = true;
                        result.clear();
                    }

                    auto subs = [&](Expr e) {
                        e = substitute(var->name, rhs, e);
                        e = simplifier->mutate(e, nullptr);
                        return e;
                    };

                    // Make a child system with the substitution
                    // performed and this equality eliminated.
                    std::unique_ptr<System> new_system(new System(simplifier, p.first == rhs, id));
                    if (non_linear_term.defined()) {
                        new_system->add_term(subs(non_linear_term));
                    }
                    for (int j = 0; j < (int)equalities.size(); j++) {
                        if (i == j) {
                            // The equation we exploited to get
                            // the substitution gets reduced
                            // modulo the coefficient.
                            new_system->add_term(simplifier->mutate(rhs_remainder == 0, nullptr));
                            continue;
                        }
                        // In the other equations, we replace the variable with the right-hand-side
                        new_system->add_term(subs(equalities[j].to_expr()));
                    }
                    new_system->finalize();
                    result.emplace_back(std::move(new_system));

                    // No point considering further candidates if
                    // we're just doing a variable1 = variable2
                    // substitution.
                    if (greedy) {
                        return;
                    }
                }
            }
        }

        if (result.size() == old_size && !equalities.empty()) {
            debug(0) << "NO CHILDREN:\n";
            dump();
        }
    }
};

uint64_t System::id_counter = 0;

// Attempt to disprove a boolean expr by constructing a constraint
// system and performing a backtracking search over substitutions
// using beam search.
bool can_disprove(Expr e, int beam_size, std::set<Expr, IRDeepCompare> *implications = nullptr) {
    e = common_subexpression_elimination(simplify(remove_likelies(e)));

    debug(0) << "*** Attempting disproof " << e << "\n";

    if (is_zero(e)) {
        // The simplifier was capable of doing the disproof by itself
        // using peephole rules alone. No need to continue.
        return true;
    }

    // Make a simplifier instance to hold all of our shared
    // knowledge, and construct the initial system of constraints
    // from the expression.
    Simplify simplifier(true, nullptr, nullptr);
    std::unique_ptr<System> system(new System(&simplifier, Expr(), 0));
    system->add_term(e);
    system->finalize();

    class FilterImplications : public IRVisitor {
        using IRVisitor::visit;

        void visit(const Variable *op) {
            // TODO: using var name prefixes here is a total hack
            if (starts_with(op->name, "c")) {
                return;
            } else if (starts_with(op->name, "k")) {
                if (simplifier->bounds_and_alignment_info.contains(op->name)) {
                    auto info = simplifier->bounds_and_alignment_info.get(op->name);
                    if (info.min_defined || info.max_defined) {
                        return;
                    }
                }
            }
            useful = false;
        }

    public:
        Simplify *simplifier;
        bool useful = true;
        FilterImplications(Simplify *s) : simplifier(s) {}
    };

    std::set<Expr, IRDeepCompare> local_implications;

    auto consider_implication = [&](const Expr &e) {
        FilterImplications f(&simplifier);
        e.accept(&f);
        if (f.useful) {
            local_implications.insert(e);
        }
    };

    // Beam search time.
    std::deque<std::unique_ptr<System>> beam;
    beam.emplace_back(std::move(system));
    while (!beam.empty()) {
        // Take the best thing
        std::unique_ptr<System> next = std::move(beam.front());
        beam.pop_front();

        if (implications) {
            for (const auto &eq : next->equalities) {
                consider_implication(eq.to_expr());
            }
            if (next->non_linear_term.defined()) {
                consider_implication(next->non_linear_term);
            }
        }


          debug(0) << "Top of beam: " << next->complexity() << "\n";
          next->dump();

        if (next->infeasible()) {
            // We found that the initial constraint system
            // eventually implied a falsehood, so we successfully
            // disproved the original expression.
            if (implications) {
                implications->insert(const_false());
            }
            return true;
        }

        // Generate children
        next->make_children(beam);

        // Take the top beam_size results by sorting all the
        // children and then popping off the end. Not the most
        // efficient way to do it, but this is not the long pole
        // here.
        std::stable_sort(beam.begin(), beam.end(),
                         [&](const std::unique_ptr<System> &a,
                             const std::unique_ptr<System> &b) {
                             return a->complexity() < b->complexity();
                         });
        while ((int)beam.size() > beam_size) {
            beam.pop_back();
        }
    }

    if (implications) {
        Scope<Interval> scope;
        map<string, Expr> subs;
        const auto &info = simplifier.bounds_and_alignment_info;
        for (auto it = info.cbegin(); it != info.cend(); ++it){
            if (starts_with(it.name(), "c")) {
                Var v(it.name());
                if (it.value().min_defined) {
                    consider_implication((int)it.value().min <= v);
                }
                if (it.value().max_defined) {
                    consider_implication(v <= (int)it.value().max);
                }
                if (it.value().min_defined || it.value().max_defined) {
                    // We need a way to communicate the bounds of this to
                    // the bounds machinery below without having the
                    // bounds machinery eliminate this variable. Wrap it
                    // in a clamp.
                    Expr replacement = v;
                    if (it.value().min_defined) {
                        // TODO: assert min/max representable as int32
                        replacement = max(replacement, (int)it.value().min);
                    }
                    if (it.value().max_defined) {
                        replacement = min(replacement, (int)it.value().max);
                    }
                    subs[it.name()] = replacement;
                }
            } else {
                Interval i = Interval::everything();
                if (it.value().min_defined) {
                    i.min = (int)it.value().min;
                }
                if (it.value().max_defined) {
                    i.max = (int)it.value().max;
                }
                debug(0) << it.name() << ": " << i.min << " " << i.max << "\n";
                scope.push(it.name(), i);
            }
        }

        // Now eliminate all the k's
        for (Expr m : local_implications) {
            m = substitute(subs, m);
            debug(0) << m << " -> ";
            if (const EQ *eq = m.as<EQ>()) {
                Expr a = eq->a, b = eq->b;
                Interval ia = bounds_of_expr_in_scope(a, scope);
                Interval ib = bounds_of_expr_in_scope(b, scope);
                if (ia.is_single_point() && ib.is_single_point()) {
                    m = (ia.min == ib.min);
                } else {
                    m = const_true();
                    if (ia.has_upper_bound() && ib.has_lower_bound()) {
                        // Equality implies their ranges must overlap
                        m = (ia.max >= ib.min);
                    }
                    if (ia.has_lower_bound() && ib.has_upper_bound()) {
                        m = m && (ia.min <= ib.max);
                    }
                }
            }
            debug(0) << m << "\n";
            implications->insert(m);
        }
    }

    return false;
}

// A class representing a use of a buffer at some symbolic time
struct Use {
    // Lexicographically-ordered time vector, ala polyhedral optimization
    vector<ClockDim> time;

    // The site in the buffer accessed. Mostly a function of the
    // variables in the time vector. May or may not be
    // piecewise-quasi-affine, but often is.
    vector<Expr> site;

    // A bunch of constraints on the variables referenced, coming from
    // if statements and loop bounds that surround the use. The
    // constrains are mostly linear, but can include arbirtary
    // non-linearities too from RDom::where clauses.
    Expr predicate;

    // The buffer accessed
    string name;

    // The source of the use, for debugging and error messages.
    Stmt original_store;
    Expr original_load;

    Use(const vector<ClockDim> &t,
        const vector<Expr> &s,
        const vector<Expr> &p,
        const string &n,
        const vector<string> &loops,
        const vector<pair<string, Expr>> &lets,
        Stmt store, Expr load) :
        time(t), site(s), predicate(const_true()), name(n),
        original_store(store), original_load(load) {

        // Wrap the lets around the site.
        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            for (auto &e : site) {
                if (expr_uses_var(e, it->first)) {
                    e = Let::make(it->first, it->second, e);
                }
            }
        }

        // Make any variables unique to this use so that we can talk
        // about different uses at distinct values of the loop
        // variables.
        map<string, Expr> renaming;
        for (const auto &v : loops) {
            string new_name = unique_name('t');
            Expr new_var = Variable::make(Int(32), new_name);
            renaming[v] = new_var;
        }

        for (auto &e : site) {
            e = substitute(renaming, e);
        }

        for (auto &e : time) {
            e.t = substitute(renaming, e.t);
        }

        // Synchronous parallel loops like vector or gpu warp lanes
        // are implicitly innermost. I.e. sequence points that look
        // like they're within the loop are actually outside the
        // loop. Handle this by bubbling those dimensions to the end
        // of the time vector.
        auto end = time.end();
        for (auto it = time.begin(); it != end;) {
            if (is_parallel(it->loop_type) && !is_unordered_parallel(it->loop_type)) {
                std::rotate(it, it + 1, time.end());
                end--;
            } else {
                it++;
            }
        }

        // Collapse the predicate vector down to a single Expr so that
        // we can wrap lets around it once.
        for (const auto &e : p) {
            predicate = predicate && e;
        }
        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            if (expr_uses_var(predicate, it->first)) {
                predicate = Let::make(it->first, it->second, predicate);
            }
        }

        predicate = substitute(renaming, predicate);
    }

    Use() = default;

    // Return a boolean in DNF form encoding whether one time vector
    // could be <= another.
    vector<Expr> may_happen_before(const Use &other, size_t start = 0) const {
        vector<Expr> result;

        // Lexicographic order starting at the given index

        if (start == time.size()) {
            // The empty time vector is <= all others.
            result.push_back(const_true());
            return result;
        }

        if (start == other.time.size()) {
            // The other string is empty and we're not, so false. In
            // DNF form this is encoded as any empty list of clauses.
            return result;
        }

        if (is_const(other.time[start].t) &&
            is_const(time[start].t)) {
            // Early out if the ordering can be resolved statically on this dimension
            if (can_prove(time[start].t < other.time[start].t)) {
                result.push_back(const_true());
                return result;
            }
            if (can_prove(time[start].t > other.time[start].t)) {
                return result;
            }
        }

        // Get the result if there's a tie on this dimension. It's a
        // vector representing a DNF form boolean Expr.
        result = may_happen_before(other, start + 1);

        // AND each clause with the statement that there is indeed a tie
        for (auto &e : result) {
            // Substitute just to simplify the expression a little.
            e = substitute(time[start].t, other.time[start].t, e);
            e = other.time[start].t == time[start].t && e;
        }

        internal_assert(other.time[start].loop_type == time[start].loop_type);

        // Then OR in the case where there isn't a tie and this may
        // happen before other, by adding a clause to the vector.
        if (is_parallel(time[start].loop_type)) {
            // If we're looking at a parallel loop, any distinct loop
            // iteration may have already run, or may be running at
            // the same time. Avoid encoding using != so that we most
            // often get things in the form of a list of ILPs. This
            // expands the number of proofs to perform by a factor of
            // two for each nested parallel loop, but parallel loop
            // nestings are seldom more than 2 deep (parallel,
            // vectorize).
            result.push_back(other.time[start].t < time[start].t);
            result.push_back(other.time[start].t > time[start].t);
        } else {
            // If we're looking at a serial loop, any earlier loop
            // iteration has already happened.
            result.push_back(other.time[start].t > time[start].t);
        }

        return result;
    }

    template<typename OutStream>
    void dump(OutStream &s) const {
        if (original_store.defined()) {
            s << "store of " << name << ":\n"
              << original_store;
        } else if (original_load.defined()) {
            s << "load of " << name << ":\n"
              << original_load << "\n";
        }
        s << "Time vector: ";
        for (const auto &e : time) {
            if (is_const(e.t)) {
                s << e.t << ", ";
            } else {
                s << e.t << ' ' << e.loop_type << ", ";
            }
        }
        s << "\n";
        s << "Site: ";
        for (const auto &e : site) {
            s << e << " ";
        }
        s << "\n";
        s << "Predicate: " << predicate << "\n";
    }

    // Try to prove that for every site in a shared buffer, this use
    // always happens strictly before another.
    bool safely_before(const Use &other, int beam_size) const {

        // We'll do a proof by contradiction. Assume that there is a
        // site where the other use happens before or at the same time
        // as this one, and derive a contradiction using the beam
        // search code above.

        // We'll generate the boolean expression in DNF form, and
        // attempt to disprove every single clause.
        Expr same_site = const_true();
        for (size_t i = 0; i < site.size(); i++) {
            same_site = same_site && site[i] == other.site[i];
        }
        Expr may_assume = simplify(same_site && predicate && other.predicate);

        //debug(0) << "Same site: " << same_site << "\n";
        //debug(0) << "Predicate: " << predicate << "\n";
        //debug(0) << "Other predicate: " << other.predicate << "\n";

        // First try to cheaply prove this term false. If we can, then
        // these two uses never alias and we don't need to worry about
        // anything temporal (e.g. one use writes to even rows and the
        // other use writes to odd rows).

        // We don't use can_disprove, because it's expensive when it
        // fails, and this is supposed to be an early-out. We've
        // already applied the simplifier so let's just check if the
        // simplifier already successfully disproved it.

        if (is_zero(may_assume)) {
            return true;
        }

        // Now consider temporal constraints too.
        auto before = other.may_happen_before(*this);

        // Try to disprove each clause in turn.
        for (const auto &e : before) {
            if (!can_disprove(e && may_assume, beam_size)) {
                // We failed. The simplifier does fancy logging when
                // it fails to prove things that are probably actually
                // true, so trigger the simplifier again.
                return can_prove(!(e && may_assume));
            }
        }

        return true;
    }

    bool is_store() const {
        return original_store.defined();
    }

    bool is_load() const {
        return original_load.defined();
    }
};

// Scrape all uses of a given buffer from a Stmt.
std::vector<Use> get_times_of_all_uses(const Stmt &s, string buf, const map<string, Function> &env) {
    class PolyhedralClock : public IRVisitor {
        using IRVisitor::visit;

        vector<ClockDim> clock;
        vector<Expr> predicate;
        vector<string> loops;
        vector<pair<string, Expr>> lets;

        const string &buf;

        void visit(const Block *op) override {
            int i = 0;
            clock.emplace_back(i, ForType::Serial);
            Stmt rest;
            do {
                op->first.accept(this);
                rest = op->rest;
                clock.back().t = ++i;

            } while ((op = rest.as<Block>()));
            rest.accept(this);
            clock.pop_back();
        }

        void visit(const For *op) override {
            Expr loop_var = Variable::make(Int(32), op->name);
            if (!is_const(op->min)) {
                // Rebase at zero to get a variable with more constant bounds
                Expr v = aux();
                predicate.push_back(v == loop_var - op->min && v >= 0 && v < op->extent);
            } else {
                predicate.push_back(loop_var >= op->min && loop_var < op->min + op->extent);
            }
            loops.push_back(op->name);
            clock.emplace_back(Variable::make(Int(32), op->name), op->for_type);
            op->body.accept(this);
            clock.pop_back();
            loops.pop_back();
            predicate.pop_back();
        }

        void visit(const IfThenElse *op) override {
            if (!is_pure(op->condition)) {
                IRVisitor::visit(op);
            } else {
                op->condition.accept(this);

                predicate.push_back(op->condition);
                op->then_case.accept(this);
                predicate.pop_back();

                if (op->else_case.defined()) {
                    predicate.push_back(!op->condition);
                    op->else_case.accept(this);
                    predicate.pop_back();
                }
            }
        }

        void visit(const Select *op) override {
            op->condition.accept(this);

            predicate.push_back(op->condition);
            op->true_value.accept(this);
            predicate.pop_back();

            predicate.push_back(!op->condition);
            op->false_value.accept(this);
            predicate.pop_back();
        }

        void found_use(const vector<Expr> &site, const string &name, Stmt store, Expr load) {
            uses.emplace_back(clock, site, predicate, name, loops, lets, store, load);
        }

        void visit(const Let *op) override {
            op->value.accept(this);
            lets.emplace_back(op->name, op->value);
            op->body.accept(this);
            lets.pop_back();
        }

        void visit(const LetStmt *op) override {
            op->value.accept(this);
            lets.emplace_back(op->name, op->value);
            op->body.accept(this);
            lets.pop_back();
        }

        void visit(const Provide *op) override {
            {
                bool rhs_undef = true;
                for (const auto &e : op->values) {
                    rhs_undef &= is_undef(e);
                }
                if (rhs_undef) {
                    return;
                }
            }

            // The RHS is evaluated before the store happens
            clock.emplace_back(0, ForType::Serial);
            IRVisitor::visit(op);
            clock.back().t = 1;
            if (op->name == buf) {
                found_use(op->args, op->name, op, Expr());
            }
            clock.pop_back();
        }

        void visit(const Call *op) override {
            IRVisitor::visit(op);
            if (op->name == buf) {
                found_use(op->args, op->name, Stmt(), op);
            }
        }

        void visit(const Realize *op) override {
            auto it = env.find(op->name);
            if (it != env.end() && it->second.schedule().async()) {

                // Realizations of async things become fork nodes
                // later in lowering. Everything inside the
                // realization inside the produce node happens in one
                // thread, and everything inside the realization
                // outside the produce node happens in another. We'll
                // conservatively pretend all of the events happen in
                // both, and treat this as a parallel loop of size 2.
                Expr v = Variable::make(Int(32), unique_name(op->name + ".fork"));
                predicate.push_back(v >= 0 && v <= 1);
                clock.emplace_back(v, ForType::Parallel);
                IRVisitor::visit(op);
                clock.pop_back();
                predicate.pop_back();
            } else {
                IRVisitor::visit(op);
            }
        }

        const std::map<string, Function> &env;
    public:
        vector<Use> uses;
        PolyhedralClock(std::string &b, const std::map<string, Function> &env) : buf(b), env(env) {}
    } clock(buf, env);

    s.accept(&clock);

    /*
    for (const auto &u : clock.uses) {
        u.dump();
    }
    */

    return clock.uses;
}

}  // namespace

class BreakIntoConvexPieces : public IRMutator {
    using IRMutator::visit;

    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;

    enum class VarSign {
        Positive,
        NonNegative,
        NonPositive,
        Negative
    };

    Scope<VarSign> var_sign;

    int indent_ = 0;
    void indent() {
        for (int i = 0; i < indent_; i++) {
            debug(0) << ' ';
        }
    }

    Expr visit(const Add *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b), op->type, op->type);

        if (rewrite(min(x, y) + z, min(x + z, y + z)) ||
            rewrite(z + min(x, y), min(z + x, z + y)) ||
            rewrite(max(x, y) + z, max(x + z, y + z)) ||
            rewrite(z + max(x, y), max(z + x, z + y)) ||
            rewrite(select(x, y, z) + w, select(x, y + w, z + w)) ||
            rewrite(w + select(x, y, z), select(x, w + y, w + z))) {
            return mutate(rewrite.result);
        } else {
            return a + b;
        }
    }

    Expr visit(const Sub *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b), op->type, op->type);

        if (rewrite(min(x, y) - z, min(x - z, y - z)) ||
            rewrite(z - min(x, y), max(z - x, z - y)) ||
            rewrite(max(x, y) - z, max(x - z, y - z)) ||
            rewrite(z - max(x, y), min(z - x, z - y)) ||
            rewrite(select(x, y, z) - w, select(x, y - w, z - w)) ||
            rewrite(w - select(x, y, z), select(x, w - y, w - z)) ||
            false) {
            return mutate(rewrite.result);
        } else {
            return a - b;
        }
    }

    Expr visit(const Mul *op) override {
        debug(0) << "Mul\n";
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::mul(a, b), op->type, op->type);
        const Variable *var_b = b.as<Variable>();

        if (false && var_b && !var_sign.contains(var_b->name)) {
            indent();
            debug(0) << "Sign of " << b << " is unknown. Expanding into cases...\n";
            Expr zero = make_zero(b.type());
            // Break it into two cases with known sign
            Expr prod = a * b;
            return mutate(select(zero < b, prod,
                                 b < zero, prod,
                                 zero));
        } else if (rewrite(min(x, y) * z, select(x < y, x * z, y * z)) ||
                   rewrite(z * min(x, y), select(x < y, z * x, z * y)) ||
                   rewrite(max(x, y) * z, select(y < x, x * z, y * z)) ||
                   rewrite(z * max(x, y), select(y < x, z * x, z * y)) ||
                   rewrite(select(x, y, z) * w, select(x, y * w, z * w)) ||
                   rewrite(w * select(x, y, z), select(x, w * y, w * z)) ||
                   rewrite((x + y) * z, x * z + y * z) ||
                   rewrite(z * (x + y), z * x + z * y) ||
                   false) {
            return mutate(rewrite.result);
        } else {
            return a * b;
        }
    }

    Expr visit(const Div *op) override {
        debug(0) << "Div\n";
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::div(a, b), op->type, op->type);
        const Variable *var_b = b.as<Variable>();

        if (false && var_b && !var_sign.contains(var_b->name)) {
            indent();
            debug(0) << "Sign of " << b << " is unknown. Expanding into cases...\n";
            Expr zero = make_zero(b.type());
            // Break it into two cases with known sign
            Expr ratio = a / b;
            return mutate(select(zero < b, ratio,
                                 b < zero, ratio,
                                 zero)); // This case is in fact unreachable
        } else if (rewrite(min(x, y) / z, select(x < y, x / z, y / z)) ||
            rewrite(z / min(x, y), select(x < y, z / x, z / y)) ||
            rewrite(max(x, y) / z, select(y < x, x / z, y / z)) ||
            rewrite(z / max(x, y), select(y < x, z / x, z / y)) ||
            rewrite(select(x, y, z) / w, select(x, y / w, z / w)) ||
            rewrite(w / select(x, y, z), select(x, w / y, w / z))) {
            return mutate(rewrite.result);
        } else {
            return a / b;
        }
    }

    Expr visit(const LT *op) override {
        debug(0) << "LT\n";
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::lt(a, b), op->type, a.type());

        if (rewrite(min(x, y) < z, (x < z) || (y < z)) ||
            rewrite(z < min(x, y), (z < x) && (z < y)) ||
            rewrite(max(x, y) < z, (x < z) && (y < z)) ||
            rewrite(z < max(x, y), (z < x) || (z < y)) ||
            //            rewrite(select(x, y, z) < w, (x && (y < w)) || (!x && (z < w))) ||
            //rewrite(w < select(x, y, z), (x && (w < y)) || (!x && (w < z)))) {
            rewrite(select(x, y, z) < w, select(x, y < w, z < w)) ||
            rewrite(w < select(x, y, z), select(x, w < y, w < z)) ||
            false) {
            return mutate(rewrite.result);
        } else {
            return a < b;
        }
    }

    Expr visit(const LE *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::le(a, b), op->type, a.type());

        if (rewrite(min(x, y) <= z, (x <= z) || (y <= z)) ||
            rewrite(z <= min(x, y), (z <= x) && (z <= y)) ||
            rewrite(max(x, y) <= z, (x <= z) && (y <= z)) ||
            rewrite(z <= max(x, y), (z <= x) || (z <= y)) ||
            //            rewrite(select(x, y, z) <= w, x && (y <= w) || !x && (z <= w)) ||
            //            rewrite(w <= select(x, y, z), x && (w <= y) || !x && (w <= z)) ||
            rewrite(select(x, y, z) <= w, select(x, y <= w, z <= w)) ||
            rewrite(w <= select(x, y, z), select(x, w <= y, w <= z)) ||
            false) {
            return mutate(rewrite.result);
        } else {
            return a <= b;
        }
    }

    Expr visit(const NE *op) override {
        if (!op->a.type().is_bool()) {
            return mutate((op->a < op->b) || (op->b < op->a));
        } else {
            return mutate((op->a && !op->b) || (!op->a && op->b));
        }
    }

    Expr visit(const Select *op) override {
        indent();
        debug(0) << "Mutating select: " << Expr(op) << "\n";
        indent_++;

        if (const LT *lt = op->condition.as<LT>()) {
            const Variable *var_a = lt->a.as<Variable>();
            const Variable *var_b = lt->b.as<Variable>();
            if (is_zero(lt->a) && var_b) {
                if (var_sign.contains(var_b->name)) {
                    auto s = var_sign.get(var_b->name);
                    if (s == VarSign::Positive) {
                        return mutate(op->true_value);
                    } else if (s == VarSign::Negative ||
                               s == VarSign::NonPositive) {
                        return mutate(op->false_value);
                    }
                }
                indent();
                debug(0) << "A\n";
                Expr cond = mutate(op->condition);
                Expr true_value, false_value;
                {
                indent();
                    debug(0) << "Assuming " << lt->b << " positive\n";
                    ScopedBinding<VarSign> bind(var_sign, var_b->name, VarSign::Positive);
                    true_value = mutate(op->true_value);
                }
                {
                indent();
                    debug(0) << "Assuming " << lt->b << " non-positive\n";
                    ScopedBinding<VarSign> bind(var_sign, var_b->name, VarSign::NonPositive);
                    false_value = mutate(op->false_value);
                }
                indent_--;
                indent();
                debug(0) << "Returning\n";
                return select(cond, true_value, false_value);
            } else if (is_zero(lt->b) && var_a) {
                if (var_sign.contains(var_a->name)) {
                    auto s = var_sign.get(var_a->name);
                    if (s == VarSign::Negative) {
                        return mutate(op->true_value);
                    } else if (s == VarSign::Positive ||
                               s == VarSign::NonNegative) {
                        return mutate(op->false_value);
                    }
                }
                indent();
                debug(0) << "B\n";
                Expr cond = mutate(op->condition);
                Expr true_value, false_value;
                {
                    ScopedBinding<VarSign> bind(var_sign, var_a->name, VarSign::Negative);
                    true_value = mutate(op->true_value);
                }
                {
                    ScopedBinding<VarSign> bind(var_sign, var_a->name, VarSign::NonNegative);
                    false_value = mutate(op->false_value);
                }
                indent_--;
                return select(cond, true_value, false_value);
            }
        }
        indent_--;
        return IRMutator::visit(op);
    }

};

class ToDNF : public IRMutator {
    using IRMutator::visit;
    Expr visit(const Select *op) override {
        if (!op->type.is_bool()) {
            return IRMutator::visit(op);
        }
        return mutate(op->condition && op->true_value ||
                      !op->condition && op->false_value);
    }

    Expr visit(const And *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        if (const Or *or_a = a.as<Or>()) {
            return mutate(or_a->a && b || or_a->b && b);
        } else if (const Or *or_b = b.as<Or>()) {
            return mutate(a && or_b->a || a && or_b->b);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Not *op) override {
        Expr a = mutate(op->a);
        if (const And *and_a = a.as<And>()) {
            return mutate(!and_a->a || !and_a->b);
        } else if (const Or *or_a = a.as<Or>()) {
            return mutate(!or_a->a && !or_a->b);
        } else {
            return IRMutator::visit(op);
        }
    }
};

class ConvertRoundingToMod : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Mul *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        const Div *d = a.as<Div>();
        if (d && equal(d->b, b)) {
            // Euclidean identity says: (a/b)*b + a % b == a. So:
            // (x / y) * y -> x - x % y
            return d->a - d->a % d->b;
        } else {
            return a * b;
        }
    }
};

bool can_disprove_nonconvex(Expr e, int beam_size, Expr *implication = nullptr) {

    debug(0) << "Attempting to disprove non-convex expression: " << e << "\n";

    // Canonicalize >, >=, and friends
    e = simplify(e);

    // Break it into convex pieces, and disprove every piece
    debug(0) << "Simplified: " << e << "\n";
    e = BreakIntoConvexPieces().mutate(e);
    debug(0) << "Moved boolean operators outermost: " << e << "\n";
    e = ToDNF().mutate(e);
    //e = ConvertRoundingToMod().mutate(e);

    vector<Expr> pieces, pending;
    pending.push_back(e);
    while (!pending.empty()) {
        Expr next = pending.back();
        pending.pop_back();
        if (const Or *op = next.as<Or>()) {
            pending.push_back(op->a);
            pending.push_back(op->b);
        } else {
            pieces.push_back(next);
        }
    }

    debug(0) << "Broken into convex pieces:\n";
    int i = 0;
    for (auto p : pieces) {
        debug(0) << (++i) << ") " << p << "\n";
    }

    // Simplify each piece.
    debug(0) << "Simplify each piece:\n";
    i = 0;
    for (auto &p : pieces) {
        p = simplify(p);
        debug(0) << (++i) << ") " << p << "\n";
    }

    if (implication) {
        // We're going to or together a term from each convex piece.
        *implication = const_false();
    }

    bool failed = false;

    for (auto p : pieces) {
        set<Expr, IRDeepCompare> implications;

        debug(0) << "Attempting to disprove non-trivial term: " << p << "\n";
        if (can_disprove(p, beam_size, &implications)) {
            debug(0) << "Success!\n";
        } else {
            debug(0) << "Failure\n";
            failed = true;
        }

        if (implication) {
            Expr m = const_true(); // Could also set it to p, but that should be captured below.
            for (auto i : implications) {
                m = m && i;
            }
            *implication = *implication || m;
        }
    }

    if (implication) {
        debug(0) << "Unsimplified implication: " << *implication << "\n";
        *implication = simplify(*implication);
    }

    return !failed;
}

Stmt lower_store_with(const Stmt &s, const vector<Function> &outputs, const map<string, Function> &env) {
    debug(3) << "Checking legality of store_with on: " << s << "\n";

    {
        Var v0, v1, v2, v3;
        Var c0("c0"), c1("c1"), c2("c2"), c3("c3"), c4("c4"), c5("c5"), c6("c6"), c7("c7"), c8("c8"), c9("c9"), c10("c10"), c11("c11"), c12("c12"), c13("c13"), c14("c14");
        /*
          Expr e = ((min(((((v0 + 13)/4)*4) + v1), (v2 + 10)) + 4) > min(((((min((((v0 + 17)/4)*4), (min((v2 - v1), v0) + 14)) + 3)/4)*4) + v1), (v2 + 14)));
        debug(0) << can_disprove_nonconvex(e, 256) <<"\n";
        */

        Expr m;
        //Expr to_prove = ((v0 + c0) / c2) * c2 >= ((v0 + c1) / c2) * c2;
        //Expr to_prove = ((min(((((v0 + c0)/c2)*c2) + v1), (v2 + c1)) + c3) > min(((((min((((v0 + c4)/c2)*c2), (min((v2 - v1), v0) + c5)) + c6)/c2)*c2) + v1), (v2 + c7)));
        //Expr to_prove = ((((v0 + c0)/c2)*c2) + v1) > min(((((min((((v0 + c4)/c2)*c2), (min((v2 - v1), v0) + c5)) + c6)/c2)*c2) + v1), (v2 + c7));
        /*Expr to_prove = (((((v0 + c0)/c2)*c2) + v1) >
          min(((((min((v0 + c4), (min((v2 - v1), v0) + c5)) + c6)/c2)*c2) + v1), (v2 + c7)));
        */

        /*
        Expr C0 = c0 * c2 + c1;
        Expr C1 = c3 * c2 + c4;
        Expr C3 = c5 * c2 + c6;
        Expr C4 = c7 * c2 + c8;
        Expr C5 = c9 * c2 + c10;
        Expr C6 = c11 * c2 + c12;
        Expr C7 = c13 * c2 + c14;

        assumption = assumption && (c1 >= 0 && c1 < c2);
        assumption = assumption && (c4 >= 0 && c4 < c2);
        assumption = assumption && (c6 >= 0 && c6 < c2);
        assumption = assumption && (c8 >= 0 && c8 < c2);
        assumption = assumption && (c10 >= 0 && c10 < c2);
        assumption = assumption && (c12 >= 0 && c12 < c2);
        assumption = assumption && (c14 >= 0 && c14 < c2);
        */

        /*
        Expr to_prove = (((((v0 + c0)/c2)*c2) + v1) >
                         min(((((min((v0 + c4), (v0 + c5)) + c6)/c2)*c2) + v1), (v2 + c7)));
        */

        // Expr assumption = c2 >= 1;
        //Expr assumption = c2 != 0;
        //Expr to_prove = ((min(((((v0 + c0)/c2)*c2) + v1), (v2 + c1)) + c3) <= min(((((min((((v0 + c4)/c2)*c2), (min((v2 - v1), v0) + c5)) + c6)/c2)*c2) + v1), (v2 + c7)));
        /*
        Expr assumption = const_true();
        Expr to_prove = ((min(((((v0 + 13)/4)*4) + v1), (v2 + 10)) + 4) <= min(((((min((((v0 + 17)/4)*4), (min((v2 - v1), v0) + 14)) + 3)/4)*4) + v1), (v2 + 14)));
        */

        /*
        Expr to_prove = v0 * c0 - v1 * c1 == (v0 * (c0 / c1) - v1) * c1;
        Expr assumption = const_true(); //c1 != 0;
        */

        /*
        Expr to_prove = ((v0 * c0 + v1) % c1) == v1 % c1;
        Expr assumption = c1 > 0; //const_true();
        */
        Expr x = v0, y = v1, z = v2, w = v3;
        pair<Expr, Expr> exprs[] =
            {
                /*
                {min((((x + c0)/c1)*c1), (x + c2)) == (x + c2), c1 > 0},
                {min((((x + c0)/c1)*c1), (x + c2)) == (x + c2), c1 < 0},
                {((x + ((y + c0)/c1)) <= ((y + c2)/c1)) == (x < c3), c1 > 0},
                {((x + ((y + c0)/c1)) <= ((y + c2)/c1)) == (x < c3), c1 < 0},
                {((x + ((y + c0)/c1)) <= ((y + c2)/c1)) == (x <= c2), c1 > 0},
                {((x + ((y + c0)/c1)) <= ((y + c2)/c1)) == (x <= c2), c1 < 0},
                {((x + (y*c0)) <= (((y*c1) + z)*c1)) == (x <= (z*c1)), const_true()},
                {(((x*c0) + y) <= (((x*c1) - z)*c1)) == (y <= (z*c2)), const_true()},
                {(((x - y)/c0) + (((y - x) + c1)/c0)) == c2, c0 > 0},
                {(((x - y)/c0) + (((y - x) + c1)/c0)) == c2, c0 < 0},
                {((((x*c0) + c1)/c2) <= max(((x*c0)/c2), y)), c2 > 0},
                {((((x*c0) + c1)/c2) <= max(((x*c0)/c2), y)), c2 < 0},
                */

                {(((min(x, y)*c0) + c1) <= min((x*c0), c2)) == (min(x, y) < c2), c0 > 0},
                /*
                {(((min(x, y)*c0) + c1) <= min((x*c0), c2)) == (min(x, y) < c2), c0 < 0},
                {((min((x*c0), c1)*y) <= min((x*c2), c3)) == (c4 <= y), const_true()},
                */
                //{((min((min(x, c0) + y), c1) + c2) <= min(y, c3)), const_true()},
            };

        Expr assumption = const_true();
        for (auto p : exprs) {
            Expr to_prove = p.first;
            assumption = p.second;

        assumption = simplify(assumption);
        debug(0) << can_disprove_nonconvex(assumption && !to_prove, 1024*4, &m);

        debug(0) << "\nImplication: " << m << "\n";

        class NormalizePrecondition : public IRMutator {
            using IRMutator::visit;
            Expr visit(const Not *op) override {
                if (const Or *o = op->a.as<Or>()) {
                    return mutate(!o->a && !o->b);
                } else if (const And *o = op->a.as<And>()) {
                    return mutate(!o->a || !o->b);
                } else {
                    return IRMutator::visit(op);
                }
            }

            Expr visit(const And *op) override {
                Expr a = mutate(op->a);
                Expr b = mutate(op->b);
                vector<Expr> pending = {a, b};
                set<Expr, IRDeepCompare> terms;
                while (!pending.empty()) {
                    Expr next = pending.back();
                    pending.pop_back();
                    if (const And *next_and = next.as<And>()) {
                        pending.push_back(next_and->a);
                        pending.push_back(next_and->b);
                    } else {
                        terms.insert(next);
                    }
                }
                Expr result;
                for (auto t : terms) {
                    if (!result.defined()) {
                        result = t;
                    } else {
                        result = result && t;
                    }
                }
                return result;
            }

            Expr visit(const Or *op) override {
                Expr a = mutate(op->a);
                Expr b = mutate(op->b);
                vector<Expr> pending = {a, b};
                set<Expr, IRDeepCompare> terms;
                while (!pending.empty()) {
                    Expr next = pending.back();
                    pending.pop_back();
                    if (const Or *next_or = next.as<Or>()) {
                        pending.push_back(next_or->a);
                        pending.push_back(next_or->b);
                    } else {
                        terms.insert(next);
                    }
                }
                Expr result;
                for (auto t : terms) {
                    if (!result.defined()) {
                        result = t;
                    } else {
                        result = result || t;
                    }
                }
                return result;
            }
        };

        // Exploit the assumption to simplify the implications. Cleans
        // up the expression a little.
        Simplify simplifier(true, nullptr, nullptr);
        simplifier.learn_true(assumption);
        m = simplifier.mutate(m, nullptr);
        debug(0) << "Assumption: " << assumption << "\n";
        debug(0) << "Simplified implication using assumption: " << m << "\n";

        Expr precondition = simplify(assumption && !m);
        precondition = NormalizePrecondition().mutate(precondition);

        // We probably have a big conjunction. Use each term in it to
        // simplify all subsequent terms, to reduce the number of
        // overlapping conditions.
        vector<Expr> terms;
        vector<Expr> pending = {precondition};
        while (!pending.empty()) {
            Expr next = pending.back();
            pending.pop_back();
            if (const And *next_and = next.as<And>()) {
                pending.push_back(next_and->a);
                pending.push_back(next_and->b);
            } else {
                terms.push_back(next);
            }
        }
        precondition = Expr();
        for (auto &t1 : terms) {
            Simplify s(true, nullptr, nullptr);
            for (const auto &t2 : terms) {
                if (t1.same_as(t2)) continue;
                s.learn_true(t2);
            }
            t1 = s.mutate(t1, nullptr);

            if (!precondition.defined()) {
                precondition = t1;
            } else {
                precondition = precondition && t1;
            }
        }

        precondition = simplify(precondition);

        debug(0) << "Precondition " << precondition << "\n"
                 << "implies " << to_prove << "\n";
    }
        /*
        ((v0 + c0) / c1) * c1 < ((v0 + c2) / c3) * c3;

        let k0 * c1 + k1 = v0 + c0;
        let k2 * c3 + k3 = v0 + c2;
        (k0 * c1 < k2 * c3);
        */

        // A strategy: Abstract a non-linear terms as a bounded variables. Hope that uses of it get cancelled???

        /*
        // Try with a symbolic constant instead of 4

        e = ((min(((((v0 + 13)/c0)*c0) + v1), (v2 + 10)) + 4) > min(((((min((((v0 + 17)/c0)*c0), (min((v2 - v1), v0) + 14)) + 3)/c0)*c0) + v1), (v2 + 14)));
        e = e && (c0 > 0);
        debug(0) << can_disprove_nonconvex(e, 32) <<"\n";

        */

        exit(-1);
    }


    // Remap the args on all accesses, but not the names, using the
    // additional args to store_with that specify the coordinate
    // mapping between the two buffers.
    class RemapArgs : public IRMutator {
        using IRMutator::visit;

        vector<Expr> remap_args(const Function &f,
                                const StoreWithDirective &stored_with,
                                const vector<Expr> &old_args) {
            map<string, Expr> coordinate_remapping;
            for (int i = 0; i < f.dimensions(); i++) {
                coordinate_remapping[f.args()[i]] = old_args[i];
            }
            vector<Expr> new_args;
            for (const Expr &a : stored_with.where) {
                new_args.push_back(substitute(coordinate_remapping, a));
            }
            return new_args;
        }

        Stmt visit(const Provide *op) override {
            auto it = env.find(op->name);
            internal_assert(it != env.end());

            const auto &stored_with = it->second.schedule().store_with();

            if (it->second.schedule().store_with().buffer.empty()) {
                return IRMutator::visit(op);
            }

            Stmt p = IRMutator::visit(op);
            op = p.as<Provide>();
            internal_assert(op);
            return Provide::make(op->name, op->values, remap_args(it->second, stored_with, op->args));
        }

        Expr visit(const Call *op) override {
            if (op->call_type != Call::Halide) {
                return IRMutator::visit(op);
            }

            auto it = env.find(op->name);
            internal_assert(it != env.end());

            const auto &stored_with = it->second.schedule().store_with();

            if (stored_with.buffer.empty()) {
                return IRMutator::visit(op);
            }

            Expr c = IRMutator::visit(op);
            op = c.as<Call>();
            internal_assert(op);
            auto args = remap_args(it->second, stored_with, op->args);
            return Call::make(it->second, args, op->value_index);
        }

        const map<string, Function> &env;
    public:
        RemapArgs(const map<string, Function> &env) : env(env) {}
    } remap_args(env);

    Stmt stmt = remap_args.mutate(s);

    {
        // Check legality on a simplified version
        Stmt simpler = simplify(uniquify_variable_names(stmt));

        // debug(0) << "Simplified: " << simpler << "\n";

        // Add dummy realize nodes for the outputs
        for (auto f : outputs) {
            Region r;
            simpler = Realize::make(f.name(), f.output_types(), MemoryType::Auto, r, const_true(), simpler);
        }

        // TODO: Once we support storing with inputs, we should add
        // dummy realize nodes for the inputs here.

        // For each buffer, figure out what other buffers are also stored there.
        map<string, vector<string>> groups;
        for (const auto &p : env) {
            const auto &stored_with = p.second.schedule().store_with();
            if (!stored_with.buffer.empty()) {
                groups[stored_with.buffer].push_back(p.first);

                // Some legality checks on the destination buffer
                auto it = env.find(stored_with.buffer);
                user_assert(it != env.end())
                    << "Can't store " << p.first << " with "
                    << stored_with.buffer << " because "
                    << stored_with.buffer << " is not used in this pipeline\n";

                user_assert(!it->second.schedule().store_level().is_inlined())
                    << "Can't store " << p.first << " with "
                    << stored_with.buffer << " because "
                    << stored_with.buffer << " is scheduled inline and thus has no storage\n";

                user_assert(!it->second.schedule().async())
                    << "Can't store " << p.first << " with "
                    << stored_with.buffer << " because "
                    << stored_with.buffer << " is scheduled async and cannot have multiple productions\n";

                user_assert(it->second.schedule().store_with().buffer.empty())
                    << "Can't store " << p.first << " with "
                    << stored_with.buffer << " because "
                    << stored_with.buffer << " is in turn stored with "
                    << it->second.schedule().store_with().buffer
                    << " and has no storage of its own\n";

                user_assert(!it->second.schedule().memoized())
                    << "Can't store " << p.first << " with "
                    << stored_with.buffer << " because "
                    << stored_with.buffer << " is memoized\n";

                // Some legality checks on the source Func
                for (auto &d : p.second.schedule().storage_dims()) {
                    user_assert(!d.alignment.defined())
                        << "Can't align the storage of " << p.first
                        << " in dimension " << d.var
                        << " because it does not have storage of its own, "
                        << "and is instead stored_with " << stored_with.buffer << "\n";

                    user_assert(!d.fold_factor.defined())
                        << "Can't fold the storage of " << p.first
                        << " in dimension " << d.var
                        << " because it does not have storage of its own, "
                        << "and is instead stored_with " << stored_with.buffer << "\n";
                }

                user_assert(!p.second.schedule().memoized())
                    << "Can't store " << p.first << " with "
                    << stored_with.buffer << " because "
                    << p.first << " is memoized\n";

                // Check the coordinate mapping doesn't store distinct
                // values at the same site.

                // Try to find a set of distinct coords in the
                // buffer's domain that are stored at the same
                // site. Hopefully we will fail. WLOG assume that one
                // of the sites is lexicographically before the other,
                // so that we can use our constraint system machinery.
                vector<Expr> disproofs;
                map<string, Expr> remapping1, remapping2;
                for (int i = 0; i < p.second.dimensions(); i++) {
                    string n1 = unique_name('t');
                    string n2 = unique_name('t');
                    Expr v1 = Variable::make(Int(32), n1);
                    Expr v2 = Variable::make(Int(32), n2);
                    string v = p.second.args()[i];
                    remapping1[v] = v1;
                    remapping2[v] = v2;

                    for (auto &e : disproofs) {
                        e = e && (v1 == v2);
                    }
                    disproofs.push_back(v1 > v2);
                }
                Expr same_dst = const_true();
                for (size_t i = 0; i < stored_with.where.size(); i++) {
                    same_dst =
                        (same_dst &&
                         (substitute(remapping1, stored_with.where[i]) ==
                          substitute(remapping2, stored_with.where[i])));
                }
                // Exploit any explicit bounds on the vars
                for (const auto &b : p.second.schedule().bounds()) {
                    Expr v1 = remapping1[b.var];
                    Expr v2 = remapping2[b.var];
                    if (b.min.defined()) {
                        same_dst = same_dst && (v1 >= b.min) && (v2 >= b.min);
                        if (b.extent.defined()) {
                            same_dst = same_dst && (v1 < b.min + b.extent) && (v2 < b.min + b.extent);
                        }
                    }
                }

                for (auto &e : disproofs) {
                    // Beam size for the one-to-one proof
                    const int beam_size = 32;
                    if (!can_disprove(e && same_dst, beam_size)) {
                        user_error << "Failed to prove that store_with mapping for " << p.first
                                   << " does not attempt place multiple values at the same site of "
                                   << stored_with.buffer << "\n";
                    }
                }
            }
        }

        class CheckEachRealization : public IRVisitor {
            using IRVisitor::visit;

            Scope<> realizations;

            void visit(const Realize *op) override {
                ScopedBinding<> bind(realizations, op->name);
                IRVisitor::visit(op);

                auto it = groups.find(op->name);
                if (it == groups.end()) {
                    return;
                }

                auto names = it->second;

                // Beam size for the no-clobber proofs
                const int beam_size = 32;

                for (const string &n : names) {
                    // Check we didn't create race conditions on any
                    // of the stored_with things. The code should
                    // behave as if the Func is compute root - all
                    // update definitions appear to happen serially
                    // for each site.

                    auto uses = get_times_of_all_uses(op->body, n, env);
                    for (size_t i = 0; i < uses.size(); i++) {
                        const auto &u1 = uses[i];
                        if (!u1.is_store()) continue;
                        for (size_t j = i + 1; j < uses.size(); j++) {
                            const auto &u2 = uses[j];
                            if (!u2.is_store()) continue;
                            if (!u1.safely_before(u2, beam_size)) {
                                std::ostringstream err;
                                err << "Cannot store " << n << " in the same buffer as " << op->name << "\n"
                                    << "In this code:\n" << Stmt(op) << "\n"
                                    << "Failed to prove that at every site, this ";
                                u1.dump(err);
                                err << "Always happens before than this ";
                                u2.dump(err);
                                user_error << err.str();
                            }
                        }
                    }
                }

                names.push_back(it->first);

                /*
                debug(0) << "store_with group: ";
                for (const auto &n : names) {
                    debug(0) << n << " ";
                }
                debug(0) << "\n";
                */

                for (size_t i = 0; i < names.size(); i++) {
                    const string &n1 = names[i];
                    auto uses_1 = get_times_of_all_uses(op->body, n1, env);

                    for (size_t j = i+1; j < names.size(); j++) {
                        const string &n2 = names[j];

                        auto uses_2 = get_times_of_all_uses(op->body, n2, env);

                        // Check all uses of 1 are before all uses of 2

                        for (const auto &u1 : uses_1) {
                            for (const auto &u2 : uses_2) {
                                /*
                                debug(0) << "Ordering:\n";
                                u1.dump(std::cerr);
                                u2.dump(std::cerr);
                                */
                                if (!u1.safely_before(u2, beam_size)) {
                                    std::ostringstream err;
                                    err << "Cannot store " << n1 << " in the same buffer as " << n2 << "\n"
                                        << "In this code:\n" << Stmt(op) << "\n"
                                        << "Failed to prove that at every site, this ";
                                    u1.dump(err);
                                    err << "Always happens before than this ";
                                    u2.dump(err);
                                    user_error << err.str();
                                }
                            }
                        }
                    }
                }
            }

            void visit(const Call *op) override {
                IRVisitor::visit(op);
                if (op->call_type == Call::Halide) {
                    auto it = parent.find(op->name);
                    if (it != parent.end()) {
                        user_assert(realizations.contains(it->second))
                            << "Cannot store " << op->name << " with " << it->second
                            << " because there is a use of " << op->name
                            << " outside of the store_at level of " << it->second << "\n";
                    }
                }
            }

            void visit(const Provide *op) override {
                IRVisitor::visit(op);
                auto it = parent.find(op->name);
                if (it != parent.end()) {
                    user_assert(realizations.contains(it->second))
                        << "Cannot store " << op->name << " with " << it->second
                        << " because there is a store to " << op->name
                        << " outside of the store_at level of " << it->second << "\n";
                }
            }

            void visit(const Variable *op) override {
                auto it = parent.find(op->name);
                if (it != parent.end()) {
                    user_assert(realizations.contains(it->second))
                        << "Cannot store " << op->name << " with " << it->second
                        << " because there is a direct reference to the allocation of " << op->name
                        << ". This may be caused by passing it to an extern stage.\n";
                    // TODO worry about GPU copies and store_with
                }
            }

            std::map<string, vector<string>> groups;
            std::map<string, string> parent;
            const std::map<string, Function> &env;

        public:
            CheckEachRealization(const std::map<string, vector<string>> &groups,
                                 const std::map<string, Function> &env) : groups(groups), env(env) {
                for (const auto &p : groups)  {
                    for (const auto &c : p.second) {
                        parent[c] = p.first;
                    }
                }
            }
        } checker(groups, env);

        simpler.accept(&checker);
    }

    // We now know that everything is legal. Remap the buffer names.
    class RemapNames : public IRMutator {
        using IRMutator::visit;

        Stmt visit(const ProducerConsumer *op) override {
            auto it = env.find(op->name);
            if (it != env.end()) {
                const string &stored_with = it->second.schedule().store_with().buffer;
                if (!stored_with.empty()) {
                    return ProducerConsumer::make(stored_with, op->is_producer, mutate(op->body));
                }
            }
            return IRMutator::visit(op);
        }

        Stmt visit(const Realize *op) override {
            auto it = env.find(op->name);
            if (it != env.end() &&
                !it->second.schedule().store_with().buffer.empty()) {
                return mutate(op->body);
            } else {
                return IRMutator::visit(op);
            }
        }

        Stmt visit(const Provide *op) override {
            auto it = env.find(op->name);
            internal_assert(it != env.end());

            const auto &stored_with = it->second.schedule().store_with();

            if (it->second.schedule().store_with().buffer.empty()) {
                return IRMutator::visit(op);
            }

            Stmt p = IRMutator::visit(op);
            op = p.as<Provide>();
            internal_assert(op);
            return Provide::make(stored_with.buffer, op->values, op->args);
        }

        Expr visit(const Call *op) override {
            if (op->call_type != Call::Halide) {
                return IRMutator::visit(op);
            }

            auto it = env.find(op->name);
            internal_assert(it != env.end());

            const auto &stored_with = it->second.schedule().store_with();

            if (stored_with.buffer.empty()) {
                return IRMutator::visit(op);
            }

            Expr c = IRMutator::visit(op);
            op = c.as<Call>();
            internal_assert(op);
            auto stored_with_it = env.find(stored_with.buffer);
            internal_assert(stored_with_it != env.end());
            return Call::make(op->type, stored_with_it->second.name(), op->args,
                              op->call_type,
                              stored_with_it->second.get_contents(),
                              op->value_index, Buffer<>(), Parameter());
        }

        const map<string, Function> &env;
    public:
        RemapNames(const map<string, Function> &env) : env(env) {}
    } remap_names(env);

    return remap_names.mutate(stmt);

}


}  // namespace Internal
}  // namespace Halide
