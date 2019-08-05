#ifndef HALIDE_BOUNDS_H
#define HALIDE_BOUNDS_H

/** \file
 * Methods for computing the upper and lower bounds of an expression,
 * and the regions of a function read or written by a statement.
 */

#include "IROperator.h"
#include "Interval.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

typedef std::map<std::pair<std::string, int>, Interval> FuncValueBounds;

/** Given an expression in some variables, and a map from those
 * variables to their bounds (in the form of (minimum possible value,
 * maximum possible value)), compute two expressions that give the
 * minimum possible value and the maximum possible value of this
 * expression. Max or min may be undefined expressions if the value is
 * not bounded above or below. If the expression is a vector, also
 * takes the bounds across the vector lanes and returns a scalar
 * result.
 *
 * This is for tasks such as deducing the region of a buffer
 * loaded by a chunk of code.
 */
Interval bounds_of_expr_in_scope(Expr expr,
                                 const Scope<Interval> &scope,
                                 const FuncValueBounds &func_bounds = FuncValueBounds(),
                                 bool const_bound = false);

/** Given a varying expression, try to find a constant that is either:
 * An upper bound (always greater than or equal to the expression), or
 * A lower bound (always less than or equal to the expression)
 * If it fails, returns an undefined Expr. */
enum class Direction {Upper, Lower};
Expr find_constant_bound(const Expr &e, Direction d,
                         const Scope<Interval> &scope = Scope<Interval>());

/** Find bounds for a varying expression that are either constants or
 * +/-inf. */
Interval find_constant_bounds(const Expr &e, const Scope<Interval> &scope);

/** Represents the bounds of a region of arbitrary dimension. Zero
 * dimensions corresponds to a scalar region. */
struct Box {
    /** The conditions under which this region may be touched. */
    Expr used;

    /** The bounds if it is touched. */
    std::vector<Interval> bounds;

    Box() = default;
    Box(size_t sz) : bounds(sz) {}
    Box(const std::vector<Interval> &b) : bounds(b) {}

    size_t size() const {return bounds.size();}
    bool empty() const {return bounds.empty();}
    Interval &operator[](size_t i) {return bounds[i];}
    const Interval &operator[](size_t i) const {return bounds[i];}
    void resize(size_t sz) {bounds.resize(sz);}
    void push_back(const Interval &i) {bounds.push_back(i);}

    /** Check if the used condition is defined and not trivially true. */
    bool maybe_unused() const {return used.defined() && !is_one(used);}

    friend std::ostream& operator<<(std::ostream& stream, const Box& b) {
        stream << "{";
        for (size_t dim = 0; dim < b.size(); dim++) {
            if (dim > 0) {
                stream << ", ";
            }
            stream << "[" << b[dim].min << ", " << b[dim].max << "]";
        }
        stream << "}";
        return stream;
    }
};

/** Expand box a to encompass box b */
void merge_boxes(Box &a, const Box &b);

/** Test if box a could possibly overlap box b. */
bool boxes_overlap(const Box &a, const Box &b);

/** The union of two boxes */
Box box_union(const Box &a, const Box &b);

/** The intersection of two boxes */
Box box_intersection(const Box &a, const Box &b);

/** Test if box a provably contains box b */
bool box_contains(const Box &a, const Box &b);


/** Compute rectangular domains large enough to cover all the 'Call's
 * to each function that occurs within a given statement or
 * expression. This is useful for figuring out what regions of things
 * to evaluate. */
// @{
std::map<std::string, Box> boxes_required(Expr e,
                                          const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                                          const FuncValueBounds &func_bounds = FuncValueBounds());
std::map<std::string, Box> boxes_required(Stmt s,
                                          const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                                          const FuncValueBounds &func_bounds = FuncValueBounds());
// @}

/** Compute rectangular domains large enough to cover all the
 * 'Provides's to each function that occurs within a given statement
 * or expression. */
// @{
std::map<std::string, Box> boxes_provided(Expr e,
                                          const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                                          const FuncValueBounds &func_bounds = FuncValueBounds());
std::map<std::string, Box> boxes_provided(Stmt s,
                                          const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                                          const FuncValueBounds &func_bounds = FuncValueBounds());
// @}

/** Compute rectangular domains large enough to cover all the 'Call's
 * and 'Provides's to each function that occurs within a given
 * statement or expression. */
// @{
std::map<std::string, Box> boxes_touched(Expr e,
                                         const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                                         const FuncValueBounds &func_bounds = FuncValueBounds());
std::map<std::string, Box> boxes_touched(Stmt s,
                                         const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                                         const FuncValueBounds &func_bounds = FuncValueBounds());
// @}

/** Variants of the above that are only concerned with a single function. */
// @{
Box box_required(Expr e, std::string fn,
                 const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                 const FuncValueBounds &func_bounds = FuncValueBounds());
Box box_required(Stmt s, std::string fn,
                 const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                 const FuncValueBounds &func_bounds = FuncValueBounds());

Box box_provided(Expr e, std::string fn,
                 const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                 const FuncValueBounds &func_bounds = FuncValueBounds());
Box box_provided(Stmt s, std::string fn,
                 const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                 const FuncValueBounds &func_bounds = FuncValueBounds());

Box box_touched(Expr e, std::string fn,
                const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                const FuncValueBounds &func_bounds = FuncValueBounds());
Box box_touched(Stmt s, std::string fn,
                const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                const FuncValueBounds &func_bounds = FuncValueBounds());
// @}

/** Compute the maximum and minimum possible value for each function
 * in an environment. */
FuncValueBounds compute_function_value_bounds(const std::vector<std::string> &order,
                                              const std::map<std::string, Function> &env);

void bounds_test();

}  // namespace Internal
}  // namespace Halide

#endif
