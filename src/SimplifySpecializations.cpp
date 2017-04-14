#include "SimplifySpecializations.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Definition.h"
#include "IREquality.h"

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
    // so erase them.
    bool seen_const_true = false;
    for (auto it = specializations.begin(); it != specializations.end(); /*no-increment*/) {
        Expr old_c = it->condition;
        Expr c = simplify(it->condition);
        // Go ahead and save the simplified condition now
        it->condition = c;
        if (is_zero(c) || seen_const_true) {
            debug(1) << "Erasing unreachable specialization (" 
                << old_c << ") -> (" << c << ") for function \"" << name << "\"\n";
            it = specializations.erase(it);
        } else {
            it++;
        }
        seen_const_true |= is_one(c);
    }

    // If the final Specialization is const-true, then the default schedule
    // for the definition will never be run: replace the definition's main
    // schedule with the one from the final Specialization and prune it from 
    // the list. This may leave the list of Specializations empty.
    if (!specializations.empty() && is_one(specializations.back().condition)) {
        debug(1) << "Replacing default Schedule with const-true specialization for function \"" << name << "\"\n";
        const Definition s_def = specializations.back().definition;
        specializations.pop_back();

        def.predicate() = s_def.predicate();
        def.values() = s_def.values();
        def.args() = s_def.args();
        def.schedule() = s_def.schedule();
        // Append our sub-specializations to the Definition's list
        specializations.insert(specializations.end(), s_def.specializations().begin(), s_def.specializations().end());
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

}
}
