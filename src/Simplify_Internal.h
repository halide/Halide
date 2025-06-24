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
    Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai);

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

            // Truncate the bounds to the new type.
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

    bool remove_dead_code;

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

    struct ScopedFact {
        Simplify *simplify;

        std::vector<const Variable *> pop_list;
        std::vector<const Variable *> bounds_pop_list;
        std::set<Expr, IRDeepCompare> truths, falsehoods;

        void learn_false(const Expr &fact);
        void learn_true(const Expr &fact);
        void learn_upper_bound(const Variable *v, int64_t val);
        void learn_lower_bound(const Variable *v, int64_t val);

        // Replace exprs known to be truths or falsehoods with const_true or const_false.
        Expr substitute_facts(const Expr &e);
        Stmt substitute_facts(const Stmt &s);

        ScopedFact(Simplify *s)
            : simplify(s) {
        }
        ~ScopedFact();

        // allow move but not copy
        ScopedFact(const ScopedFact &that) = delete;
        ScopedFact(ScopedFact &&that) = default;
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
    Stmt visit(const HoistedStorage *op);

    std::pair<std::vector<Expr>, bool> mutate_with_changes(const std::vector<Expr> &old_exprs);
};

}  // namespace Internal
}  // namespace Halide

#endif
