#include "ApplySplit.h"
#include "IR.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

vector<ApplySplitResult> apply_split(const Split &split, const string &prefix,
                                     map<string, Expr> &dim_extent_alignment) {
    vector<ApplySplitResult> result;

    Expr outer = Variable::make(Int(32), prefix + split.outer);
    Expr outer_max = Variable::make(Int(32), prefix + split.outer + ".loop_max");
    switch (split.split_type) {
    case Split::SplitVar: {
        Expr inner = Variable::make(Int(32), prefix + split.inner);
        Expr old_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
        Expr old_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
        Expr old_extent = (old_max - old_min) + 1;
        Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");

        dim_extent_alignment[split.inner] = split.factor;

        Expr base;
        if (split.align.defined()) {
            base = outer * split.factor;
        } else {
            base = outer * split.factor + old_min;
        }

        string base_name = prefix + split.inner + ".base";
        Expr base_var = Variable::make(Int(32), base_name);
        string old_var_name = prefix + split.old_var;
        Expr old_var = Variable::make(Int(32), old_var_name);

        map<string, Expr>::iterator iter = dim_extent_alignment.find(split.old_var);

        TailStrategy tail = split.tail;
        internal_assert(tail != TailStrategy::Auto)
            << "An explicit tail strategy should exist at this point\n";

        // When align is defined, tiles are anchored to align instead of to
        // old_min, so knowing that the factor divides the extent is not
        // enough to prove no boundary guard is needed: we additionally need
        // the tiling anchored at align to line up with the tiling anchored
        // at old_min, i.e. old_min and align must be congruent mod factor.
        bool alignment_matches_old_min = !split.align.defined() ||
                                         is_const_zero(simplify((old_min - split.align) % split.factor));

        if ((iter != dim_extent_alignment.end()) &&
            is_const_zero(simplify(iter->second % split.factor)) &&
            alignment_matches_old_min) {
            // We have proved that the split factor divides the
            // old extent. No need to adjust the base or add an if
            // statement.
            dim_extent_alignment[split.outer] = iter->second / split.factor;
        } else if (is_negative_const(split.factor) || is_const_zero(split.factor)) {
            user_error << "Can't split " << split.old_var << " by " << split.factor
                       << ". Split factors must be strictly positive\n";
        } else if (is_const_one(split.factor)) {
            // The split factor trivially divides the old extent,
            // but we know nothing new about the outer dimension.
        } else if (tail == TailStrategy::GuardWithIf ||
                   tail == TailStrategy::Predicate ||
                   tail == TailStrategy::PredicateLoads ||
                   tail == TailStrategy::PredicateStores) {
            // It's an exact split but we failed to prove that the
            // extent divides the factor. Use predication to guard
            // the calls and/or provides.

            Expr guarded;
            if (split.align.defined()) {
                // Because the un-rebased base block can start before old_min,
                // we must clamp both the minimum and maximum boundaries.
                guarded = promise_clamped(old_var, old_min, old_max);
            } else {
                // Legacy: structurally guaranteed to be >= old_min
                guarded = promise_clamped(old_var, old_var, old_max);
            }

            string guarded_var_name = prefix + split.old_var + ".guarded";
            Expr guarded_var = Variable::make(Int(32), guarded_var_name);

            ApplySplitResult::Type predicate_type, substitution_type;
            switch (tail) {
            case TailStrategy::GuardWithIf:
                substitution_type = ApplySplitResult::Substitution;
                predicate_type = ApplySplitResult::Predicate;
                break;
            case TailStrategy::Predicate:
                substitution_type = ApplySplitResult::Substitution;
                predicate_type = ApplySplitResult::Predicate;
                break;
            case TailStrategy::PredicateLoads:
                substitution_type = ApplySplitResult::SubstitutionInCalls;
                predicate_type = ApplySplitResult::PredicateCalls;
                break;
            case TailStrategy::PredicateStores:
                substitution_type = ApplySplitResult::SubstitutionInProvides;
                predicate_type = ApplySplitResult::PredicateProvides;
                break;
            default:
                break;
            }

            // Inject the if condition *after* doing the substitution
            // for the guarded version.
            result.emplace_back(prefix + split.old_var, guarded_var, substitution_type);
            result.emplace_back(guarded_var_name, guarded, ApplySplitResult::LetStmt);

            Expr guard_cond = likely(old_var <= old_max);
            if (split.align.defined()) {
                guard_cond = likely(old_var >= old_min && old_var <= old_max);
            }
            result.emplace_back(guard_cond, predicate_type);

        } else if (tail == TailStrategy::ShiftInwards) {
            // Adjust the base downwards to not compute off the
            // end of the realization.

            base = likely_if_innermost(base);
            if (split.align.defined()) {
                base = Max::make(base, old_min - split.align);
                base = Min::make(base, old_max + (1 - split.factor) - split.align);
            } else {
                base = Min::make(base, old_max + (1 - split.factor));
            }
        } else if (tail == TailStrategy::ShiftInwardsAndBlend) {
            // Unclamped base, saved before the Min/Max below adjust it. Used
            // to figure out how much (if at all) the boundary tile got
            // shifted, so we know which elements of it are redundant with a
            // neighboring tile and must be masked out rather than
            // recomputed (to avoid double-counting in a reduction).
            Expr old_base = base;
            base = likely(base);
            Expr mask;
            if (split.align.defined()) {
                // Because base is anchored to align instead of old_min, the
                // boundary tile can now be shifted at either end (whereas
                // without align only the max end is reachable, since base
                // is structurally >= old_min already). Elements shifted in
                // from the low end overlap the tile above (mask out the
                // last shift_low of them); elements shifted in from the
                // high end overlap the tile below (mask out the first
                // shift_high of them).
                Expr low_bound = old_min - split.align;
                Expr high_bound = old_max + (1 - split.factor) - split.align;
                Expr shift_low = low_bound - old_base;
                Expr shift_high = old_base - high_bound;
                base = Max::make(base, low_bound);
                base = Min::make(base, high_bound);
                Expr mask_low = inner < split.factor - shift_low;
                Expr mask_high = inner >= shift_high;
                mask = select(old_base < low_bound, mask_low,
                              select(old_base > high_bound, mask_high, likely(const_true())));
            } else {
                // Without align, base is structurally >= old_min (outer
                // starts at 0), so only the max end can ever be shifted.
                base = Min::make(base, old_max + (1 - split.factor));
                Expr unwanted_elems = (-old_extent) % split.factor;
                mask = inner >= unwanted_elems;
                mask = select(base == old_base, likely(const_true()), mask);
            }
            result.emplace_back(mask, ApplySplitResult::BlendProvides);
        } else if (tail == TailStrategy::RoundUpAndBlend) {
            Expr mask;
            if (split.align.defined()) {
                // Unlike ShiftInwardsAndBlend, the max end is intentionally
                // left unclamped here (RoundUp relies on padding, not on
                // shifting, to handle overrun at the max end) -- but the min
                // end still needs clamping: align can make the min-end tile
                // start before old_min, and unlike ShiftInwards/blend at the
                // max end, there's no padding below old_min to absorb an
                // underrun into, so it has to be prevented outright.
                //
                // The mask below compares old_base (the unclamped base)
                // against low_bound/high_bound directly, rather than
                // comparing outer against outer_min/outer_max: the latter
                // needs loop partitioning to split the loop into three
                // pieces (prologue/steady-state/epilogue) to stay correct,
                // and partition_loops doesn't reliably do that here when
                // both boundaries are data-dependent, silently dropping the
                // last tile. Comparing old_base against the bounds directly
                // is correct regardless of how (or whether) the loop gets
                // partitioned, matching the approach already proven correct
                // above for ShiftInwardsAndBlend.
                Expr old_base = base;
                Expr low_bound = old_min - split.align;
                Expr high_bound = old_max + (1 - split.factor) - split.align;
                Expr shift_low = low_bound - old_base;
                Expr shift_high = old_base - high_bound;
                base = Max::make(likely(base), low_bound);
                // The min end is clamped (shifted forward), so its overlap
                // is with the tile *above* -- same geometry as
                // ShiftInwardsAndBlend, mask out the trailing shift_low
                // elements. The max end is left unclamped, so shift_high
                // counts a genuine overrun past old_max with no
                // neighboring tile to defer to -- mask out the trailing
                // shift_high elements too (the opposite convention from
                // ShiftInwardsAndBlend's clamped max end, which instead
                // masks out the *leading* elements of a shifted-back tile).
                Expr mask_low = inner < split.factor - shift_low;
                Expr mask_high = inner < split.factor - shift_high;
                mask = select(old_base < low_bound, mask_low,
                              select(old_base > high_bound, mask_high, likely(const_true())));
            } else {
                Expr unwanted_elems = (-old_extent) % split.factor;
                Expr fresh_high = inner < split.factor - unwanted_elems;
                mask = select(outer < outer_max, likely(const_true()), fresh_high);
            }
            result.emplace_back(mask, ApplySplitResult::BlendProvides);
        } else {
            internal_assert(tail == TailStrategy::RoundUp);
        }

        // Add align back in last, after all tail-strategy clamping/masking is
        // done in terms of the unaligned base: this keeps align as a bare
        // top-level addend in the final expressions (so e.g. it can still
        // cancel algebraically against a matching subtraction elsewhere)
        // rather than being smeared into a Max/Min-clamped expression, while
        // letting the inner loop variable itself range over the simple,
        // often-constant [0, factor) instead of [align, align + factor).
        if (split.align.defined()) {
            base = base + split.align;
        }

        // Define the original variable as the base value computed above plus the inner loop variable.
        result.emplace_back(old_var_name, base_var + inner, ApplySplitResult::LetStmt);
        result.emplace_back(base_name, base, ApplySplitResult::LetStmt);
    } break;
    case Split::FuseVars: {
        // Define the inner and outer in terms of the fused var
        Expr fused = Variable::make(Int(32), prefix + split.old_var);
        Expr inner_min = Variable::make(Int(32), prefix + split.inner + ".loop_min");
        Expr inner_max = Variable::make(Int(32), prefix + split.inner + ".loop_max");
        Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");

        const Expr &factor = (inner_max - inner_min) + 1;
        Expr inner = fused % factor + inner_min;
        Expr outer = fused / factor + outer_min;

        result.emplace_back(prefix + split.inner, inner, ApplySplitResult::Substitution);
        result.emplace_back(prefix + split.outer, outer, ApplySplitResult::Substitution);
        result.emplace_back(prefix + split.inner, inner, ApplySplitResult::LetStmt);
        result.emplace_back(prefix + split.outer, outer, ApplySplitResult::LetStmt);

        // Maintain the known size of the fused dim if
        // possible. This is important for possible later splits.
        map<string, Expr>::iterator inner_dim = dim_extent_alignment.find(split.inner);
        map<string, Expr>::iterator outer_dim = dim_extent_alignment.find(split.outer);
        if (inner_dim != dim_extent_alignment.end() &&
            outer_dim != dim_extent_alignment.end()) {
            dim_extent_alignment[split.old_var] = inner_dim->second * outer_dim->second;
        }
    } break;
    case Split::RenameVar:
        result.emplace_back(prefix + split.old_var, outer, ApplySplitResult::Substitution);
        result.emplace_back(prefix + split.old_var, outer, ApplySplitResult::LetStmt);
        break;
    }

    return result;
}

vector<std::pair<string, Expr>> compute_loop_bounds_after_split(const Split &split, const string &prefix) {
    // Define the bounds on the split dimensions using the bounds on the function args.
    vector<std::pair<string, Expr>> let_stmts;

    Expr old_var_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
    Expr old_var_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
    switch (split.split_type) {
    case Split::SplitVar: {
        if (split.align.defined()) {
            Expr align = split.align;
            Expr outer_min = (old_var_min - align) / split.factor;
            Expr outer_max = (old_var_max - align) / split.factor;
            let_stmts.emplace_back(prefix + split.inner + ".loop_min", 0);
            let_stmts.emplace_back(prefix + split.inner + ".loop_max", split.factor - 1);
            let_stmts.emplace_back(prefix + split.outer + ".loop_min", outer_min);
            let_stmts.emplace_back(prefix + split.outer + ".loop_max", outer_max);
        } else {
            Expr inner_extent = split.factor;
            Expr outer_extent = (old_var_max - old_var_min + split.factor) / split.factor;
            let_stmts.emplace_back(prefix + split.inner + ".loop_min", 0);
            let_stmts.emplace_back(prefix + split.inner + ".loop_max", inner_extent - 1);
            let_stmts.emplace_back(prefix + split.outer + ".loop_min", 0);
            let_stmts.emplace_back(prefix + split.outer + ".loop_max", outer_extent - 1);
        }
    } break;
    case Split::FuseVars: {
        // Define bounds on the fused var using the bounds on the inner and outer
        Expr inner_min = Variable::make(Int(32), prefix + split.inner + ".loop_min");
        Expr inner_max = Variable::make(Int(32), prefix + split.inner + ".loop_max");
        Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");
        Expr outer_max = Variable::make(Int(32), prefix + split.outer + ".loop_max");
        Expr fused_extent = (inner_max - inner_min + 1) * (outer_max - outer_min + 1);
        let_stmts.emplace_back(prefix + split.old_var + ".loop_min", 0);
        let_stmts.emplace_back(prefix + split.old_var + ".loop_max", fused_extent - 1);
    } break;
    case Split::RenameVar:
        let_stmts.emplace_back(prefix + split.outer + ".loop_min", old_var_min);
        let_stmts.emplace_back(prefix + split.outer + ".loop_max", old_var_max);
        break;
    }

    return let_stmts;
}

}  // namespace Internal
}  // namespace Halide
