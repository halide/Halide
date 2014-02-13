#ifndef HALIDE_BOUNDS_H
#define HALIDE_BOUNDS_H

#include "IR.h"
#include "Scope.h"
#include <vector>

/** \file
 * Methods for computing the upper and lower bounds of an expression,
 * and the regions of a function read or written by a statement.
 */

namespace Halide {
namespace Internal {

struct Interval {
    Expr min, max;
    Interval() {}
    Interval(Expr min, Expr max) : min(min), max(max) {}
};

typedef std::map<std::pair<std::string, int>, Interval> FuncValueBounds;

/** Given an expression in some variables, and a map from those
 * variables to their bounds (in the form of (minimum possible value,
 * maximum possible value)), compute two expressions that give the
 * minimum possible value and the maximum possible value of this
 * expression. Max or min may be undefined expressions if the value is
 * not bounded above or below.
 *
 * This is for tasks such as deducing the region of a buffer
 * loaded by a chunk of code.
 */
Interval bounds_of_expr_in_scope(Expr expr,
                                 const Scope<Interval> &scope,
                                 const FuncValueBounds &func_bounds = FuncValueBounds());


typedef std::vector<Interval> Box;

// Expand box a to encompass box b
void merge_boxes(Box &a, const Box &b);

/** Compute rectangular domains large enough to cover all the 'Call's
 * to each function that occurs within a given statement or
 * expression. This is useful for figuring out what regions of things
 * to evaluate. */
// @{
std::map<std::string, Box> boxes_required(Expr e, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_required(Stmt s, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_required(Expr e);
std::map<std::string, Box> boxes_required(Stmt s);

std::map<std::string, Box> boxes_required(Expr e, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_required(Stmt s, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_required(Expr e, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_required(Stmt s, const FuncValueBounds &func_bounds);
// @}

/** Compute rectangular domains large enough to cover all the
 * 'Provides's to each function that occurs within a given statement
 * or expression. */
// @{
std::map<std::string, Box> boxes_provided(Expr e, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_provided(Stmt s, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_provided(Expr e);
std::map<std::string, Box> boxes_provided(Stmt s);

std::map<std::string, Box> boxes_provided(Expr e, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_provided(Stmt s, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_provided(Expr e, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_provided(Stmt s, const FuncValueBounds &func_bounds);
// @}

/** Compute rectangular domains large enough to cover all the 'Call's
 * and 'Provides's to each function that occurs within a given
 * statement or expression. */
// @{
std::map<std::string, Box> boxes_touched(Expr e, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_touched(Stmt s, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_touched(Expr e);
std::map<std::string, Box> boxes_touched(Stmt s);

std::map<std::string, Box> boxes_touched(Expr e, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_touched(Stmt s, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_touched(Expr e, const FuncValueBounds &func_bounds);
std::map<std::string, Box> boxes_touched(Stmt s, const FuncValueBounds &func_bounds);
// @}

/** Variants of the above that are only concerned with a single function. */
// @{
Box box_required(Expr e, std::string fn, const Scope<Interval> &scope);
Box box_required(Stmt s, std::string fn, const Scope<Interval> &scope);
Box box_required(Expr e, std::string fn);
Box box_required(Stmt s, std::string fn);

Box box_provided(Expr e, std::string fn, const Scope<Interval> &scope);
Box box_provided(Stmt s, std::string fn, const Scope<Interval> &scope);
Box box_provided(Expr e, std::string fn);
Box box_provided(Stmt s, std::string fn);

Box box_touched(Expr e, std::string fn, const Scope<Interval> &scope);
Box box_touched(Stmt s, std::string fn, const Scope<Interval> &scope);
Box box_touched(Expr e, std::string fn);
Box box_touched(Stmt s, std::string fn);

Box box_required(Expr e, std::string fn, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
Box box_required(Stmt s, std::string fn, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
Box box_required(Expr e, std::string fn, const FuncValueBounds &func_bounds);
Box box_required(Stmt s, std::string fn, const FuncValueBounds &func_bounds);

Box box_provided(Expr e, std::string fn, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
Box box_provided(Stmt s, std::string fn, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
Box box_provided(Expr e, std::string fn, const FuncValueBounds &func_bounds);
Box box_provided(Stmt s, std::string fn, const FuncValueBounds &func_bounds);

Box box_touched(Expr e, std::string fn, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
Box box_touched(Stmt s, std::string fn, const Scope<Interval> &scope, const FuncValueBounds &func_bounds);
Box box_touched(Expr e, std::string fn, const FuncValueBounds &func_bounds);
Box box_touched(Stmt s, std::string fn, const FuncValueBounds &func_bounds);
// @}

/** Compute the maximum and minimum possible value for each function
 * in an environment. */
FuncValueBounds compute_function_value_bounds(const std::vector<std::string> &order,
                                              const std::map<std::string, Function> &env);

void bounds_test();

}
}

#endif
