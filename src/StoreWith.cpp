#include "StoreWith.h"

#include "CSE.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
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
    bool parallel;
    ClockDim(const Expr &t, bool p) : t(t), parallel(p) {}
};

// Return the symbolic times and sites for all uses of a buffer.
struct Use {
    vector<ClockDim> time;
    vector<Expr> site;
    Expr predicate;
    string name;
    bool is_store;

    Use(const vector<ClockDim> &t,
        const vector<Expr> &s,
        const vector<Expr> &p,
        const string &n,
        const vector<string> &loops,
        const vector<pair<string, Expr>> &lets,
        bool is_store) :
        time(t), site(s), predicate(const_true()), name(n), is_store(is_store) {

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            for (auto &e : site) {
                if (expr_uses_var(e, it->first)) {
                    e = Let::make(it->first, it->second, e);
                }
            }
        }

        // Make any variables unique to this use
        map<string, Expr> renaming;
        for (const auto &v : loops) {
            string new_name = unique_name('t');
            Expr new_var = Variable::make(Int(32), new_name);
            debug(0) << v << " -> " << new_var << "\n";
            renaming[v] = new_var;
        }

        // TODO: parallel variables don't get included in the renaming, which means they're not unique across uses!

        for (auto &e : site) {
            e = substitute(renaming, e);
        }

        for (auto &e : time) {
            e.t = substitute(renaming, e.t);
        }

        for (const auto &e : p) {
            predicate = predicate && e;
        }

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            if (expr_uses_var(predicate, it->first)) {
                predicate = Let::make(it->first, it->second, predicate);
            }
        }

        predicate = substitute(renaming, predicate);
        // dump();
    }

    Use() = default;

    // Return a boolean in DNF form encoding whether one time vector
    // is < another . Quadratic in loop depth.
    vector<Expr> may_happen_before(const Use &other, bool includes_equal = false, size_t start = 0) const {
        vector<Expr> result;

        // Lexicographic order starting at the given index

        if (start == time.size() && start == other.time.size()) {
            // Both are empty
            if (includes_equal) {
                result.push_back(const_true());
            }
            return result;
        }

        if (start == time.size()) {
            // Empty vs non-empty
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
            // Early out if it can be resolved statically on this dimension
            if (can_prove(time[start].t < other.time[start].t)) {
                result.push_back(const_true());
                return result;
            }
            if (can_prove(time[start].t > other.time[start].t)) {
                return result;
            }
        }

        result = may_happen_before(other, includes_equal, start + 1);

        for (auto &e : result) {
            e = substitute(time[start].t, other.time[start].t, e);
            e = other.time[start].t == time[start].t && e;
        }

        if (time[start].parallel) {
            internal_assert(other.time[start].parallel);
            result.push_back(other.time[start].t != time[start].t);
        } else {
            internal_assert(!other.time[start].parallel);
            result.push_back(other.time[start].t > time[start].t);
        }

        return result;
    }

    void dump() const {
        debug(0) << (is_store ? "Store" : "Load")
                 << " of " << name << ":\n"
                 << "Clock: ";
        for (const auto &e : time) {
            debug(0) << e.t << (e.parallel ? " p" : " s") << ", ";
        }
        debug(0) << "\n";
        debug(0) << "Site: ";
        for (const auto &e : site) {
            debug(0) << e << " ";
        }
        debug(0) << "\n";
        debug(0) << "Predicate: " << predicate << "\n";
    }

    vector<Expr> may_happen_before_or_at_the_same_time(const Use &other) const {
        return may_happen_before(other, true);
    }

    struct Equality {
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

        std::map<Expr, int, IRDeepCompare> terms;
        int num_vars = 0;

        bool uses_var(const std::string &name) const {
            for (const auto &p : terms) {
                if (expr_uses_var(p.second, name)) {
                    return true;
                }
            }
            return false;
        }

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

    class System {
    public:
        // A bunch of equalities
        vector<Equality> equalities;
        // A simplifier instance, which slowly accumulates knowledge
        // about the bounds of different variables.
        Simplify *simplifier;
        // An additional arbitrary term to place non-linear constraints
        Expr remainder;

        // A heuristic for how close we are to finding infeasibility
        float c = 0;

        // IDs for debugging and training a good heuristic
        static int id_counter;
        int id, parent_id;

        System(Simplify *s, int pid) : simplifier(s), id(id_counter++), parent_id(pid) {}

        void add_equality(const EQ *eq) {
            equalities.emplace_back(eq);
        }

        void add_non_linear_term(const Expr &e) {
            internal_assert(e.type().is_bool()) << e << "\n";
            if (is_zero(e) || !remainder.defined()) {
                remainder = e;
            } else {
                remainder = remainder && e;
            }
            // Non-linearities count for 3.
            c += 3;
        }

        void add_term(const Expr &e) {
            debug(0) << "Adding term " << e << "\n";
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
                    debug(0) << "Telling simplifier that " << e << "\n";
                    simplifier->learn_true(e);
                } else if (is_const(le->b) && va) {
                    debug(0) << "Telling simplifier that " << e << "\n";
                    simplifier->learn_true(e);
                } else {
                    Expr v = aux();
                    debug(0) << "Telling simplifier that " << (-1 < v) << "\n";
                    simplifier->learn_true(-1 < v);
                    add_term(le->a + v == le->b);
                }
            } else if (lt && lt->a.type() == Int(32)) {
                const Variable *va = lt->a.as<Variable>();
                const Variable *vb = lt->b.as<Variable>();
                if (const Min *min_b = lt->b.as<Min>()) {
                    // x <= min(y, z) -> x <= y && x <= z
                    add_term(le->a < min_b->a);
                    add_term(le->a < min_b->b);
                } else if (const Max *max_a = lt->a.as<Max>()) {
                    // max(x, y) <= z -> x <= z && y <= z
                    add_term(max_a->a < le->b);
                    add_term(max_a->b < le->b);
                } else if (is_const(lt->a) && vb) {
                    debug(0) << "Telling simplifier that " << e << "\n";
                    simplifier->learn_true(e);
                } else if (is_const(lt->b) && va) {
                    debug(0) << "Telling simplifier that " << e << "\n";
                    simplifier->learn_true(e);
                } else {
                    Expr v = aux();
                    debug(0) << "Telling simplifier that " << (0 < v) << "\n";
                    simplifier->learn_true(0 < v);
                    add_term(lt->a + v == lt->b);
                }
            } else if (const Let *l = e.as<Let>()) {
                if (l->value.type().is_bool()) {
                    add_term(substitute(l->name, l->value, l->body));
                } else {
                    Expr eq = Variable::make(l->value.type(), l->name) == l->value;
                    debug(0) << "Telling simplifier that " << eq << "\n";
                    simplifier->learn_true(eq);
                    add_term(eq);
                    add_term(l->body);
                }
            } else {
                add_non_linear_term(e);
            }
        }

        Expr non_linear_term() const {
            if (remainder.defined()) {
                return remainder;
            } else {
                return const_true();
            }
        }

        void dump() const {
            for (auto &e : equalities) {
                debug(0) << " " << e.to_expr() << "\n";
            }
            if (remainder.defined()) {
                debug(0) << " " << remainder << "\n";
            }
            const auto &info = simplifier->bounds_and_alignment_info;
            for (auto it = info.cbegin(); it != info.cend(); ++it){
                bool used = false;
                for (auto &e : equalities) {
                    used |= expr_uses_var(e.to_expr(), it.name());
                }
                if (remainder.defined()) {
                    used |= expr_uses_var(remainder, it.name());
                }
                if (!used) continue;
                if (it.value().min_defined && it.value().max_defined) {
                    debug(0) << it.value().min << " <= " << it.name()
                             << " <= " << it.value().max << "\n";
                } else if (it.value().min_defined) {
                    debug(0) << it.value().min << " <= " << it.name() << "\n";
                } else if (it.value().max_defined) {
                    debug(0) << it.name() << " <= " << it.value().max << "\n";
                }
            }
        }

        bool infeasible() const {
            for (auto &e : equalities) {
                if (is_zero(simplifier->mutate(e.to_expr(), nullptr))) {
                    debug(0) << "INFEASIBLE: " << id << "\n";
                    return true;
                }
            }
            if (remainder.defined() && is_zero(simplifier->mutate(remainder, nullptr))) {
                debug(0) << "INFEASIBLE: " << id << "\n";
                return true;
            }
            return false;
        }

        void compute_complexity(Simplify *simplify) {
            std::map<std::string, int> inequalities;
            int non_linear_terms = 0, num_terms = 0;
            for (auto &e : equalities) {
                for (auto &p : e.terms) {
                    Simplify::ExprInfo info;
                    simplify->mutate(p.first, &info);
                    if (const Variable *var = p.first.as<Variable>()) {
                        inequalities[var->name] = (int)info.max_defined + (int)info.min_defined;
                    } else if (!is_const(p.first)) {
                        non_linear_terms++;
                    }
                    num_terms++;
                }
            }
            int unconstrained_vars = 0;
            int semi_constrained_vars = 0;
            int totally_constrained_vars = 0;
            int num_constraints = (int)equalities.size() + (int)remainder.defined();
            for (const auto &p : inequalities) {
                if (p.second == 0) {
                    unconstrained_vars++;
                } else if (p.second == 1) {
                    semi_constrained_vars++;
                } else {
                    totally_constrained_vars++;
                }
            }
            debug(0) << "FEATURES " << id << " " << parent_id << " " << non_linear_terms << " " << unconstrained_vars << " " << semi_constrained_vars << " " << totally_constrained_vars << " " << num_terms << " " << num_constraints << "\n";
            int terms[] = {non_linear_terms, unconstrained_vars, semi_constrained_vars,
                           totally_constrained_vars, num_terms, num_constraints};
            // Use a linear combination of these features to decide
            // which stats are the most promising to explore. Trained
            // by tracking which states lead to success in the
            // store_with test and minimizing cross-entropy loss on a
            // linear classifier.
            float coeffs[] = {-0.2117f, -0.0170f, -0.2342f, -0.0031f,  0.0517f, -0.0789f};
            c = 0.0f;
            for (int i = 0; i < 6; i++) {
                c += terms[i] * coeffs[i];
            }
        }

        float complexity() const {
            return c;
        }

        void make_children(std::deque<std::unique_ptr<System>> &result) {
            // Sort the system, putting low term-count expressions with naked vars first
            std::stable_sort(equalities.begin(), equalities.end(),
                             [](const Equality &a, const Equality &b) {
                                 if (a.num_vars > b.num_vars) return true;
                                 if (a.num_vars < b.num_vars) return false;
                                 return a.terms.size() < b.terms.size();
                             });

            // Eliminate divs and mods by introducing new variables
            for (int i = 0; i < (int)equalities.size(); i++) {
                Expr lhs, rhs;
                for (auto &p : equalities[i].terms) {
                    const Mod *mod = p.first.as<Mod>();
                    const Div *div = p.first.as<Div>();
                    if (mod) {
                        lhs = mod->a;
                        rhs = mod->b;
                    } else if (div) {
                        lhs = div->a;
                        rhs = div->b;
                    }
                    if (is_const(rhs)) {
                        break;
                    } else {
                        lhs = rhs = Expr();
                    }
                }
                if (lhs.defined()) {
                    Expr k1 = aux(), k2 = aux();
                    Expr replacement = k1 + k2 * rhs;
                    auto subs = [&](Expr e) {
                        e = substitute(lhs % rhs, k1, e);
                        e = substitute(lhs / rhs, k2, e);
                        return simplifier->mutate(e, nullptr);
                    };
                    std::unique_ptr<System> new_system(new System(simplifier, id));
                    if (remainder.defined()) {
                        new_system->add_term(subs(remainder));
                    }
                    for (int j = 0; j < (int)equalities.size(); j++) {
                        new_system->add_term(subs(equalities[j].to_expr()));
                    }
                    new_system->add_term(lhs == replacement);
                    debug(0) << "Telling simplifier that: " << ((-1 < k1) && (k1 < rhs)) << '\n';
                    simplifier->learn_true(-1 < k1);
                    simplifier->learn_true(k1 < rhs);
                    new_system->compute_complexity(simplifier);
                    result.emplace_back(std::move(new_system));
                    return;
                }
            }

            // Eliminate an unbounded variable
            std::vector<const Variable *> vars;
            for (int i = 0; i < (int)equalities.size(); i++) {
                if (equalities[i].num_vars == 0) {
                    // We're not going to be able to find an
                    // elimination from something with no naked vars.
                    continue;
                }
                bool skip_it = false;
                for (const Variable *v : vars) {
                    // If x and y never both occur in the same
                    // equality, it doesn't matter what order we
                    // eliminate them in. So we could restrict
                    // candidates for elimination to things that
                    // co-occur with *all* previously-found candidates.
                    if (!equalities[i].uses_var(v->name)) {
                        skip_it = true;
                        break;
                    }
                }
                if (skip_it) {
                    continue;
                }

                for (const auto &p : equalities[i].terms) {
                    const Variable *var = p.first.as<Variable>();
                    if (var &&
                        (p.second == 1 || p.second == -1)) {

                        Expr rhs = 0;
                        for (const auto &p2 : equalities[i].terms) {
                            if (!p2.first.same_as(p.first)) {
                                rhs += p2.first * (p2.second * -p.second);
                            }
                        }

                        rhs = simplifier->mutate(rhs, nullptr);
                        // debug(0) << "Telling simplifier that: " << (p.first == rhs) << '\n';
                        // TODO: rhs may simplifier to p.first if we already knew the fact.
                        // simplifier->learn_true(p.first == rhs);

                        if (expr_uses_var(rhs, var->name)) {
                            // Didn't successfully eliminate it - it
                            // still occurs inside a non-linearity on
                            // the right.
                            continue;
                        }

                        // debug(0) << "Considering elimination " << var->name << " = " << rhs << "\n";
                        vars.push_back(var);

                        auto subs = [&](Expr e) {
                            e = substitute(var->name, rhs, e);
                            e = simplifier->mutate(e, nullptr);
                            return e;
                        };

                        std::unique_ptr<System> new_system(new System(simplifier, id));
                        if (remainder.defined()) {
                            new_system->add_term(subs(remainder));
                        }
                        for (int j = 0; j < (int)equalities.size(); j++) {
                            if (i == j) {
                                // The equation we exploited to get
                                // the substitution goes away.
                                continue;
                            }
                            // In the other equations, we replace the variable with the right-hand-side
                            new_system->add_term(subs(equalities[j].to_expr()));
                        }
                        new_system->compute_complexity(simplifier);
                        result.emplace_back(std::move(new_system));
                    }
                }
            }
        }
    };

    bool can_disprove(Expr e, int beam_size) const {
        e = common_subexpression_elimination(simplify(remove_likely_tags(e)));

        debug(0) << "Attempting to disprove: " << e << "\n";

        if (is_zero(e)) {
            debug(0) << "Trivially false\n";
            return true;
        }

        // The expression was constructed to be mostly a big
        // conjunction of equalities, so break it into those terms and
        // start substituting out variables.
        Simplify simplifier(true, nullptr, nullptr);
        std::unique_ptr<System> system(new System(&simplifier, 0));
        system->add_term(e);

        debug(0) << "Encoded as a system:\n";
        system->dump();

        std::deque<std::unique_ptr<System>> beam;
        beam.emplace_back(std::move(system));

        while (!beam.empty()) {
            // Take the best thing
            std::unique_ptr<System> next = std::move(beam.front());
            beam.pop_front();

            debug(0) << "Top of beam: " << next->complexity() << "\n";
            next->dump();

            if (next->infeasible()) {
                return true;
            }

            // Generate children
            next->make_children(beam);

            // Take top K
            std::stable_sort(beam.begin(), beam.end(),
                             [](const std::unique_ptr<System> &a,
                                const std::unique_ptr<System> &b) {
                                 return a->complexity() < b->complexity();
                             });

            while ((int)beam.size() > beam_size) {
                beam.pop_back();
            }
        }
        return false;
    }

    bool safely_before(const Use &other, int beam_size) const {
        // We want to prove that for every site in the shared
        // allocation, this use happens strictly before (<) the other
        // use. We negate this, and encode the problem as disproving:
        // other.might_happen_before_or_at_the_same_time_as(this) &&
        // same_site(other)

        // We'll generate that boolean expression in DNF form, and
        // attempt to disprove every single clause.

        Expr same_site = const_true();
        for (size_t i = 0; i < site.size(); i++) {
            same_site = same_site && site[i] == other.site[i];
        }

        Expr may_assume = simplify(same_site && predicate && other.predicate);

        debug(0) << "Same site: " << same_site << "\n";
        debug(0) << "Predicate: " << predicate << "\n";
        debug(0) << "Other predicate: " << other.predicate << "\n";

        /*
        // First try to prove this term false. If we can, then these
        // two uses never alias and we don't need to worry about
        // anything temporal (e.g. one use writes to even rows and the
        // other use writes to odd rows).
        if (can_disprove(may_assume)) {
            return true;
        }
        */

        // Now consider temporal constraints too.
        auto before = other.may_happen_before_or_at_the_same_time(*this);

        // Try to disprove each clause in turn.
        for (const auto &e : before) {
            if (!can_disprove(e && may_assume, beam_size)) {
                // Trigger the simplifier logging code
                return can_prove(!(e && may_assume));
                // return false;
            }
        }

        return true;
    }
};

int Use::System::id_counter = 0;

std::vector<Use> get_times_of_all_uses(const Stmt &s, string buf) {
    class PolyhedralClock : public IRVisitor {
        using IRVisitor::visit;

        vector<ClockDim> clock;
        vector<Expr> predicate;
        vector<string> loops;
        vector<pair<string, Expr>> lets;

        const string &buf;

        void visit(const Block *op) override {
            int i = 0;
            clock.emplace_back(i, false);
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
            predicate.push_back(loop_var >= op->min);
            predicate.push_back(loop_var < op->min + op->extent);
            loops.push_back(op->name);
            clock.emplace_back(Variable::make(Int(32), op->name), op->is_parallel());
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

        void found_use(const vector<Expr> &site, const string &name, bool is_store) {
            uses.emplace_back(clock, site, predicate, name, loops, lets, is_store);
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
                // TODO: skip this block if the buffer is an output - undef
                // sites in outputs may have meaningful values we're
                // supposed to not touch.
                bool rhs_undef = true;
                for (const auto &e : op->values) {
                    rhs_undef &= is_undef(e);
                }
                if (rhs_undef) {
                    return;
                }
            }

            // The RHS is evaluated before the store happens
            clock.emplace_back(0, false);
            IRVisitor::visit(op);
            clock.back().t = 1;
            if (op->name == buf) {
                found_use(op->args, op->name, true);
            }
            clock.pop_back();
        }

        void visit(const Call *op) override {
            IRVisitor::visit(op);
            if (op->name == buf) {
                found_use(op->args, op->name, false);
            }
        }
    public:
        vector<Use> uses;
        PolyhedralClock(std::string &b) : buf(b) {}
    } clock(buf);

    s.accept(&clock);

    for (const auto &u : clock.uses) {
        u.dump();
    }

    return clock.uses;
}

}  // namespace

Stmt lower_store_with(const Stmt &s, const map<string, Function> &env) {
    // First check legality on a simplified version of the stmt
    debug(0) << "Checking legality of store_with on: " << s << "\n";

    // Remap the args on all accesses, but not the names
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
            // Assumes no transitive buggery

            // TODO: assert stored_with in scope

            if (stored_with.buffer.empty()) {
                return IRMutator::visit(op);
            }

            Expr c = IRMutator::visit(op);
            op = c.as<Call>();
            internal_assert(op);
            auto args = remap_args(it->second, stored_with, op->args);
            return Call::make(it->second, args, op->value_index);
        }

        // TODO: ProducerConsumer?

        const map<string, Function> &env;
    public:
        RemapArgs(const map<string, Function> &env) : env(env) {}
    } remap_args(env);

    Stmt stmt = remap_args.mutate(s);

    {
        // Check legality on a simplified version
        Stmt simpler = simplify(uniquify_variable_names(stmt));

        debug(0) << "Simplified: " << simpler << "\n";

        // For each buffer, what other buffers are also stored there
        map<string, vector<string>> groups;
        for (const auto &p : env) {
            const auto &stored_with = p.second.schedule().store_with();
            if (!stored_with.buffer.empty()) {
                groups[stored_with.buffer].push_back(p.first);
            }
        }

        for (const auto &p : groups) {
            auto names = p.second;
            names.push_back(p.first);

            debug(0) << "store_with group: ";
            for (const auto &n : names) {
                debug(0) << n << " ";
            }
            debug(0) << "\n";

            for (size_t i = 0; i < names.size(); i++) {
                const string &n1 = names[i];
                auto uses_1 = get_times_of_all_uses(simpler, n1);

                for (size_t j = i+1; j < names.size(); j++) {
                    const string &n2 = names[j];

                    debug(0) << "Ordering " << n1 << " " << n2 << "\n";
                    auto uses_2 = get_times_of_all_uses(simpler, n2);

                    // Check all uses of 1 are before all uses of 2, or vice-versa

                    // TODO: can constrain this in various ways. No
                    // point checking topologically invalid
                    // orderings. Uses as inputs must be before temporaries
                    // and outputs. Uses as outputs must be after
                    // temporaries and inputs.

                    bool check1 = true, check2 = true;
                    for (const auto &u1 : uses_1) {
                        for (const auto &u2 : uses_2) {
                            if (!check1 && !check2) break;
                            bool c1 = check1, c2 = check2;
                            // We need to prove one of the two
                            // things. The other will probably fail,
                            // slowly. Race them against each other
                            // with steadily increasing beam size
                            // until we succeed on one of the proofs.
                            for (int beam_size = 1; beam_size < 8192; beam_size *= 2) {
                                debug(0) << "beam size " << beam_size << " " << c1 << " " << c2 << "\n";
                                debug(0) << "Ordering:\n";
                                u1.dump();
                                u2.dump();
                                c1 = check1 && u1.safely_before(u2, beam_size);
                                c2 = check2 && u2.safely_before(u1, beam_size);
                                if (c1 || c2) break;
                            }
                            check1 = c1;
                            check2 = c2;
                        }
                    }
                    user_assert(check1 || check2)
                        << "Could not prove it's safe to store " << n1
                        << " in the same buffer as " << n2 << '\n';
                }
            }
        }
    }

    // Remap the names
    class RemapNames : public IRMutator {
        using IRMutator::visit;

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

            // TODO: assert stored_with in scope

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
            // Assumes no transitive buggery

            // TODO: assert stored_with in scope

            if (stored_with.buffer.empty()) {
                return IRMutator::visit(op);
            }

            Expr c = IRMutator::visit(op);
            op = c.as<Call>();
            internal_assert(op);
            // TODO: handle store_with input params
            auto stored_with_it = env.find(stored_with.buffer);
            internal_assert(stored_with_it != env.end());
            return Call::make(stored_with_it->second, op->args, op->value_index);
        }

        const map<string, Function> &env;
    public:
        RemapNames(const map<string, Function> &env) : env(env) {}
    } remap_names(env);

    return remap_names.mutate(stmt);

}


}  // namespace Internal
}  // namespace Halide
