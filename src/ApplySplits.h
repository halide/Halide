#ifndef APPLY_SPLITS_H
#define APPLY_SPLITS_H

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
    std::vector<std::pair<std::string, Expr>> let_stmts;
    std::vector<std::pair<std::string, Expr>> substitutions;
    std::vector<Expr> predicates;
};

/** Given a Split schedule on a definition (init or update), return a list of
 * of predicates on the definition, a list of substitutions that needs to be
 * applied to the definition (in ascending order of application), and a list
 * of let stmts which defined the values of variables referred by the predicates
 * and substitutions (ordered from innermost to outermost let). */
ApplySplitResult apply_split(const Split &split, bool is_update, std::string prefix,
                             std::map<std::string, Expr> &dim_extent_alignment);

/** Apply a list of Split schedules (in ascending order) on a
 * definition (init or update). See \ref apply_split. */
ApplySplitResult apply_splits(
    const std::vector<Split> &splits, bool is_update, std::string prefix,
    std::map<std::string, Expr> &dim_extent_alignment);

/** Compute the loop bounds of the new dimensions resulting from applying the
 * split schedules using the loop bounds of the old dimensions. */
std::vector<std::pair<std::string, Expr>> compute_loop_bounds_after_split(
    const Split &split, std::string prefix);

}
}

#endif
