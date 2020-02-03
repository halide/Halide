#ifndef APPLY_SPLIT_H
#define APPLY_SPLIT_H

/** \file
 *
 * Defines method that returns a list of let stmts, substitutions, and
 * predicates to be added given a split schedule.
 */

#include <map>
#include <utility>
#include <vector>

#include "IR.h"
#include "Schedule.h"

namespace Halide {
namespace Internal {

struct ApplySplitResult {
    // If type is "Substitution", then this represents a substitution of
    // variable "name" to value. If type is "LetStmt", we should insert a new
    // let stmt defining "name" with value "value". If type is "Predicate", we
    // should ignore "name" and the predicate is "value".

    std::string name;
    Expr value;

    enum Type { Substitution = 0,
                LetStmt,
                Predicate };
    Type type;

    ApplySplitResult(const std::string &n, Expr val, Type t)
        : name(n), value(val), type(t) {
    }
    ApplySplitResult(Expr val)
        : name(""), value(val), type(Predicate) {
    }

    bool is_substitution() const {
        return (type == Substitution);
    }
    bool is_let() const {
        return (type == LetStmt);
    }
    bool is_predicate() const {
        return (type == Predicate);
    }
};

/** Given a Split schedule on a definition (init or update), return a list of
 * of predicates on the definition, substitutions that needs to be applied to
 * the definition (in ascending order of application), and let stmts which
 * defined the values of variables referred by the predicates and substitutions
 * (ordered from innermost to outermost let). */
std::vector<ApplySplitResult> apply_split(
    const Split &split, bool is_update, std::string prefix,
    std::map<std::string, Expr> &dim_extent_alignment);

/** Compute the loop bounds of the new dimensions resulting from applying the
 * split schedules using the loop bounds of the old dimensions. */
std::vector<std::pair<std::string, Expr>> compute_loop_bounds_after_split(
    const Split &split, std::string prefix);

}  // namespace Internal
}  // namespace Halide

#endif
