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
        for (auto &def_arg : def.explicit_args()) {
            def_arg = simplify(substitute(var, value, def_arg));
        }

        // TODO: This seems like it could convert an implicit variable to something else.
        std::set<Expr, IVarOrdering> new_implicits;
        for (auto &def_arg : def.implicit_args()) {
            new_implicits.insert(simplify(substitute(var, value, def_arg)));
        }
        def.implicit_args() = new_implicits;

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
        for (auto &def_arg : def.all_args()) {
            def_arg = simplify(SimplifyUsingFact(fact).mutate(def_arg));
        }
        for (auto &def_val : def.values()) {
            def_val = simplify(SimplifyUsingFact(fact).mutate(def_val));
        }
    }
}

vector<Definition> propagate_specialization_in_definition(Definition &def) {
    vector<Definition> result;

    result.push_back(def);

    vector<Specialization> &specializations = def.specializations();
    for (size_t i = specializations.size(); i > 0; i--) {
        Expr c = specializations[i-1].condition;
        Definition &s_def = specializations[i-1].definition;
        const EQ *eq = c.as<EQ>();
        const Variable *var = eq ? eq->a.as<Variable>() : c.as<Variable>();

        vector<Definition> s_result = propagate_specialization_in_definition(s_def);

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
        propagate_specialization_in_definition(func.definition());
    }
}

}
}
