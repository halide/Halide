#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "CSE.h"
#include "DerivativeUtils.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Monotonic.h"
#include "RealizationOrder.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

class StripLets : public IRMutator2 {
public:
    using IRMutator2::visit;
    Expr visit(const Let *op) override {
        return mutate(op->body);
    }
};

vector<string> gather_variables(const Expr &expr,
                                const vector<string> &filter) {

    class GatherVariables : public IRGraphVisitor {
    public:
        using IRGraphVisitor::visit;

        void visit(const Variable *op) override {
            for (const auto &pv : filter) {
                if (op->name == pv) {
                    variables.push_back(op->name);
                }
            }
        }

        GatherVariables(const vector<string> &f)
            : filter(f) {
        }

        vector<string> variables;
        const vector<string> &filter;
    } gatherer(filter);

    expr.accept(&gatherer);

    return gatherer.variables;
}

vector<string> gather_variables(const Expr &expr,
                                const vector<Var> &filter) {
    vector<string> str_filter;
    str_filter.reserve(filter.size());
    for (const auto &var : filter) {
        str_filter.push_back(var.name());
    }
    return gather_variables(expr, str_filter);
}

map<string, ReductionVariableInfo> gather_rvariables(Tuple tuple) {

    class GatherRVars : public IRGraphVisitor {
    public:
        using IRGraphVisitor::visit;

        void visit(const Variable *op) override {
            if (op->reduction_domain.defined()) {
                const vector<ReductionVariable> &domain =
                    op->reduction_domain.domain();
                for (int i = 0; i < (int) domain.size(); i++) {
                    const ReductionVariable &rv = domain[i];
                    if (rv.var == op->name) {
                        rvar_map[op->name] = ReductionVariableInfo{
                            rv.min, rv.extent, i, op->reduction_domain, op->name
                        };
                        return;
                    }
                }
                internal_error << "Unknown reduction variable encountered";
            }
        }

        map<string, ReductionVariableInfo> rvar_map;
    } gatherer;

    for (const auto &expr : tuple.as_vector()) {
        expr.accept(&gatherer);
    }
    return gatherer.rvar_map;
}

map<string, ReductionVariableInfo> gather_rvariables(Expr expr) {
    return gather_rvariables(Tuple(expr));
}

Expr add_let_expression(const Expr &expr,
                        const map<string, Expr> &let_var_mapping,
                        const vector<string> &let_variables) {
    // TODO: find a faster way to do this
    Expr ret = StripLets().mutate(expr);
    bool changed = true;
    vector<bool> injected(let_variables.size(), false);
    while (changed) {
        changed = false;
        for (size_t i = 0; i < let_variables.size(); i++) {
            const auto &let_variable = let_variables[i];
            if (!injected[i] &&
                expr_uses_var(ret, let_variable)) {
                auto value = let_var_mapping.find(let_variable)->second;
                ret = Let::make(let_variable, value, ret);
                injected[i] = true;
                changed = true;
            }
        }
    }
    return ret;
}

/** Gather the expression DAG and sort them in topological order
 */
class ExpressionSorter : public IRGraphVisitor {
public:
    using IRGraphVisitor::include;
    using IRGraphVisitor::visit;

    vector<Expr> sort(const Expr &expr);

    void visit(const Call *op) override;
    void visit(const Let *op) override;
    void visit(const Variable *op) override;
    void visit(const Select *op) override;

protected:
    void include(const Expr &e) override;

private:
    set<const IRNode *> visited_exprs;
    vector<Expr> expr_list;
    map<string, Expr> let_var_mapping;
};

vector<Expr> ExpressionSorter::sort(const Expr &e) {
    e.accept(this);
    expr_list.push_back(e);
    return expr_list;
}

void ExpressionSorter::visit(const Call *op) {
    // No point visiting the arguments of a Halide func or an image
    if (op->call_type == Call::Halide || op->call_type == Call::Image) {
        return;
    }

    for (const auto &arg : op->args) {
        include(arg);
    }
}

void ExpressionSorter::visit(const Let *op) {
    internal_assert(let_var_mapping.find(op->name) == let_var_mapping.end());
    let_var_mapping[op->name] = op->value;

    include(op->body);
}

void ExpressionSorter::visit(const Select *op) {
    // Ignore the condition since the derivative is zero
    include(op->true_value);
    include(op->false_value);
}

void ExpressionSorter::visit(const Variable *op) {
    auto it = let_var_mapping.find(op->name);
    if (it != let_var_mapping.end()) {
        include(it->second);
    }
}

void ExpressionSorter::include(const Expr &e) {
    IRGraphVisitor::include(e);
    if (visited_exprs.count(e.get()) == 0) {
        visited_exprs.insert(e.get());
        expr_list.push_back(e);
    }
}

vector<Expr> sort_expressions(const Expr &expr) {
    ExpressionSorter sorter;
    return sorter.sort(expr);
}

map<string, Box> inference_bounds(const vector<Func> &funcs,
                                  const vector<Box> &output_bounds) {
    internal_assert(funcs.size() == output_bounds.size());
    // Obtain all dependencies
    vector<Function> functions;
    functions.reserve(funcs.size());
    for (const auto &func : funcs) {
        functions.push_back(func.function());
    }
    map<string, Function> env;
    for (const auto &func : functions) {
        map<string, Function> local_env = find_transitive_calls(func);
        env.insert(local_env.begin(), local_env.end());
    }
    // Reduction variable scopes
    Scope<Interval> scope;
    for (const auto &it : env) {
        Func func = Func(it.second);
        for (int i = 0; i < func.num_update_definitions(); i++) {
            map<string, ReductionVariableInfo> rvars =
                gather_rvariables(func.update_values(i));
            for (const auto &it : rvars) {
                Interval interval(it.second.min, it.second.min + it.second.extent - 1);
                scope.push(it.first, interval);
            }
        }
    }
    // Sort functions
    vector<string> order = realization_order(functions, env).first;

    map<string, Box> bounds;
    // Set up bounds for outputs
    for (int i = 0; i < (int) funcs.size(); i++) {
        const Func &func = funcs[i];
        bounds[func.name()] = output_bounds[i];
    }
    // Traverse from the consumers to the producers
    for (auto it = order.rbegin(); it != order.rend(); it++) {
        Func func = Func(env[*it]);
        // We should already have the bounds of this function
        internal_assert(bounds.find(*it) != bounds.end());
        const Box &current_bounds = bounds[*it];
        internal_assert(func.args().size() == current_bounds.size());
        // We know the range for each argument of this function
        for (int i = 0; i < (int) current_bounds.size(); i++) {
            string arg = func.args()[i].name();
            scope.push(arg, current_bounds[i]);
        }
        // Propagate the bounds
        for (int update_id = -1; update_id < func.num_update_definitions(); update_id++) {
            // For each rhs expression
            Tuple tuple = update_id == -1 ? func.values() : func.update_values(update_id);
            for (const auto &expr : tuple.as_vector()) {
                // For all the immediate dependencies of this expression,
                // find the required ranges
                map<string, Box> update_bounds =
                    boxes_required(expr, scope);
                // Loop over the dependencies
                for (const auto &it : update_bounds) {
                    // Update the bounds, if not exists then create a new one
                    auto found = bounds.find(it.first);
                    if (found == bounds.end()) {
                        bounds[it.first] = it.second;
                    } else {
                        Box new_box = box_union(found->second, it.second);
                        bounds[it.first] = new_box;
                    }
                }
            }
        }
        for (int i = 0; i < (int) current_bounds.size(); i++) {
            scope.pop(func.args()[i].name());
        }
    }
    for (auto &it : bounds) {
        auto &bound = it.second;
        for (int i = 0; i < (int) bound.size(); i++) {
            bound[i].min = common_subexpression_elimination(simplify(bound[i].min));
            bound[i].max = common_subexpression_elimination(simplify(bound[i].max));
        }
    }
    return bounds;
}

map<string, Box> inference_bounds(const Func &func,
                                  const Box &output_bounds) {
    return inference_bounds(vector<Func>{ func },
                            vector<Box>{ output_bounds });
}

vector<pair<Expr, Expr>> box_to_vector(const Box &bounds) {
    vector<pair<Expr, Expr>> ret;
    ret.reserve(bounds.size());
    for (const auto &b : bounds.bounds) {
        ret.push_back({ b.min, b.max - b.min + 1 });
    }
    return ret;
}

bool equal(const RDom &bounds0, const RDom &bounds1) {
    if (bounds0.domain().domain().size() != bounds1.domain().domain().size()) {
        return false;
    }
    for (int bid = 0; bid < (int) bounds0.domain().domain().size(); bid++) {
        if (!equal(bounds0[bid].min(), bounds1[bid].min()) ||
            !equal(bounds0[bid].extent(), bounds1[bid].extent())) {
            return false;
        }
    }
    return true;
}

vector<string> vars_to_strings(const vector<Var> &vars) {
    vector<string> ret;
    ret.reserve(vars.size());
    for (const auto &var : vars) {
        ret.push_back(var.name());
    }
    return ret;
}

class RDomExtractor : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    ReductionDomain gather(const Expr &expr) {
        expr.accept(this);
        return rdom;
    }

    void visit(const Variable *op) override {
        if (op->reduction_domain.defined()) {
            rdom = op->reduction_domain;
        }
    }

private:
    ReductionDomain rdom;
};

ReductionDomain extract_rdom(const Expr &expr) {
    RDomExtractor extractor;
    return extractor.gather(expr);
}

pair<bool, Expr> solve_inverse(Expr expr,
                               const string &new_var,
                               const string &var) {
    expr = substitute_in_all_lets(simplify(expr));
    Interval interval = solve_for_outer_interval(expr, var);
    if (!interval.is_bounded()) {
        return { false, Expr() };
    }
    Expr rmin = simplify(interval.min);
    Expr rmax = simplify(interval.max);
    Expr rextent = simplify(rmax - rmin + 1);

    const int64_t *extent_int = as_const_int(rextent);
    if (extent_int == nullptr) {
        return { false, Expr() };
    }

    // For some reason interval.is_single_point() doesn't work
    if (extent_int != nullptr && *extent_int == 1) {
        return { true, rmin };
    }

    // Create a RDom to loop over the interval
    RDom r(0, int(*extent_int));
    Expr cond = substitute(var, rmin + r.x, expr.as<EQ>()->b);
    cond = substitute(new_var, Var(var), cond) == Var(var);
    r.where(cond);
    return { true, rmin + r.x };
}

struct BufferDimensionsFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    map<string, BufferInfo> find(const Func &func) {
        buffer_calls.clear();
        vector<Expr> vals = func.values().as_vector();
        for (Expr val : vals) {
            val.accept(this);
        }
        for (int update_id = 0; update_id < func.num_update_definitions(); update_id++) {
            vals = func.update_values(update_id).as_vector();
            for (Expr val : vals) {
                val.accept(this);
            }
        }
        return buffer_calls;
    }

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);
        if (op->call_type == Call::Image) {
            if (op->image.defined()) {
                buffer_calls[op->name] = BufferInfo{
                    op->image.dimensions(),
                    op->type
                };
            } else {
                internal_assert(op->param.defined());
                buffer_calls[op->name] = BufferInfo{
                    op->param.dimensions(),
                    op->type
                };
            }
        }
    }

    map<string, BufferInfo> buffer_calls;
};

map<string, BufferInfo> find_buffer_calls(const Func &func) {
    BufferDimensionsFinder finder;
    return finder.find(func);
}

struct ImplicitVariablesFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    set<string> find(Expr expr) {
        implicit_variables.clear();
        expr.accept(this);
        return implicit_variables;
    }

    void visit(const Variable *op) override {
        IRGraphVisitor::visit(op);
        if (Var::is_implicit(op->name)) {
            implicit_variables.insert(op->name);
        }
    }

    set<string> implicit_variables;
};

set<string> find_implicit_variables(Expr expr) {
    ImplicitVariablesFinder finder;
    return finder.find(expr);
}

Expr substitute_rdom_predicate(
    const string &name, const Expr &replacement, const Expr &expr) {
    Expr substituted = substitute(name, replacement, expr);
    map<string, ReductionVariableInfo> rvars =
        gather_rvariables(substituted);
    set<ReductionDomain, ReductionDomain::Compare> rdoms_set;
    for (const auto &it : rvars) {
        rdoms_set.insert(it.second.domain);
    }
    vector<ReductionDomain> rdoms;
    copy(rdoms_set.begin(), rdoms_set.end(), std::back_inserter(rdoms));
    for (auto &r : rdoms) {
        Expr predicate = r.predicate();
        predicate = substitute(name, replacement, predicate);
        r.set_predicate(predicate);
    }
    return substituted;
}

struct FunctionCallFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    bool find(const string &func_name_,
              const Expr &expr,
              const map<string, Expr> &let_var_mapping_) {
        func_name = func_name_;
        let_var_mapping = &let_var_mapping_;
        found = false;
        expr.accept(this);
        return found;
    }

    bool find(const Expr &expr,
              const map<string, Expr> &let_var_mapping_) {
        func_name = "";
        let_var_mapping = &let_var_mapping_;
        found = false;
        expr.accept(this);
        return found;
    }

    void visit(const Variable *var) override {
        if (!found) {
            auto it = let_var_mapping->find(var->name);
            if (it != let_var_mapping->end()) {
                found = find(func_name, it->second, *let_var_mapping);
            }
        }
    }

    void visit(const Call *op) override {
        if (op->call_type == Call::Image || op->call_type == Call::Halide) {
            if (func_name == "" || op->name == func_name) {
                found = true;
            }
        }
        if (!found) {
            IRGraphVisitor::visit(op);
        }
    }

    string func_name;
    map<string, Expr> const *let_var_mapping;
    bool found;
};

bool is_calling_function(
    const string &func_name, const Expr &expr,
    const map<string, Expr> &let_var_mapping) {
    FunctionCallFinder finder;
    return finder.find(func_name, expr, let_var_mapping);
}

bool is_calling_function(
    const Expr &expr,
    const map<string, Expr> &let_var_mapping) {
    FunctionCallFinder finder;
    return finder.find(expr, let_var_mapping);
}

}  // namespace Internal
}  // namespace Halide
