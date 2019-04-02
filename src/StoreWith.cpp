#include "StoreWith.h"

#include "CSE.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Solve.h"
#include "UniquifyVariableNames.h"
#include "Var.h"

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

// Return the symbolic times and sites for all uses of a buffer.
struct Use {
    vector<Expr> time, site;
    Expr predicate;
    string name;
    bool is_store;

    Use(const vector<Expr> &t, const vector<Expr> &s, const vector<Expr> &p, const string &n, bool is_store) :
        time(t), site(s), predicate(const_true()), name(n), is_store(is_store) {
        // Make any variables unique to this use
        map<string, Expr> renaming;
        for (auto &e : time) {
            if (const Variable *v = e.as<Variable>()) {
                string new_name = unique_name(v->name);
                Expr new_var = Variable::make(Int(32), new_name);
                renaming[v->name] = new_var;
                e = new_var;
            }
        }
        for (auto &e : site) {
            e = substitute(renaming, e);
        }
        for (const auto &e : p) {
            predicate = predicate && e;
        }
        predicate = substitute(renaming, predicate);
    }

    Use() = default;

    // Return a boolean in DNF form encoding whether one time vector
    // is < another . Quadratic in loop depth.
    vector<Expr> happens_before(const Use &other, bool includes_equal = false, size_t start = 0) const {
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

        result = happens_before(other, includes_equal, start + 1);

        for (auto &e : result) {
            e = substitute(time[start], other.time[start], e);
            e = other.time[start] == time[start] && e;
        }

        // Use a strictly positive auxiliary variable to encode the
        // ordering. Use a form that the simplifier is going to be
        // able to exploit.
        result.push_back(other.time[start] == time[start] + max(1, aux()));

        return result;
    }

    void dump() const {
        debug(0) << (is_store ? "Store" : "Load")
                 << " of " << name << ":\n"
                 << "Clock: ";
        for (const auto &e : time) {
            debug(0) << e << " ";
        }
        debug(0) << "\n";
        debug(0) << "Site: ";
        for (const auto &e : site) {
            debug(0) << e << " ";
        }
        debug(0) << "\n";
    }

    vector<Expr> happens_before_or_at_the_same_time(const Use &other) const {
        return happens_before(other, true);
    }

    vector<Expr> happens_strictly_before(const Use &other) const {
        return happens_before(other, false);
    }

    bool can_disprove(Expr e) const {
        debug(0) << "Attempting to disprove: " << e << "\n";

        e = simplify(e);

        if (is_zero(e)) {
            return true;
        }

        // Strip containing lets
        vector<pair<string, Expr>> lets;
        while (const Let *l = e.as<Let>()) {
            lets.emplace_back(l->name, l->value);
            e = l->body;
        }

        // The expression was constructed to be mostly a big
        // conjunction of equalities, so break it into those terms and
        // start substituting out variables.
        vector<Expr> c, pending;
        pending.push_back(e);
        while (!pending.empty()) {
            Expr next = pending.back();
            pending.pop_back();
            if (const And *a = next.as<And>()) {
                pending.push_back(a->a);
                pending.push_back(a->b);
            } else {
                c.push_back(next);
            }
        }

        debug(0) << "Attempting to disprove conjunction:\n";
        for (auto &e : c) {
            debug(0) << " " << e << "\n";
        }

        auto is_var_eq = [](const Expr &e, string *name, Expr *value) {
            const EQ *eq = e.as<EQ>();
            const Variable *var = eq ? eq->a.as<Variable>() : nullptr;
            if (var) {
                *name = var->name;
                *value = eq->b;
                return true;
            } else {
                return false;
            }
        };

        debug(0) << "Substituting...\n";

        while (1) {
            // Iteratively find a term of the form var == val, and use
            // it to substitute out the var and simplify the resulting
            // terms. Stop whenever we hit a const false.
            Expr next;
            string var;
            Expr val;
            for (size_t i = 0; i < c.size(); i++) {
                if (is_zero(c[i])) {
                    return true;
                }
                if (is_var_eq(c[i], &var, &val)) {
                    // This term is of the form var = value. Pop it
                    // and use it to substitute out the variable.
                    next = c[i];
                    c[i] = c.back();
                    c.pop_back();
                    break;
                }
            }

            if (next.defined()) {
                for (auto &e : c) {
                    e = simplify(substitute(var, val, e));
                    if (is_zero(e)) {
                        return true;
                    }
                }
            } else {
                // Stuck. Could be a good test case for improving the
                // simplifier. Convert it back to a single expression,
                // re-wrap the lets, and pass it to can_prove to
                // trigger that code.
                e = const_true();
                for (const auto &term : c) {
                    e = e && term;
                }
                while (!lets.empty()) {
                    e = Let::make(lets.back().first, lets.back().second, e);
                    lets.pop_back();
                }
                if (can_prove(!e)) {
                    return true;
                } else {
                    debug(0) << "Failed to disprove " << simplify(e) << "\n";
                    return false;
                }
            }
        }
    }

    bool safely_before(const Use &other) const {
        // We want to prove that for every site in the shared
        // allocation, this use happens strictly before (<) the other
        // use. We negate this, and encode the problem as disproving:
        // other.happens_before_or_at_the_same_time(this) &&
        // same_site(other)

        // We'll generate that boolean expression in DNF form, and
        // attempt to disprove every single clause.

        Expr same_site = const_true();
        for (size_t i = 0; i < site.size(); i++) {
            same_site = same_site && site[i] == other.site[i];
        }

        Expr may_assume = same_site && predicate && other.predicate;

        // First try to prove this term false. If we can, then these
        // two uses never alias and we don't need to worry about
        // anything temporal (e.g. one use writes to even rows and the
        // other use writes to odd rows).
        if (can_disprove(may_assume)) {
            return true;
        }

        // Now consider temporal constraints too.
        auto before = other.happens_before_or_at_the_same_time(*this);

        // Try to disprove each clause in turn.
        for (const auto &e : before) {
            if (!can_disprove(e && may_assume)) {
                return false;
            }
        }

        return true;
    }
};

std::vector<Use> get_times_of_all_uses(const Stmt &s, string buf) {
    class PolyhedralClock : public IRVisitor {
        using IRVisitor::visit;

        vector<Expr> clock;
        vector<Expr> predicate;
        const string &buf;

        void visit(const Block *op) override {
            int i = 0;
            clock.push_back(i);
            Stmt rest;
            do {
                op->first.accept(this);
                rest = op->rest;
                clock.back() = ++i;
            } while ((op = rest.as<Block>()));
            rest.accept(this);
            clock.pop_back();
        }

        void visit(const For *op) override {
            Expr loop_var = Variable::make(Int(32), op->name);
            Expr p = simplify(loop_var == op->min + clamp(aux(), 0, op->extent - 1));
            predicate.push_back(p);
            if (op->is_parallel()) {
                // No useful ordering, so add nothing to the clock
                op->body.accept(this);
            } else {
                clock.push_back(Variable::make(Int(32), op->name));
                op->body.accept(this);
                clock.pop_back();
            }
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
            uses.emplace_back(clock, site, predicate, name, is_store);
            for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                for (auto &e : uses.back().site) {
                    if (expr_uses_var(e, it->first)) {
                        e = Let::make(it->first, it->second, e);
                    }
                }

                if (expr_uses_var(uses.back().predicate, it->first)) {
                    uses.back().predicate = Let::make(it->first, it->second, uses.back().predicate);
                }
            }
        }

        vector<pair<string, Expr>> lets;

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
            clock.push_back(0);
            IRVisitor::visit(op);
            clock.back() = 1;
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

        const map<string, Function> &env;
    public:
        RemapArgs(const map<string, Function> &env) : env(env) {}
    } remap_args(env);

    Stmt stmt = remap_args.mutate(s);

    {
        // Check legality on a simplified version
        Stmt simpler = simplify(uniquify_variable_names(stmt));

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
                            if (check1) {
                                debug(0) << "\n *** Checking " << n1 << " before " << n2 << "\n";
                                u1.dump();
                                u2.dump();
                                check1 = u1.safely_before(u2);
                                debug(0) << (check1 ? "Success!\n" : "Failure!\n");
                            }
                            if (check2) {
                                debug(0) << "\n *** Checking " << n2 << " before " << n1 << "\n";
                                u1.dump();
                                u2.dump();
                                check2 = u2.safely_before(u1);
                                debug(0) << (check2 ? "Success!\n" : "Failure!\n");
                            }
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
