#ifndef HALIDE_BOUNDS_H
#define HALIDE_BOUNDS_H

#include "IR.h"
#include "IRVisitor.h"
#include "Scope.h"
#include <utility>
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
Interval bounds_of_expr_in_scope(Expr expr, const Scope<Interval> &scope);


#if 0

std::map<std::string, Region> regions_called(Stmt s);

/** Compute rectangular domains large enough to cover all the
 * 'Provide's to each function that occur within a given
 * statement. This is useful for figuring out what region of a
 * function a scattering reduction (e.g. a histogram) might touch. */
std::map<std::string, Region> regions_provided(Stmt s);

/** Compute rectangular domains large enough to cover all Calls and
 * Provides to each function that occurs within a given statement */
std::map<std::string, Region> regions_touched(Stmt s);

/** Compute a rectangular domain large enough to cover all Calls and
 * Provides to a given function */
Region region_touched(Stmt s, const std::string &func);

/** Compute a rectangular domain large enough to cover all Provides to
 * a given function */
Region region_provided(Stmt s, const std::string &func);

/** Compute a rectangular domain large enough to cover all Calls
 * to a given function */
Region region_called(Stmt s, const std::string &func);

/** Compute the smallest bounding box that contains two regions */
Region region_union(const Region &, const Region &);

#endif

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
// @}

/** Compute rectangular domains large enough to cover all the
 * 'Provides's to each function that occurs within a given statement
 * or expression. */
// @{
std::map<std::string, Box> boxes_provided(Expr e, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_provided(Stmt s, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_provided(Expr e);
std::map<std::string, Box> boxes_provided(Stmt s);
// @}

/** Compute rectangular domains large enough to cover all the 'Call's
 * and 'Provides's to each function that occurs within a given
 * statement or expression. */
// @{
std::map<std::string, Box> boxes_touched(Expr e, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_touched(Stmt s, const Scope<Interval> &scope);
std::map<std::string, Box> boxes_touched(Expr e);
std::map<std::string, Box> boxes_touched(Stmt s);
// @}

/** Variants of the above that are only concerned with a single function */
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
// @}

void bounds_test();

}
}

#endif
