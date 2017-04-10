#include "SimplifySpecializations.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Definition.h"
#include "IREquality.h"
#include "Param.h"
#include "Func.h"

#include <set>

namespace Halide{
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

void substitute_value_in_var(const string &var, Expr value, vector<Definition> &definitions) {
    for (Definition &def : definitions) {
        for (auto &def_arg : def.args()) {
            def_arg = simplify(substitute(var, value, def_arg));
        }
        for (auto &def_val : def.values()) {
            def_val = simplify(substitute(var, value, def_val));
        }
    }
}

class SimplifyUsingFact : public IRMutator {
public:
    using IRMutator::mutate;

    Expr mutate(Expr e) {
        if (e.type().is_bool()) {
            if (equal(fact, e) ||
                can_prove(!fact || e)) {
                // fact implies e
                return const_true();
            }
            if (equal(fact, !e) ||
                equal(!fact, e) ||
                can_prove(!fact || !e)) {
                // fact implies !e
                return const_false();
            }
        }
        return IRMutator::mutate(e);
    }

    Expr fact;
    SimplifyUsingFact(Expr f) : fact(f) {}
};

void simplify_using_fact(Expr fact, vector<Definition> &definitions) {
    for (Definition &def : definitions) {
        for (auto &def_arg : def.args()) {
            def_arg = simplify(SimplifyUsingFact(fact).mutate(def_arg));
        }
        for (auto &def_val : def.values()) {
            def_val = simplify(SimplifyUsingFact(fact).mutate(def_val));
        }
    }
}

vector<Definition> propagate_specialization_in_definition(Definition &def, const string &name) {
    vector<Definition> result;

    result.push_back(def);

    vector<Specialization> &specializations = def.specializations();

    // Prune specializations based on constants:
    // -- Any Specializations that have const-false as a condition
    // can never trigger; go ahead and prune them now to save time & energy
    // during later phases.
    // -- Once we encounter a Specialization that is const-true, no subsequent
    // Specializations can ever trigger (since we evaluate them in order), 
    // so erase them. (Note however that it is legal to call specialize(true)
    // multiple times, which is required to return the same handle, so leave in
    // subsequent true specializations; since this is an odd edge case, don't
    // bother trying to combine them into a single Definition here.)
    bool seen_const_true = false;
    for (auto it = specializations.begin(); it != specializations.end(); /*no-increment*/) {
        Expr old_c = it->condition;
        Expr c = simplify(it->condition);
        // Go ahead and save the simplified condition now
        it->condition = c;
        bool is_const_true = is_one(c);
        if (is_zero(c) || (seen_const_true && !is_const_true)) {
            debug(1) << "Erasing unreachable specialization (" 
                << old_c << ") -> (" << c << ") for function \"" << name << "\"\n";
            it = specializations.erase(it);
        } else {
            it++;
        }
        seen_const_true |= is_const_true;
    }

    for (size_t i = specializations.size(); i > 0; i--) {
        Expr c = specializations[i-1].condition;
        Definition &s_def = specializations[i-1].definition;
        const EQ *eq = c.as<EQ>();
        const Variable *var = eq ? eq->a.as<Variable>() : c.as<Variable>();

        vector<Definition> s_result = propagate_specialization_in_definition(s_def, name);

        if (var && eq) {
            // Then case
            substitute_value_in_var(var->name, eq->b, s_result);

            // Else case
            if (eq->b.type().is_bool()) {
                substitute_value_in_var(var->name, !eq->b, result);
            }
        } else if (var) {
            // Then case
            substitute_value_in_var(var->name, const_true(), s_result);

            // Else case
            substitute_value_in_var(var->name, const_false(), result);
        } else {
            simplify_using_fact(c, s_result);
            simplify_using_fact(!c, result);
        }

        result.insert(result.end(), s_result.begin(), s_result.end());
    }

    return result;
}

}



void simplify_specializations(map<string, Function> &env) {
    for (auto &iter : env) {
        Function &func = iter.second;
        propagate_specialization_in_definition(func.definition(), func.name());
    }
}

namespace {

uint16_t vector_store_lanes = 0;

int my_trace(void *user_context, const halide_trace_event_t *ev) {
    if (ev->event == halide_trace_store) {
        if (ev->type.lanes > 1) {
            vector_store_lanes = ev->type.lanes;
        } else {
            vector_store_lanes = 0;
        }
    }
    return 0;
}

}  // namespace

void simplify_specializations_test() {
    Var x, y;
    Param<int> p;
    Expr const_false = Expr(0) == Expr(1);
    Expr const_true = Expr(0) == Expr(0);
    Expr different_const_true = Expr(1) == Expr(1);

    {
        // Check that we aggressively prune specialize(const-false)
        Func f;
        f(x) = x;
        f.specialize(p == 0).vectorize(x, 32);      // will *not* be pruned
        f.specialize(const_false).vectorize(x, 8);  // will be pruned
        f.vectorize(x, 4);                          // default case, not a specialization

        internal_assert(f.function().definition().specializations().size() == 2);

        map<string, Function> env;
        env.insert({f.function().name(), f.function()});
        simplify_specializations(env);

        const auto &s = f.function().definition().specializations();
        internal_assert(s.size() == 1);
        // should be (something) == 0
        internal_assert(s[0].condition.as<EQ>() && is_zero(s[0].condition.as<EQ>()->b));

        f.set_custom_trace(&my_trace);
        f.trace_stores();

        vector_store_lanes = 0;
        p.set(0);
        f.realize(100);
        internal_assert(vector_store_lanes == 32);

        vector_store_lanes = 0;
        p.set(42);  // just a nonzero value
        f.realize(100);
        internal_assert(vector_store_lanes == 4);
    }
    {
        // Check that we aggressively prune all specializations after specialize(const-true)
        // except for other occurrences of specialize(const-true)
        Func f;
        f(x) = x;
        f.specialize(p == 0).vectorize(x, 32);      // will *not* be pruned
        f.specialize(const_true).vectorize(x, 16);  // will *not* be pruned
        f.specialize(const_false).vectorize(x, 4);  // will be pruned
        f.specialize(p == 42).vectorize(x, 8);      // will be pruned
        f.specialize(const_true);                   // dupe of call above, won't add new specialization
        // Note that specialize() will return the same schedule for subsequent
        // calls with the same Expr, but doesn't guarantee that all Exprs
        // that evaluate to the same value collapse. Use a deliberately-
        // different Expr here to check that we don't elide these.
        f.specialize(different_const_true);         // will *not* be pruned

        internal_assert(f.function().definition().specializations().size() == 5);

        map<string, Function> env;
        env.insert({f.function().name(), f.function()});
        simplify_specializations(env);

        const auto &s = f.function().definition().specializations();
        internal_assert(s.size() == 3);
        // should be (something) == 0
        internal_assert(s[0].condition.as<EQ>() && is_zero(s[0].condition.as<EQ>()->b));
        internal_assert(is_one(s[1].condition));
        internal_assert(is_one(s[2].condition));

        f.set_custom_trace(&my_trace);
        f.trace_stores();

        vector_store_lanes = 0;
        p.set(42);  // Chosen to ensure pruned branch is pruned
        f.realize(100);
        internal_assert(vector_store_lanes == 16);

        vector_store_lanes = 0;
        p.set(0);
        f.realize(100);
        internal_assert(vector_store_lanes == 32);
    }

    std::cout << "SimplifySpecializations test passed" << std::endl;
}

}
}
