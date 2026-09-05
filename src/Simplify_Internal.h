#ifndef HALIDE_SIMPLIFY_VISITORS_H
#define HALIDE_SIMPLIFY_VISITORS_H

/** \file
 * The simplifier is separated into multiple compilation units with
 * this single shared header to speed up the build. This file is not
 * exported in Halide.h. */

#include "Bounds.h"
#include "ConstantInterval.h"
#include "IRMatch.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "Scope.h"

// Because this file is only included by the simplify methods and
// doesn't go into Halide.h, we're free to use any old names for our
// macros.

#define LOG_EXPR_MUTATIONS 0
#define LOG_STMT_MUTATIONS 0

// On old compilers, some visitors would use large stack frames,
// because they use expression templates that generate large numbers
// of temporary objects when they are built and matched against. If we
// wrap the expressions that imply lots of temporaries in a lambda, we
// can get these large frames out of the recursive path.
#define EVAL_IN_LAMBDA(x) (([&]() HALIDE_NEVER_INLINE { return (x); })())

namespace Halide {
namespace Internal {

class Simplify : public VariadicVisitor<Simplify, Expr, Stmt> {
    using Super = VariadicVisitor<Simplify, Expr, Stmt>;

public:
    Simplify(const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai);

    struct ExprInfo {
        // We track constant integer bounds when they exist
        ConstantInterval bounds;
        // And the alignment of integer variables
        ModulusRemainder alignment;

        void trim_bounds_using_alignment() {
            if (alignment.modulus == 0) {
                bounds = ConstantInterval::single_point(alignment.remainder);
            } else if (alignment.modulus > 1) {
                if (bounds.min_defined) {
                    int64_t adjustment;
                    bool no_overflow = sub_with_overflow(64, alignment.remainder, mod_imp(bounds.min, alignment.modulus), &adjustment);
                    adjustment = mod_imp(adjustment, alignment.modulus);
                    int64_t new_min;
                    no_overflow &= add_with_overflow(64, bounds.min, adjustment, &new_min);
                    if (no_overflow) {
                        bounds.min = new_min;
                    }
                }
                if (bounds.max_defined) {
                    int64_t adjustment;
                    bool no_overflow = sub_with_overflow(64, mod_imp(bounds.max, alignment.modulus), alignment.remainder, &adjustment);
                    adjustment = mod_imp(adjustment, alignment.modulus);
                    int64_t new_max;
                    no_overflow &= sub_with_overflow(64, bounds.max, adjustment, &new_max);
                    if (no_overflow) {
                        bounds.max = new_max;
                    }
                }
            }

            if (bounds.is_single_point()) {
                alignment.modulus = 0;
                alignment.remainder = bounds.min;
            }

            if (bounds.is_bounded() && bounds.min > bounds.max) {
                // Impossible, we must be in unreachable code. TODO: surface
                // this to the simplify instance's in_unreachable flag.
                bounds.max = bounds.min;
            }
        }

        uint64_t largest_power_of_two_factor(uint64_t x) const {
            // Consider the bits of x from MSB to LSB. Say there are three
            // trailing zeros, and the four high bits are unknown:
            // a b c d 1 0 0 0
            // The largest power of two factor of a number is the trailing bits
            // up to and including the first 1. In this example that's 1000
            // (i.e. 8).
            // Negating is flipping the bits and adding one. First we flip:
            // ~a ~b ~c ~d 0 1 1 1
            // Then we add one:
            // ~a ~b ~c ~d 1 0 0 0
            // If we bitwise and this with the original, the unknown bits cancel
            // out, and we get left with just the largest power of two
            // factor. If we want a mask of the trailing zeros instead, we can
            // just subtract one.
            return x & -x;
        }

        void cast_to(Type t) {
            if ((!t.is_int() && !t.is_uint()) || (t.is_int() && t.bits() >= 32)) {
                return;
            }

            // We've just done some infinite-integer operation on a bounded
            // integer type, and we need to project the bounds and alignment
            // back in-range.

            if (!t.can_represent(bounds)) {
                if (t.bits() >= 64) {
                    // Just preserve any power-of-two factor in the modulus. When
                    // alignment.modulus == 0, the value is some positive constant
                    // representable as any 64-bit integer type, so there's no
                    // wraparound.
                    if (alignment.modulus > 0) {
                        alignment.modulus = largest_power_of_two_factor(alignment.modulus);
                        alignment.remainder &= alignment.modulus - 1;
                    }
                } else {
                    // A narrowing integer cast that could possibly overflow adds
                    // some unknown multiple of 2^bits
                    alignment = alignment + ModulusRemainder(((int64_t)1 << t.bits()), 0);
                }
            }

            // For UInt64 constants, the remainder might not be representable as an int64
            if (t.bits() == 64 && t.is_uint() &&
                alignment.modulus == 0 && alignment.remainder < 0) {
                // Forget the leading two bits to get a representable modulus
                // and remainder.
                alignment.modulus = (int64_t)1 << 62;
                alignment.remainder = alignment.remainder & (alignment.modulus - 1);
            }

            bounds.cast_to(t);
        }

        // Mix in existing knowledge about this Expr
        void intersect(const ExprInfo &other) {
            if (bounds < other.bounds || other.bounds < bounds) {
                // Impossible. We must be in unreachable code. TODO: It might
                // be nice to surface this to the simplify instance's
                // in_unreachable flag, but we'd have to be sure that it's going
                // to be caught at the right place.
                return;
            }
            bounds = ConstantInterval::make_intersection(bounds, other.bounds);
            alignment = ModulusRemainder::intersect(alignment, other.alignment);
            trim_bounds_using_alignment();
        }

        // An alternative representation for information about integers is that
        // certain bits have known values in the 2s complement
        // representation. This is a useful form for analyzing bitwise ops, so
        // we provide conversions to and from that representation. For narrow
        // types, this represent what the bits would be if they were sign or
        // zero-extended to 64 bits, so for uints the high bits are known to be
        // zero, and for ints it depends on whether or not we knew the high bit
        // to begin with.
        struct BitsKnown {
            // A mask which is 1 where we know the value of that bit
            uint64_t mask;
            // The actual value of the known bits
            uint64_t value;

            uint64_t known_zeros() const {
                return mask & ~value;
            }

            uint64_t known_ones() const {
                return mask & value;
            }

            bool all_bits_known() const {
                return mask == (uint64_t)(-1);
            }

            BitsKnown operator&(const BitsKnown &other) const {
                // Where either has known zeros, we have known zeros in the result
                uint64_t zeros = known_zeros() | other.known_zeros();
                // Where both have a known one, we have a known one in the result
                uint64_t ones = known_ones() & other.known_ones();
                return {zeros | ones, ones};
            }

            BitsKnown operator|(const BitsKnown &other) const {
                // Where either has known ones, we have known ones in the result
                uint64_t ones = known_ones() | other.known_ones();
                // Where both have a known zero, we have a known zero in the result.
                uint64_t zeros = known_zeros() & other.known_zeros();
                return {zeros | ones, ones};
            }

            BitsKnown operator^(const BitsKnown &other) const {
                // Unlike & and |, we need to know both bits to know anything.
                uint64_t new_mask = mask & other.mask;
                return {new_mask, (value ^ other.value) & new_mask};
            }
        };

        BitsKnown to_bits_known(const Type &type) const;
        void from_bits_known(BitsKnown known, const Type &type);
    };

    HALIDE_ALWAYS_INLINE
    void clear_expr_info(ExprInfo *info) {
        if (info) {
            *info = ExprInfo{};
        }
    }

    void set_expr_info_to_constant(ExprInfo *info, int64_t c) const {
        if (info) {
            info->bounds = ConstantInterval::single_point(c);
            info->alignment = ModulusRemainder{0, c};
        }
    }

    int64_t normalize_constant(const Type &t, int64_t c) {
        // If this is supposed to be an int32, but the constant is not
        // representable as an int32, we have a problem, because the Halide
        // simplifier is unsound with respect to int32 overflow (see
        // https://github.com/halide/Halide/issues/3245).

        // It's tempting to just say we return a signed_integer_overflow, for
        // which we know nothing, but if we're in this function we're making a
        // constant, so we clearly decided not to do that in the caller. Is this
        // a bug in the caller? No, this intentionally happens when
        // constant-folding narrowing casts, and changing that behavior to
        // return signed_integer_overflow breaks a bunch of real code, because
        // unfortunately that's how people express wrapping casts to int32. We
        // could return an ExprInfo that says "I know nothing", but we're also
        // returning a constant Expr, so the next mutation is just going to
        // infer everything there is to infer about a constant. The best we can
        // do at this point is just wrap to the right number of bits.
        int dropped_bits = 64 - t.bits();
        if (t.is_int()) {
            c <<= dropped_bits;
            c >>= dropped_bits;  // sign-extend
        } else if (t.is_uint()) {
            // For uints, normalization is considerably less problematic
            c <<= dropped_bits;
            c = (int64_t)(((uint64_t)c >> dropped_bits));  // zero-extend
        }
        return c;
    }

    // We never want to return make_const anything in the simplifier without
    // also setting the ExprInfo, so shadow the global make_const.
    Expr make_const(const Type &t, int64_t c, ExprInfo *info) {
        if (t.is_uint() && c < 0) {
            // Wrap it around
            return make_const(t, (uint64_t)c, info);
        }
        c = normalize_constant(t, c);
        set_expr_info_to_constant(info, c);
        return Halide::Internal::make_const(t, c);
    }

    Expr make_const(const Type &t, uint64_t c, ExprInfo *info) {
        c = normalize_constant(t, c);

        if ((int64_t)c >= 0) {
            // This is representable as an int64_t
            set_expr_info_to_constant(info, (int64_t)c);
        } else if (info) {
            // If it's not representable as an int64, we can't express
            // everything we know about it in ExprInfo.
            // We can say that it's big:
            info->bounds = ConstantInterval::bounded_below(INT64_MAX);
            // And we can say what we know about the bottom 62 bits (2^62 is the
            // largest power of two we can represent as an int64_t):
            int64_t modulus = (int64_t)1 << 62;
            info->alignment = {modulus, (int64_t)c & (modulus - 1)};
        }
        return Halide::Internal::make_const(t, c);
    }

    HALIDE_ALWAYS_INLINE
    Expr make_const(const Type &t, double c, ExprInfo *info) {
        // We don't currently track information about floats
        return Halide::Internal::make_const(t, c);
    }

    HALIDE_ALWAYS_INLINE
    Expr const_false(int lanes, ExprInfo *info) {
        return make_const(UInt(1, lanes), (int64_t)0, info);
    }

    HALIDE_ALWAYS_INLINE
    Expr const_true(int lanes, ExprInfo *info) {
        return make_const(UInt(1, lanes), (int64_t)1, info);
    }

#if (LOG_EXPR_MUTATIONS || LOG_STMT_MUTATIONS)
    int debug_indent = 0;
#endif

#if LOG_EXPR_MUTATIONS
    Expr mutate(const Expr &e, ExprInfo *b) {
        internal_assert(debug_indent >= 0);
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Expr: " << e << "\n";
        debug_indent++;
        Expr new_e = Super::dispatch(e, b);
        debug_indent--;
        if (!new_e.same_as(e)) {
            debug(1)
                << spaces << "Before: " << e << "\n"
                << spaces << "After:  " << new_e << "\n";
            if (b) {
                debug(1)
                    << spaces << "Bounds: " << b->bounds << " " << b->alignment << "\n";
                if (auto i = as_const_int(new_e)) {
                    internal_assert(b->bounds.contains(*i))
                        << e << "\n"
                        << new_e << "\n"
                        << b->bounds;
                } else if (auto i = as_const_uint(new_e)) {
                    internal_assert(b->bounds.contains(*i))
                        << e << "\n"
                        << new_e << "\n"
                        << b->bounds;
                }
                if (new_e.type().is_uint() &&
                    new_e.type().bits() < 64 &&
                    !is_signed_integer_overflow(new_e)) {
                    internal_assert(b->bounds.min_defined && b->bounds.min >= 0)
                        << e << "\n"
                        << new_e << "\n"
                        << b->bounds;
                }
            }
        }
        internal_assert(e.type() == new_e.type());
        return new_e;
    }

#else
    HALIDE_ALWAYS_INLINE
    Expr mutate(const Expr &e, ExprInfo *b) {
        // This gets inlined into every call to mutate, so do not add any code here.
        return Super::dispatch(e, b);
    }
#endif

#if LOG_STMT_MUTATIONS
    Stmt mutate(const Stmt &s) {
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Stmt: " << s << "\n";
        debug_indent++;
        Stmt new_s = Super::dispatch(s);
        debug_indent--;
        if (!new_s.same_as(s)) {
            debug(1)
                << spaces << "Before: " << s << "\n"
                << spaces << "After:  " << new_s << "\n";
        }
        return new_s;
    }
#else
    Stmt mutate(const Stmt &s) {
        return Super::dispatch(s);
    }
#endif

    // Returns true iff t is an integral type where overflow is undefined
    HALIDE_ALWAYS_INLINE
    bool no_overflow_int(Type t) {
        return t.is_int() && t.bits() >= 32;
    }

    HALIDE_ALWAYS_INLINE
    bool no_overflow_scalar_int(Type t) {
        return t.is_scalar() && no_overflow_int(t);
    }

    // Returns true iff t does not have a well defined overflow behavior.
    HALIDE_ALWAYS_INLINE
    bool no_overflow(Type t) {
        return t.is_float() || no_overflow_int(t);
    }

    struct VarInfo {
        Expr replacement;
        int old_uses, new_uses;
    };

    // Tracked for all let vars
    Scope<VarInfo> var_info;

    // Only tracked for integer let vars
    Scope<ExprInfo> bounds_and_alignment_info;

    // Symbols used by rewrite rules
    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;
    IRMatcher::Wild<4> u;
    IRMatcher::Wild<5> v;
    IRMatcher::WildConst<0> c0;
    IRMatcher::WildConst<1> c1;
    IRMatcher::WildConst<2> c2;
    IRMatcher::WildConst<3> c3;
    IRMatcher::WildConst<4> c4;
    IRMatcher::WildConst<5> c5;

    // Tracks whether or not we're inside a vector loop. Certain
    // transformations are not a good idea if the code is to be
    // vectorized.
    bool in_vector_loop = false;

    // Tracks whether or not the current IR is unconditionally unreachable.
    bool in_unreachable = false;

    // If we encounter a reference to a buffer (a Load, Store, Call,
    // or Provide), there's an implicit dependence on some associated
    // symbols.
    void found_buffer_reference(const std::string &name, size_t dimensions = 0);

    // Put the args to a commutative op in a canonical order
    HALIDE_ALWAYS_INLINE
    bool should_commute(const Expr &a, const Expr &b) {
        if (a.node_type() < b.node_type()) {
            return true;
        }
        if (a.node_type() > b.node_type()) {
            return false;
        }

        if (a.node_type() == IRNodeType::Variable) {
            const Variable *va = a.as<Variable>();
            const Variable *vb = b.as<Variable>();
            return va->name.compare(vb->name) > 0;
        }

        return false;
    }

    std::set<Expr, IRDeepCompare> truths, falsehoods;

    /** What we know about the difference between a pair of Exprs. Every
     * comparison we can learn from is a statement about (a - b): a < b means it
     * is at most -1, !(a < b) means it is at least 0, a == b means it is zero.
     * Because the complement of a half-line is a half-line, only the negation
     * of an equality fails to be an interval, and that is always a single point
     * removed, which is what invert represents. */
    struct KnownBound {
        Expr a, b;
        ConstantInterval diff;
        // Cheap structural summaries of a and b. Equal Exprs always summarize
        // to the same value, so a mismatch rules a record out without touching
        // the Exprs at all. Almost every query is about a pair nothing is known
        // about, so what this scan needs to be good at is saying no.
        uint32_t fingerprint_a = 0, fingerprint_b = 0;
        // If set, a - b is known *not* to lie in diff, which is always a single
        // point. Only a != b (or !(a == b)) produces one of these.
        bool invert = false;
    };
    std::vector<KnownBound> known_bounds;

    // A bit per pair key, over every record in the table. A query whose bit is
    // clear cannot match anything, which is the answer almost every query gets.
    // Wide enough that a few dozen facts leave it sparse: at 64 bits a typical
    // table saturates and lets four queries in ten through to the scan.
    static constexpr int difference_key_words = 4;
    uint64_t difference_keys[difference_key_words] = {0};

    // Summarize an Expr by its node type, plus the name or value of the leaves
    // that distinguish otherwise identical-looking nodes. Deliberately ignores
    // children: this only has to be equal for equal Exprs, not unique.
    static uint32_t expr_fingerprint(const BaseExprNode *e) {
        uint32_t h = ((uint32_t)e->node_type + 1) * 2654435761u;
        if (e->node_type == IRNodeType::Variable) {
            for (char c : ((const Variable *)e)->name) {
                h = h * 31u + (uint32_t)(unsigned char)c;
            }
        } else if (e->node_type == IRNodeType::IntImm) {
            h ^= (uint32_t)((const IntImm *)e)->value;
        }
        return h;
    }

    /** What a scan of known_bounds was able to establish about (a - b). A hole
     * that doesn't touch an end of the interval can't be represented in the
     * bounds, so it is tracked separately when it matters, which is when the
     * hole is at zero. */
    struct KnownDiff {
        ConstantInterval bounds;
        bool excludes_zero = false;
    };

    /** Everything the facts tell us about (a - b), without building any IR.
     * The arguments are borrowed, so this is safe to call with the raw nodes a
     * rewrite rule has bound to its wildcards. */
    KnownDiff known_difference(const BaseExprNode *a, const BaseExprNode *b);

    // Helpers over known_difference, for use as rewrite rule predicates. The
    // diffs return false when nothing is known, so that a rule asking for a
    // bound it can't get simply doesn't fire.
    bool is_known_equal(const BaseExprNode *a, const BaseExprNode *b);
    bool is_known_not_equal(const BaseExprNode *a, const BaseExprNode *b);
    bool known_min_diff(const BaseExprNode *a, const BaseExprNode *b, int64_t *result);
    bool known_max_diff(const BaseExprNode *a, const BaseExprNode *b, int64_t *result);

    // How deeply are we nested inside the conditions of can_prove predicates?
    // Proving such a condition recursively invokes the simplifier on it, so a
    // rule whose left-hand side also matches something built while proving its
    // own predicate recurses without bound. Bound it.
    //
    // The work grows sharply with this limit -- on an adversarial nest of
    // min(x, y) - min(z, w) it is roughly 0.02s at 1 or 2, 0.11s at 3 and 0.72s
    // at 4 -- while no rule needs the depth: instrumenting every correctness
    // test shows the deepest nesting any of them reaches is one. So this is
    // already a level of headroom over anything observed.
    int can_prove_depth = 0;
    static constexpr int max_can_prove_depth = 2;

    // Is there anything a known_true predicate could look up? Used to gate rules
    // whose predicates are only ever provable from facts learned higher up in
    // the IR, so that we don't pay for them in the common case.
    bool has_facts() const {
        return !truths.empty() || !falsehoods.empty();
    }

    // Is there anything a min_diff or max_diff predicate could look up? Only a
    // comparison of non-overflowing integers leaves a record here, so this is
    // strictly narrower than has_facts: a boolean fact, or a fact about a type
    // that can wrap, satisfies that one while leaving this table empty. Rules
    // that ask about differences must gate on this, or they spend a lookup on
    // a table that cannot answer.
    bool has_difference_facts() const {
        return !known_bounds.empty();
    }

    // Symmetric key for a pair. Equal summaries say only that the two nodes are
    // the same kind, and xoring them throws even that away, so key those by the
    // kind instead of letting every same-type pair share one bit.
    HALIDE_ALWAYS_INLINE
    static uint32_t difference_key(uint32_t fa, uint32_t fb) {
        return fa == fb ? fa * 0x9e3779b9u : (fa ^ fb);
    }

    // One bit per pair key.
    HALIDE_ALWAYS_INLINE
    bool difference_key_present(uint32_t key) const {
        const uint32_t bit = key % (difference_key_words * 64);
        return (difference_keys[bit / 64] >> (bit % 64)) & 1;
    }

    HALIDE_ALWAYS_INLINE
    void add_difference_key(uint32_t key) {
        const uint32_t bit = key % (difference_key_words * 64);
        difference_keys[bit / 64] |= (uint64_t)1 << (bit % 64);
    }

    // Replace exprs known to be truths or falsehoods with const_true or
    // const_false. Used to inject everything currently known into the
    // conditions of can_prove predicates in rewrite rules.
    Expr substitute_facts(const Expr &e);

    // Simplify the condition of a can_prove predicate in a rewrite rule, using
    // everything currently known.
    Expr simplify_can_prove_condition(const Expr &e);

    // Is a boolean Expr already known to be true? Unlike can_prove this only
    // looks the condition up in the facts, without simplifying anything.
    bool is_known_true(const Expr &e);

    struct ScopedFact {
        Simplify *simplify;

        std::vector<const Variable *> pop_list;
        std::vector<const Variable *> bounds_pop_list;
        std::set<Expr, IRDeepCompare> truths, falsehoods;
        // Everything in the simplifier's known_bounds from this index on was
        // pushed by this scope, and is truncated away again when it ends.
        size_t known_bounds_size = 0;
        // Bits can't be cleared one at a time, so keep the summary from before
        // this scope and put it back wholesale.
        uint64_t saved_difference_keys[difference_key_words] = {0};

        void learn_false(const Expr &fact);
        void learn_true(const Expr &fact);
        void learn_upper_bound(const Variable *v, int64_t val);
        void learn_lower_bound(const Variable *v, int64_t val);
        // Record what a comparison says about the difference between its sides.
        void learn_difference(const Expr &a, const Expr &b, const ConstantInterval &diff, bool invert);

        // Replace exprs known to be truths or falsehoods with const_true or const_false.
        Expr substitute_facts(const Expr &e);
        Stmt substitute_facts(const Stmt &s);

        ScopedFact(Simplify *s)
            : simplify(s), known_bounds_size(s->known_bounds.size()) {
            for (int i = 0; i < difference_key_words; i++) {
                saved_difference_keys[i] = s->difference_keys[i];
            }
        }
        ~ScopedFact();

        // allow move but not copy
        ScopedFact(const ScopedFact &that) = delete;
        // Not defaulted: the moved-from object must not undo anything in its
        // destructor. The containers below would be empty after a move and so
        // would be harmless, but known_bounds_size would survive and truncate
        // away the facts this scope had just learned.
        ScopedFact(ScopedFact &&that) noexcept
            : simplify(that.simplify),
              pop_list(std::move(that.pop_list)),
              bounds_pop_list(std::move(that.bounds_pop_list)),
              truths(std::move(that.truths)),
              falsehoods(std::move(that.falsehoods)),
              known_bounds_size(that.known_bounds_size) {
            for (int i = 0; i < difference_key_words; i++) {
                saved_difference_keys[i] = that.saved_difference_keys[i];
            }
            that.simplify = nullptr;
        }
    };

    // Tell the simplifier to learn from and exploit a boolean
    // condition, over the lifetime of the returned object.
    ScopedFact scoped_truth(const Expr &fact) {
        ScopedFact f(this);
        f.learn_true(fact);
        return f;
    }

    // Tell the simplifier to assume a boolean condition is false over
    // the lifetime of the returned object.
    ScopedFact scoped_falsehood(const Expr &fact) {
        ScopedFact f(this);
        f.learn_false(fact);
        return f;
    }

    Stmt mutate_let_body(const Stmt &s, ExprInfo *) {
        return mutate(s);
    }
    Expr mutate_let_body(const Expr &e, ExprInfo *info) {
        return mutate(e, info);
    }

    template<typename T, typename Body>
    HALIDE_NEVER_INLINE Body simplify_let_inner(const T *op, ExprInfo *info, std::vector<ScopedBinding<VarInfo>> &substituted);

    template<typename T, typename Body>
    Body simplify_let(const T *op, ExprInfo *info);

    Expr visit(const IntImm *op, ExprInfo *info);
    Expr visit(const UIntImm *op, ExprInfo *info);
    Expr visit(const FloatImm *op, ExprInfo *info);
    Expr visit(const StringImm *op, ExprInfo *info);
    Expr visit(const Broadcast *op, ExprInfo *info);
    Expr visit(const Cast *op, ExprInfo *info);
    Expr visit(const Reinterpret *op, ExprInfo *info);
    Expr visit(const Variable *op, ExprInfo *info);
    Expr visit(const Add *op, ExprInfo *info);
    Expr visit(const Sub *op, ExprInfo *info);
    Expr visit(const Mul *op, ExprInfo *info);
    Expr visit(const Div *op, ExprInfo *info);
    Expr visit(const Mod *op, ExprInfo *info);
    Expr visit(const Min *op, ExprInfo *info);
    Expr visit(const Max *op, ExprInfo *info);
    Expr visit(const EQ *op, ExprInfo *info);
    Expr visit(const NE *op, ExprInfo *info);
    Expr visit(const LT *op, ExprInfo *info);
    Expr visit(const LE *op, ExprInfo *info);
    Expr visit(const GT *op, ExprInfo *info);
    Expr visit(const GE *op, ExprInfo *info);
    Expr visit(const And *op, ExprInfo *info);
    Expr visit(const Or *op, ExprInfo *info);
    Expr visit(const Not *op, ExprInfo *info);
    Expr visit(const Select *op, ExprInfo *info);
    Expr visit(const Ramp *op, ExprInfo *info);
    Stmt visit(const IfThenElse *op);
    Expr visit(const Load *op, ExprInfo *info);
    Expr visit(const Call *op, ExprInfo *info);
    Expr visit(const Shuffle *op, ExprInfo *info);
    Expr visit(const VectorReduce *op, ExprInfo *info);
    Expr visit(const Let *op, ExprInfo *info);
    Stmt visit(const LetStmt *op);
    Stmt visit(const AssertStmt *op);
    Stmt visit(const For *op);
    Stmt visit(const Provide *op);
    Stmt visit(const Store *op);
    Stmt visit(const Allocate *op);
    Stmt visit(const Evaluate *op);
    Stmt visit(const ProducerConsumer *op);
    Stmt visit(const Block *op);
    Stmt visit(const Realize *op);
    Stmt visit(const Prefetch *op);
    Stmt visit(const Free *op);
    Stmt visit(const Acquire *op);
    Stmt visit(const Fork *op);
    Stmt visit(const Atomic *op);
    Stmt visit(const StreamingStore *op);
    Stmt visit(const StreamingLoads *op);
    Stmt visit(const HoistedStorage *op);

    std::pair<std::vector<Expr>, bool> mutate_with_changes(const std::vector<Expr> &old_exprs);
};

}  // namespace Internal
}  // namespace Halide

#endif
