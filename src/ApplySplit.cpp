#include "ApplySplit.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide{
namespace Internal {

using std::map;
using std::string;
using std::vector;

vector<ApplySplitResult> apply_split(const Split &split, bool is_update, string prefix,
                                     map<string, Expr> &dim_extent_alignment) {
    vector<ApplySplitResult> result;

    Expr outer = Variable::make(Int(32), prefix + split.outer);
    Expr outer_max = Variable::make(Int(32), prefix + split.outer + ".loop_max");
    if (split.is_split()) {
        Expr inner = Variable::make(Int(32), prefix + split.inner);
        Expr old_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
        Expr old_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
        Expr old_extent = Variable::make(Int(32), prefix + split.old_var + ".loop_extent");

        dim_extent_alignment[split.inner] = split.factor;

        Expr base = outer * split.factor + old_min;
        string base_name = prefix + split.inner + ".base";
        Expr base_var = Variable::make(Int(32), base_name);
        string old_var_name = prefix + split.old_var;
        Expr old_var = Variable::make(Int(32), old_var_name);

        map<string, Expr>::iterator iter = dim_extent_alignment.find(split.old_var);

        TailStrategy tail = split.tail;
        internal_assert(tail != TailStrategy::Auto)
            << "An explicit tail strategy should exist at this point\n";

        if ((iter != dim_extent_alignment.end()) &&
            is_zero(simplify(iter->second % split.factor))) {
            // We have proved that the split factor divides the
            // old extent. No need to adjust the base or add an if
            // statement.
            dim_extent_alignment[split.outer] = iter->second / split.factor;
        } else if (is_negative_const(split.factor) || is_zero(split.factor)) {
            user_error << "Can't split " << split.old_var << " by " << split.factor
                       << ". Split factors must be strictly positive\n";
        } else if (is_one(split.factor)) {
            // The split factor trivially divides the old extent,
            // but we know nothing new about the outer dimension.
        } else if (tail == TailStrategy::GuardWithIf) {
            // It's an exact split but we failed to prove that the
            // extent divides the factor. Use predication.

            // Make a var representing the original var minus its
            // min. It's important that this is a single Var so
            // that bounds inference has a chance of understanding
            // what it means for it to be limited by the if
            // statement's condition.
            Expr rebased = outer * split.factor + inner;
            string rebased_var_name = prefix + split.old_var + ".rebased";
            Expr rebased_var = Variable::make(Int(32), rebased_var_name);

            result.push_back(ApplySplitResult(
                prefix + split.old_var, rebased_var + old_min, ApplySplitResult::Substitution));

            // Tell Halide to optimize for the case in which this
            // condition is true by partitioning some outer loop.
            Expr cond = likely(rebased_var < old_extent);
            result.push_back(ApplySplitResult(cond));
            result.push_back(ApplySplitResult(rebased_var_name, rebased, ApplySplitResult::LetStmt));

        } else if (tail == TailStrategy::ShiftInwards) {
            // Adjust the base downwards to not compute off the
            // end of the realization.

            // We'll only mark the base as likely (triggering a loop
            // partition) if we're at or inside the innermost
            // non-trivial loop.
            base = likely_if_innermost(base);

            base = Min::make(base, old_max + (1 - split.factor));
        } else {
            internal_assert(tail == TailStrategy::RoundUp);
        }

        // Substitute in the new expression for the split variable ...
        result.push_back(ApplySplitResult(old_var_name, base_var + inner, ApplySplitResult::Substitution));
        // ... but also define it as a let for the benefit of bounds inference.
        result.push_back(ApplySplitResult(old_var_name, base_var + inner, ApplySplitResult::LetStmt));
        result.push_back(ApplySplitResult(base_name, base, ApplySplitResult::LetStmt));

    } else if (split.is_fuse()) {
        // Define the inner and outer in terms of the fused var
        Expr fused = Variable::make(Int(32), prefix + split.old_var);
        Expr inner_min = Variable::make(Int(32), prefix + split.inner + ".loop_min");
        Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");
        Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");

        // If the inner extent is zero, the loop will never be
        // entered, but the bounds expressions lifted out might
        // contain divides or mods by zero. In the cases where
        // simplification of inner and outer matter, inner_extent
        // is a constant, so the max will simplify away.
        Expr factor = max(inner_extent, 1);
        Expr inner = fused % factor + inner_min;
        Expr outer = fused / factor + outer_min;

        result.push_back(ApplySplitResult(prefix + split.inner, inner, ApplySplitResult::Substitution));
        result.push_back(ApplySplitResult(prefix + split.outer, outer, ApplySplitResult::Substitution));
        result.push_back(ApplySplitResult(prefix + split.inner, inner, ApplySplitResult::LetStmt));
        result.push_back(ApplySplitResult(prefix + split.outer, outer, ApplySplitResult::LetStmt));

        // Maintain the known size of the fused dim if
        // possible. This is important for possible later splits.
        map<string, Expr>::iterator inner_dim = dim_extent_alignment.find(split.inner);
        map<string, Expr>::iterator outer_dim = dim_extent_alignment.find(split.outer);
        if (inner_dim != dim_extent_alignment.end() &&
            outer_dim != dim_extent_alignment.end()) {
            dim_extent_alignment[split.old_var] = inner_dim->second*outer_dim->second;
        }
    } else {
        // rename or purify
        result.push_back(ApplySplitResult(prefix + split.old_var, outer, ApplySplitResult::Substitution));
        result.push_back(ApplySplitResult(prefix + split.old_var, outer, ApplySplitResult::LetStmt));
    }

    return result;
}

vector<std::pair<string, Expr>> compute_loop_bounds_after_split(const Split &split, string prefix) {
    // Define the bounds on the split dimensions using the bounds
    // on the function args. If it is a purify, we should use the bounds
    // from the dims instead.

    vector<std::pair<string, Expr>> let_stmts;

    Expr old_var_extent = Variable::make(Int(32), prefix + split.old_var + ".loop_extent");
    Expr old_var_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
    Expr old_var_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
    if (split.is_split()) {
        Expr inner_extent = split.factor;
        Expr outer_extent = (old_var_max - old_var_min + split.factor)/split.factor;
        let_stmts.push_back({ prefix + split.inner + ".loop_min", 0 });
        let_stmts.push_back({ prefix + split.inner + ".loop_max", inner_extent-1 });
        let_stmts.push_back({ prefix + split.inner + ".loop_extent", inner_extent });
        let_stmts.push_back({ prefix + split.outer + ".loop_min", 0 });
        let_stmts.push_back({ prefix + split.outer + ".loop_max", outer_extent-1 });
        let_stmts.push_back({ prefix + split.outer + ".loop_extent", outer_extent });
    } else if (split.is_fuse()) {
        // Define bounds on the fused var using the bounds on the inner and outer
        Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");
        Expr outer_extent = Variable::make(Int(32), prefix + split.outer + ".loop_extent");
        Expr fused_extent = inner_extent * outer_extent;
        let_stmts.push_back({ prefix + split.old_var + ".loop_min", 0 });
        let_stmts.push_back({ prefix + split.old_var + ".loop_max", fused_extent - 1 });
        let_stmts.push_back({ prefix + split.old_var + ".loop_extent", fused_extent });
    } else if (split.is_rename()) {
        let_stmts.push_back({ prefix + split.outer + ".loop_min", old_var_min });
        let_stmts.push_back({ prefix + split.outer + ".loop_max", old_var_max });
        let_stmts.push_back({ prefix + split.outer + ".loop_extent", old_var_extent });
    }
    // Do nothing for purify

    return let_stmts;
}


}
}
