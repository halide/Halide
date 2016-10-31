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

/** Return a list of let stmts, substitutions, and predicates given a split schedule. */
ApplySplitResult apply_split(const Split &split, bool is_update, std::string prefix,
                             std::map<std::string, Expr> &dim_extent_alignment);

ApplySplitResult apply_splits(
    const std::vector<Split> &splits, bool is_update, std::string prefix,
    std::map<std::string, Expr> &dim_extent_alignment);

std::vector<std::pair<std::string, Expr>> compute_bounds_after_split(
    const Split &split, std::string prefix);

}
}

#endif
