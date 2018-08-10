#include "DerivativeUtils.h"

#include "CSE.h"
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

class VariableFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    bool find(const Expr &expr, const std::string &_var_name) {
        var_name = _var_name;
        found = false;
        expr.accept(this);
        return found;
    }

    void visit(const Variable *op) {
        if (op->name == var_name) {
            found = true;
        }
    }

private:
    std::string var_name;
    bool found;
};

bool has_variable(const Expr &expr, const std::string &name) {
    VariableFinder finder;
    return finder.find(expr, name);
}

class LetFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    bool find(const Expr &expr, const std::string &_var_name) {
        var_name = _var_name;
        found = false;
        expr.accept(this);
        return found;
    }

    void visit(const Let *op) {
        if (op->name == var_name) {
            found = true;
        }
        op->value->accept(this);
        op->body->accept(this);
    }

private:
    std::string var_name;
    bool found;
};

bool has_let_defined(const Expr &expr, const std::string &name) {
    LetFinder finder;
    return finder.find(expr, name);
}

class LetRemover : public IRMutator {
public:
    using IRMutator::visit;

    Expr remove(const Expr &expr) {
        return mutate(expr);
    }

    void visit(const Let *op) {
        expr = mutate(op->body);
    }
};

Expr remove_let_definitions(const Expr &expr) {
    LetRemover remover;
    return remover.remove(expr);
}

class VariableGatherer : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    std::vector<std::string> gather(const Expr &expr,
                                    const std::vector<std::string> &_filter) {
        filter = _filter;
        variables.clear();
        expr.accept(this);
        return variables;
    }

    void visit(const Variable *op) {
        for (const auto &pv : filter) {
            if (op->name == pv) {
                variables.push_back(op->name);
            }
        }
    }

private:
    std::vector<std::string> variables;
    std::vector<std::string> filter;
};

std::vector<std::string> gather_variables(const Expr &expr,
                                          const std::vector<std::string> &filter) {
    VariableGatherer gatherer;
    return gatherer.gather(expr, filter);
}

std::vector<std::string> gather_variables(const Expr &expr,
                                          const std::vector<Var> &filter) {
    std::vector<std::string> str_filter;
    str_filter.reserve(filter.size());
    for (const auto &var : filter) {
        str_filter.push_back(var.name());
    }
    return gather_variables(expr, str_filter);
}

class RVarGatherer : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    std::map<std::string, ReductionVariableInfo> gather(const Expr &expr) {
        expr.accept(this);
        return rvar_map;
    }

    std::map<std::string, ReductionVariableInfo> get_rvar_map() const {
        return rvar_map;
    }

    void visit(const Variable *op) {
        if (op->reduction_domain.defined()) {
            const std::vector<ReductionVariable> &domain =
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

private:
    std::map<std::string, ReductionVariableInfo> rvar_map;
};

std::map<std::string, ReductionVariableInfo> gather_rvariables(Expr expr) {
    return gather_rvariables(Tuple(expr));
}

std::map<std::string, ReductionVariableInfo> gather_rvariables(Tuple tuple) {
    RVarGatherer gatherer;
    for (const auto &expr : tuple.as_vector()) {
        gatherer.gather(expr);
    }
    return gatherer.get_rvar_map();
}

Expr add_let_expression(const Expr &expr,
                        const std::map<std::string, Expr> &let_var_mapping,
                        const std::vector<std::string> &let_variables) {
    // TODO: find a faster way to do this
    Expr ret = expr;
    ret = remove_let_definitions(ret);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &let_variable : let_variables) {
            if (has_variable(ret, let_variable) &&
                !has_let_defined(ret, let_variable)) {
                auto value = let_var_mapping.find(let_variable)->second;
                ret = Let::make(let_variable, value, ret);
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

    std::vector<Expr> sort(const Expr &expr);

    void visit(const Call *op);
    void visit(const Let *op);
    void visit(const Variable *op);
    void visit(const Select *op);

protected:
    void include(const Expr &e);

private:
    std::set<const IRNode *> visited_exprs;
    std::vector<Expr> expr_list;
    std::map<std::string, Expr> let_var_mapping;
};

std::vector<Expr> ExpressionSorter::sort(const Expr &e) {
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
    assert(let_var_mapping.find(op->name) == let_var_mapping.end());
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

std::vector<Expr> sort_expressions(const Expr &expr) {
    ExpressionSorter sorter;
    return sorter.sort(expr);
}

std::map<std::string, Box> inference_bounds(const std::vector<Func> &funcs,
                                            const std::vector<FuncBounds> &output_bounds) {
    assert(funcs.size() == output_bounds.size());
    std::vector<Function> functions;
    functions.reserve(funcs.size());
    for (const auto &func : funcs) {
        functions.push_back(func.function());
    }
    std::map<std::string, Function> env;
    for (const auto &func : functions) {
        std::map<std::string, Function> local_env = find_transitive_calls(func);
        env.insert(local_env.begin(), local_env.end());
    }
    Scope<Interval> scope;
    for (const auto &it : env) {
        Func func = Func(it.second);
        for (int i = 0; i < func.num_update_definitions(); i++) {
            std::map<std::string, ReductionVariableInfo> rvars =
                gather_rvariables(func.update_values(i));
            for (const auto &it : rvars) {
                Interval interval(it.second.min, it.second.min + it.second.extent - 1);
                scope.push(it.first, interval);
            }
        }
    }
    std::vector<std::string> order = realization_order(functions, env).first;

    std::map<std::string, Box> bounds;
    for (int i = 0; i < (int) funcs.size(); i++) {
        const Func &func = funcs[i];
        const FuncBounds &func_bounds = output_bounds[i];
        std::vector<Interval> func_bounds_interval;
        for (const auto &b : func_bounds) {
            func_bounds_interval.push_back(Interval(b.first, b.second));
        }
        Box func_bounds_box(func_bounds_interval);
        bounds[func.name()] = func_bounds_box;
    }
    // Traverse from the consumers to the producers
    for (auto it = order.rbegin(); it != order.rend(); it++) {
        Func func = Func(env[*it]);
        assert(bounds.find(*it) != bounds.end());
        const Box &current_bounds = bounds[*it];
        assert(func.args().size() == current_bounds.size());
        for (int i = 0; i < (int) current_bounds.size(); i++) {
            std::string arg = func.args()[i].name();
            scope.push(arg, current_bounds[i]);
        }
        for (int update_id = -1; update_id < func.num_update_definitions(); update_id++) {
            Tuple tuple = update_id == -1 ? func.values() : func.update_values(update_id);
            for (const auto &expr : tuple.as_vector()) {
                std::map<std::string, Box> update_bounds =
                    boxes_required(expr, scope);
                for (const auto &it : update_bounds) {
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

std::map<std::string, Box> inference_bounds(const Func &func,
                                            const FuncBounds &output_bounds) {
    return inference_bounds(std::vector<Func>{ func },
                            std::vector<FuncBounds>{ output_bounds });
}

std::vector<std::pair<Expr, Expr>> box_to_vector(const Box &bounds) {
    std::vector<std::pair<Expr, Expr>> ret;
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

std::vector<std::string> vars_to_strings(const std::vector<Var> &vars) {
    std::vector<std::string> ret;
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

    void visit(const Variable *op) {
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

std::pair<bool, Expr> solve_inverse(Expr expr,
                                    const std::string &new_var,
                                    const std::string &var) {
    expr = substitute_in_all_lets(simplify(expr));
    Interval interval = solve_for_outer_interval(expr, var);
    if (!interval.is_bounded()) {
        return std::make_pair(false, Expr());
    }
    Expr rmin = simplify(interval.min);
    Expr rmax = simplify(interval.max);
    Expr rextent = simplify(rmax - rmin + 1);

    const int64_t *extent_int = as_const_int(rextent);
    if (extent_int == nullptr) {
        return std::make_pair(false, Expr());
    }

    // For some reason interval.is_single_point() doesn't work
    if (extent_int != nullptr && *extent_int == 1) {
        return std::make_pair(true, rmin);
    }

    // Create a RDom to loop over the interval
    RDom r(0, int(*extent_int));
    Expr cond = substitute(var, rmin + r.x, expr.as<EQ>()->b);
    cond = substitute(new_var, Var(var), cond) == Var(var);
    r.where(cond);
    return std::make_pair(true, rmin + r.x);
}

struct BufferDimensionsFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    std::map<std::string, BufferInfo> find(const Func &func) {
        buffer_calls.clear();
        std::vector<Expr> vals = func.values().as_vector();
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

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->call_type == Call::Image) {
            if (op->image.defined()) {
                buffer_calls[op->name] = BufferInfo{
                    op->image.dimensions(),
                    op->type
                };
            } else {
                assert(op->param.defined());
                buffer_calls[op->name] = BufferInfo{
                    op->param.dimensions(),
                    op->type
                };
            }
        }
    }

    std::map<std::string, BufferInfo> buffer_calls;
};

std::map<std::string, BufferInfo> find_buffer_calls(const Func &func) {
    BufferDimensionsFinder finder;
    return finder.find(func);
}

struct ImplicitVariablesFinder : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    std::set<std::string> find(Expr expr) {
        implicit_variables.clear();
        expr.accept(this);
        return implicit_variables;
    }

    void visit(const Variable *op) {
        IRGraphVisitor::visit(op);
        if (Var::is_implicit(op->name)) {
            implicit_variables.insert(op->name);
        }
    }

    std::set<std::string> implicit_variables;
};

std::set<std::string> find_implicit_variables(Expr expr) {
    ImplicitVariablesFinder finder;
    return finder.find(expr);
}

Expr substitute_rdom_predicate(
    const std::string &name, const Expr &replacement, const Expr &expr) {
    Expr substituted = substitute(name, replacement, expr);
    std::map<std::string, ReductionVariableInfo> rvars =
        gather_rvariables(substituted);
    std::set<ReductionDomain, ReductionDomain::Compare> rdoms_set;
    for (const auto &it : rvars) {
        rdoms_set.insert(it.second.domain);
    }
    std::vector<ReductionDomain> rdoms;
    std::copy(rdoms_set.begin(), rdoms_set.end(), std::back_inserter(rdoms));
    for (auto &r : rdoms) {
        Expr predicate = r.predicate();
        predicate = substitute(name, replacement, predicate);
        r.set_predicate(predicate);
    }
    return substituted;
}

}  // namespace Internal
}  // namespace Halide
