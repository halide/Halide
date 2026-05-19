#ifndef HALIDE_INLINE_H
#define HALIDE_INLINE_H

/** \file
 * Methods for replacing calls to functions with their definitions.
 */

#include <map>
#include <utility>
#include <vector>

#include "Expr.h"
#include "Function.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

/** A mutator that inlines a set of pure functions wherever they're called
 * in an IR. Usage:
 *
 *   Inliner inliner;
 *   for (Function f : to_inline) inliner.add(f);
 *   Stmt result = inliner.do_inlining(stmt);
 *   // or: Expr result = inliner.do_inlining(expr);
 *
 * For a single function, Inliner(f) gives an equivalent shortcut and
 * `inline_function(s/e, f)` packages it up further.
 *
 * For best performance, add() the functions in consumer-first
 * (reverse-topological) order: outermost consumers first, innermost
 * producers last. Any other order is also correct, just slower. See the
 * implementation comments in Inline.cpp for why. */
class Inliner : public IRMutator {
public:
    Inliner() = default;

    /** Construct an Inliner that will inline a single function. */
    explicit Inliner(const Function &f);

    /** Insert f into the set of functions to be inlined. */
    void add(const Function &f);

    /** Inline all calls to the added functions within e/s, returning the
     * result with CSE applied. */
    Expr do_inlining(const Expr &e);
    Stmt do_inlining(const Stmt &s);

    /** operator() on an Inliner is a synonym for do_inlining, so callers
     * who use the IRMutator-style i(s) syntax get the same behavior as
     * the explicit i.do_inlining(s). Shadows the inherited inline
     * operator() from IRMutator. */
    Expr operator()(const Expr &e) {
        return do_inlining(e);
    }
    Stmt operator()(const Stmt &s) {
        return do_inlining(s);
    }

protected:
    Expr visit(const Call *op) override;
    Expr visit(const Variable *op) override;
    Stmt visit(const Provide *op) override;

    using IRMutator::visit;

private:
    /** Per-(function, value_index) inlining state. */
    struct Entry {
        Function func;
        Expr qualified_body;
        size_t order_id;
        /** Min order_id of any inlinable Call still present inside this
         * entry's qualified_body. SIZE_MAX means the body has no pending
         * inlines (i.e. it's fully inlined, or hasn't been computed yet).
         * When active_limit later exceeds this, we recompute the cached
         * body so that the freshly-active functions are pulled in too. */
        size_t lowest_pending_order_id = SIZE_MAX;
    };
    using Key = std::pair<std::string, int>;
    std::map<Key, Entry> to_inline;

    size_t active_limit = SIZE_MAX;
    /** Min order_id of any inlinable Call still un-inlined anywhere in
     * the working Expr/Stmt this pass. Updated by visit(Call) for Calls
     * above the limit and bubbled up from re-processing cached bodies.
     * SIZE_MAX means nothing's left to inline; the deepening loop stops. */
    size_t min_skipped_order_id = SIZE_MAX;
    static constexpr size_t batch_size = 8;
};

/** Inline a single named function, which must be pure. For a pure function to
 * be inlined, it must not have any specializations (i.e. it can only have one
 * values definition). */
// @{
Stmt inline_function(const Stmt &s, const Function &f);
Expr inline_function(Expr e, const Function &f);
void inline_function(Function caller, const Function &f);
// @}

/** Inline a set of pure functions. Equivalent in effect to calling
 * inline_function(s, f) for each f, but the shared Inliner lets a chain
 * of nested inlines share work via the qualified-body cache; see Inliner
 * above for how the batch is processed. */
Stmt inline_functions(const Stmt &s, const std::vector<Function> &fs);

/** Check if the schedule of an inlined function is legal, throwing an error
 * if it is not. */
void validate_schedule_inlined_function(Function f);

}  // namespace Internal
}  // namespace Halide

#endif
