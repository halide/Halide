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
                        // This masks off all bits except for the lowest set one,
                        // giving the largest power-of-two factor of a number.
                        alignment.modulus &= -alignment.modulus;
                        alignment.remainder = mod_imp(alignment.remainder, alignment.modulus);
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
    };

    HALIDE_ALWAYS_INLINE
    void clear_expr_info(ExprInfo *b) {
        if (b) {
            *b = ExprInfo{};
        }
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
                    internal_assert(b->bounds.contains(*i)) << e << "\n"
                                                            << new_e << "\n"
                                                            << b->bounds;
                } else if (auto i = as_const_uint(new_e)) {
                    internal_assert(b->bounds.contains(*i)) << e << "\n"
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
    bool no_float_simplify = false;

    HALIDE_ALWAYS_INLINE
    bool may_simplify(const Type &t) const {
        return !no_float_simplify || !t.is_float();
    }

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
