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
    Interval(Expr min, Expr max) : min(min), max(max) {}
    Interval();
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

/** Call bounds_of_expr_in_scope with an empty scope */
Interval bounds_of_expr(Expr expr);

/** Compute rectangular domains large enough to cover all the 'Call's
 * to each function that occurs within a given statement. This is
 * useful for figuring out what regions of things to evaluate. */
std::map<std::string, Region> regions_called(Stmt s);

/** Compute rectangular domains large enough to cover all the
 * 'Provide's to each function that occur within a given
 * statement. This is useful for figuring out what region of a
 * function a scattering reduction (e.g. a histogram) might touch. */
std::map<std::string, Region> regions_provided(Stmt s);

/** Compute rectangular domains large enough to cover all Calls and
 * Provides to each function that occurs within a given statement */
std::map<std::string, Region> regions_touched(Stmt s);;

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

void bounds_test();
        
}
}

#endif
