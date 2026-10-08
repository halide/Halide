#include "SimplifySpecializations.h"
#include "Definition.h"
#include "Function.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

#include <algorithm>

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

template<typename Fn>
void mutate_definition_exprs(Definition &def, Fn &&mutate_expr, bool include_rvars = true) {
    for (Expr &def_arg : def.args()) {
        def_arg = mutate_expr(def_arg);
    }
    for (Expr &def_val : def.values()) {
        def_val = mutate_expr(def_val);
    }
    if (def.predicate().defined()) {
        def.predicate() = mutate_expr(def.predicate());
    }
    if (include_rvars) {
        for (ReductionVariable &rv : def.schedule().rvars()) {
            if (rv.min.defined()) {
                rv.min = mutate_expr(rv.min);
            }
            if (rv.extent.defined()) {
                rv.extent = mutate_expr(rv.extent);
            }
        }
    }
}

void substitute_value_in_var(const string &var, const Expr &value, vector<Definition> &definitions) {
    for (Definition &def : definitions) {
        mutate_definition_exprs(def, [&](const Expr &e) {
            return simplify(substitute(var, value, e));
        });
    }
}

void simplify_using_fact(const Expr &fact, vector<Definition> &definitions) {
    for (Definition &def : definitions) {
        mutate_definition_exprs(def, [&](const Expr &expr) {
            return simplify(mutate_with(expr, [&](auto *self, const Expr &e) {
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
                return self->mutate_base(e);
            }));
        });
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
        if (is_const_zero(c) || seen_const_true) {
            debug(1) << "Erasing unreachable specialization ("
                     << old_c << ") -> (" << c << ") for function \"" << name << "\"\n";
            it = specializations.erase(it);
        } else {
            it++;
        }
        seen_const_true |= is_const_one(c);
    }

    // If the final Specialization is const-true, then the default schedule
    // for the definition will never be run: replace the definition's main
    // schedule with the one from the final Specialization and prune it from
    // the list. This may leave the list of Specializations empty.
    if (!specializations.empty() && is_const_one(specializations.back().condition) && specializations.back().failure_message.empty()) {
        debug(1) << "Replacing default Schedule with const-true specialization for function \"" << name << "\"\n";
        const Definition s_def = specializations.back().definition;
        specializations.pop_back();

        // The values/args/predicate need to be copied over since they might have
        // been simplified based on the predicate of the specialization.
        def.values() = s_def.values();
        def.args() = s_def.args();
        def.predicate() = s_def.predicate();

        // Copy over the schedule.
        def.schedule() = s_def.schedule().get_copy();

        // Append our sub-specializations to the Definition's list
        specializations.insert(specializations.end(), s_def.specializations().begin(), s_def.specializations().end());
    }

    for (auto &s : reverse_view(specializations)) {
        Expr c = s.condition;
        Definition &s_def = s.definition;
        const EQ *eq = c.as<EQ>();
        const Variable *var = eq ? eq->a.as<Variable>() : c.as<Variable>();

        internal_assert(s_def.defined());

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

bool same_rvar(const ReductionVariable &a, const ReductionVariable &b) {
    return a.var == b.var && equal(a.min, b.min) && equal(a.extent, b.extent);
}

bool no_rvars_differ(const vector<Definition> &defs) {
    if (defs.size() <= 1) {
        return true;
    }
    const auto &first = defs[0].schedule().rvars();
    return std::all_of(defs.begin() + 1, defs.end(), [&](const Definition &d) {
        const auto &other = d.schedule().rvars();
        return std::equal(first.begin(), first.end(), other.begin(), other.end(), same_rvar);
    });
}

void uniquify_specialization_rvars(vector<Definition> &defs) {
    if (no_rvars_differ(defs)) {
        return;
    }

    for (size_t i = 0; i < defs.size(); i++) {
        Definition &d = defs[i];
        if (d.schedule().rvars().empty()) {
            continue;
        }
        vector<Split> rename_splits;
        map<string, Expr> var_renames;
        for (ReductionVariable &rv : d.schedule().rvars()) {
            string old_var = rv.var;
            string new_var = "s" + std::to_string(i) + "." + old_var;
            rv.var = new_var;
            var_renames[old_var] = Variable::make(Int(32), new_var);
            rename_splits.push_back(
                Split{new_var, old_var, "", 0, Expr(), true, TailStrategy::RoundUp, Split::RenameVar});
        }
        mutate_definition_exprs(
            d, [&](const Expr &e) { return substitute(var_renames, e); }, /*include_rvars=*/false);
        auto &splits = d.schedule().splits();
        splits.insert(splits.begin(), rename_splits.begin(), rename_splits.end());
    }
}

void simplify_stage_specializations(Definition &def, const string &name) {
    vector<Definition> defs = propagate_specialization_in_definition(def, name);
    uniquify_specialization_rvars(defs);
}

}  // namespace

void simplify_specializations(map<string, Function> &env) {
    for (auto &[name, func] : env) {
        simplify_stage_specializations(func.definition(), name);
        for (size_t i = 0; i < func.updates().size(); i++) {
            simplify_stage_specializations(func.update(i), name);
        }
        for (size_t i = 0; i < func.updates().size(); i++) {
            propagate_specialization_in_definition(func.update((int)i), func.name());
        }
    }
}

}  // namespace Internal
}  // namespace Halide
