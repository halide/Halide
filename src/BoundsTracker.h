#ifndef HALIDE_BOUNDS_TRACKER_H
#define HALIDE_BOUNDS_TRACKER_H

/** \file
 * A utility for finding constant bounds of expressions at some point inside
 * a Stmt tree.
 */

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Bounds.h"
#include "ConstantInterval.h"
#include "Expr.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Tracks the enclosing lets, loops and dominating conditions as a pass
 * descends a Stmt, and uses them to find constant bounds of expressions at
 * that point.
 *
 * Pushing only records the Expr; everything derived from it is computed on
 * the first query that needs it and memoized while the binding lives.
 *
 * Names must be unique, as simplify() requires. push_interval() is the
 * exception: it refines a name that is already bound. */
class BoundsTracker {
public:
    /** Pops what push_for/push_let/push_interval pushed when destroyed. */
    class Binding {
    public:
        Binding() = default;
        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;
        Binding(Binding &&other) noexcept;
        ~Binding();

    private:
        friend class BoundsTracker;
        explicit Binding(BoundsTracker *tracker);

        BoundsTracker *tracker = nullptr;
    };

    /** Push a loop variable with range [min, max]. */
    Binding push_for(const std::string &name, const Expr &min, const Expr &max);

    /** Push a known interval for a variable, e.g. to narrow one already in
     * scope under a dominating condition. */
    Binding push_interval(const std::string &name, const Interval &interval);

    /** Push a let binding. A pure value may be substituted into the
     * expressions being bounded. */
    Binding push_let(const std::string &name, const Expr &value);

    /** Pops what push_fact pushed when destroyed. */
    class FactGuard {
    public:
        FactGuard() = default;
        FactGuard(const FactGuard &) = delete;
        FactGuard &operator=(const FactGuard &) = delete;
        FactGuard(FactGuard &&other) noexcept;
        ~FactGuard();

    private:
        friend class BoundsTracker;
        explicit FactGuard(BoundsTracker *tracker);

        BoundsTracker *tracker = nullptr;
    };

    /** Push a condition known to hold here, e.g. that of an enclosing
     * IfThenElse or a dominating assert. */
    FactGuard push_fact(const Expr &condition);

    /** Constant bounds of e at this point: the intersection of bounding e
     * against the scope, bounding it after simplify_with_context(), and
     * substituting the ends of each enclosing loop e is monotonic in. Stops
     * early once the result is exact. Integer types only; a bound that does
     * not fit in an int64_t is missing. */
    ConstantInterval find_constant_bounds(const Expr &e) const;
    std::optional<int64_t> find_constant_bound(const Expr &e, Direction d) const;

    /** Simplify e under the enclosing pure lets it uses and the relevant
     * facts. The result need not be constant, and may contain Lets, so it is
     * meant for analysis rather than for putting back into the IR. */
    Expr simplify_with_context(const Expr &e) const;

    /** The constant bounds of every variable in scope, for passing to
     * simplify(). */
    const Scope<Interval> &interval_scope() const;

    /** The pushed facts and enclosing loop ranges that share a variable with
     * e, directly or through another selected fact, for passing to
     * simplify(). */
    std::vector<Expr> relevant_facts(const Expr &e) const;

private:
    /** The first step of find_constant_bounds(): bounds of e from the scope
     * alone. */
    ConstantInterval find_constant_bounds_in_scope(const Expr &e) const;

    /** Narrow interval by substituting the ends of each enclosing loop e is
     * monotonic in, which keeps e correlated with the loop's range. */
    ConstantInterval tighten_using_loop_monotonicity(const Expr &e, ConstantInterval interval) const;

    /** Wrap e in the enclosing pure lets it refers to, innermost first. */
    Expr wrap_in_used_lets(const Expr &e) const;

    /** The variables an Expr mentions, as sorted Variable hashes plus a Bloom
     * filter over them. A hash collision only costs an extra fact. */
    struct VarSet {
        std::vector<uint32_t> hashes;
        uint64_t bloom = 0;

        /** One byte of a node's hash is its node type, so mix all 32 bits
         * rather than taking fixed bits. */
        static uint64_t bloom_bit(uint32_t h) {
            return (uint64_t)1 << ((h * 2654435761u) >> 26);
        }

        void add(uint32_t h) {
            hashes.push_back(h);
            bloom |= bloom_bit(h);
        }

        void sort_unique() {
            std::sort(hashes.begin(), hashes.end());
            hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
        }

        bool intersects(const VarSet &other) const {
            if (!(bloom & other.bloom)) {
                return false;
            }
            auto a = hashes.begin();
            auto b = other.hashes.begin();
            while (a != hashes.end() && b != other.hashes.end()) {
                if (*a == *b) {
                    return true;
                }
                if (*a < *b) {
                    ++a;
                } else {
                    ++b;
                }
            }
            return false;
        }

        void merge(const VarSet &other) {
            hashes.insert(hashes.end(), other.hashes.begin(), other.hashes.end());
            bloom |= other.bloom;
            sort_unique();
        }
    };

    /** The variables e mentions. */
    static VarSet vars_of(const Expr &e);

    enum class Kind {
        Let,      //< lo is the value
        Loop,     //< lo and hi are the loop min and max
        Explicit  //< bounds is given up front
    };

    struct Entry {
        Kind kind;
        std::string name;
        Expr lo, hi;
        /** Valid once this entry is realized into `scope`. */
        Interval bounds;
        enum class Purity {
            Unknown,
            Pure,
            Impure
        };
        mutable Purity purity = Purity::Unknown;
        /** Whether this entry has its own binding in `scope`. */
        mutable bool in_scope = false;
        /** For a Loop: min <= var and var <= max, and their variables. */
        mutable Expr fact_lo, fact_hi;
        mutable VarSet fact_vars;
        mutable bool facts_built = false;
    };

    /** One end of an enclosing loop's range, as a fact. An index rather than
     * a pointer, because pushing can reallocate `entries`. */
    struct Candidate {
        size_t entry;
        bool use_hi;
    };

    const Expr &candidate_expr(const Candidate &c) const;
    const VarSet &candidate_vars(const Candidate &c) const;

    /** The facts every enclosing loop implies, kept in step with `entries`. */
    const std::vector<Candidate> &candidates() const;

    /** Push every entry not yet in `scope`, outermost first. */
    void realize_all() const;
    Interval bounds_of(const Entry &entry) const;
    bool entry_is_pure(const Entry &entry) const;
    void pop_entry();
    void pop_fact();

    /** The bindings in scope, outermost first. */
    mutable std::vector<Entry> entries;

    /** entries[0, realized) are in `scope`. */
    mutable size_t realized = 0;
    mutable Scope<Interval> scope;

    /** A pushed condition, simplified and with its variables collected on
     * first use. The simplifier requires simplified facts. */
    struct Fact {
        mutable Expr condition;
        mutable VarSet vars;
        mutable bool vars_built = false;
    };
    std::vector<Fact> facts;

    mutable std::vector<Candidate> all_candidates;
    /** entries[0, candidates_built) have their facts in `all_candidates`. */
    mutable size_t candidates_built = 0;
};

}  // namespace Internal
}  // namespace Halide

#endif
