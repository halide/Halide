#ifndef HALIDE_INTERNAL_DERIVATIVE_UTILS_H
#define HALIDE_INTERNAL_DERIVATIVE_UTILS_H

#include <set>

#include "Bounds.h"
#include "Derivative.h"
#include "Expr.h"
#include "RDom.h"
#include "Scope.h"
#include "Var.h"

namespace Halide {
namespace Internal {

/**
 * Remove all let definitions of expr
 */
Expr remove_let_definitions(const Expr &expr);

/**
 * Return a list of variables that expr depends on and are in the filter
 */
std::vector<std::string> gather_variables(const Expr &expr,
                                          const std::vector<std::string> &filter);
std::vector<std::string> gather_variables(const Expr &expr,
                                          const std::vector<Var> &filter);

/**
 * Return a list of reduction variables the expression or tuple depends on
 */
struct ReductionVariableInfo {
    Expr min, extent;
    int index;
    ReductionDomain domain;
    std::string name;
};
std::map<std::string, ReductionVariableInfo> gather_rvariables(Expr expr);
std::map<std::string, ReductionVariableInfo> gather_rvariables(Tuple tuple);
/**
 * Add necessary let expressions to expr
 */
Expr add_let_expression(const Expr &expr,
                        const std::map<std::string, Expr> &let_var_mapping,
                        const std::vector<std::string> &let_variables);
/**
 * Topologically sort the expression graph expressed by expr
 */
std::vector<Expr> sort_expressions(const Expr &expr);
/**
 * Compute the bounds of funcs
 */
std::map<std::string, Box> inference_bounds(const std::vector<Func> &funcs,
                                            const std::vector<Box> &output_bounds);
std::map<std::string, Box> inference_bounds(const Func &func,
                                            const Box &output_bounds);
/**
 * Convert Box to vector of (min, extent)
 */
std::vector<std::pair<Expr, Expr>> box_to_vector(const Box &bounds);
/**
 * Return true if bounds0 and bounds1 represent the same bounds.
 */
bool equal(const RDom &bounds0, const RDom &bounds1);
/**
 * Return a list of variable names
 */
std::vector<std::string> vars_to_strings(const std::vector<Var> &vars);
/**
 * Return the reduction domain used by expr
 */
ReductionDomain extract_rdom(const Expr &expr);
/**
 * expr is new_var == f(var), solve for var == g(new_var)
 * if multiple new_var correponds to same var, introduce a RDom
 */
std::pair<bool, Expr> solve_inverse(Expr expr,
                                    const std::string &new_var,
                                    const std::string &var);
/**
 * Find all calls to image buffers in the function
 */
struct BufferInfo {
    int dimension;
    Type type;
};
std::map<std::string, BufferInfo> find_buffer_calls(const Func &func);
/**
 * Find all implicit variables in expr
 */
std::set<std::string> find_implicit_variables(Expr expr);
/**
 * Substitute the variable. Also replace all occurence in rdom.where() predicates.
 */
Expr substitute_rdom_predicate(
    const std::string &name, const Expr &replacement, const Expr &expr);
/**
 * Return true if expr contains call to func_name
 */
bool is_calling_function(
    const std::string &func_name, const Expr &expr,
    const std::map<std::string, Expr> &let_var_mapping);
/**
 * Return true if expr depends on any function or buffer
 */
bool is_calling_function(
    const Expr &expr,
    const std::map<std::string, Expr> &let_var_mapping);

}  // namespace Internal
}  // namespace Halide

#endif
