#ifndef HALIDE_SIMPLIFY_VISITORS_H
#define HALIDE_SIMPLIFY_VISITORS_H

/** \file
 * The simplifier is separated into multiple compilation units with
 * this single shared header to speed up the build. This file is not
 * exported in Halide.h. */

#include "Bounds.h"
#include "IRMatch.h"
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
#define EVAL_IN_LAMBDA(x) (([&]() HALIDE_NEVER_INLINE {return (x);})())

namespace Halide {
namespace Internal {

class Simplify : public VariadicVisitor<Simplify, Expr, Stmt> {
    using Super = VariadicVisitor<Simplify, Expr, Stmt>;

public:
    Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai);

    // We track constant integer bounds when they exist
    struct ConstBounds {
        int64_t min = 0, max = 0;
        bool min_defined = false, max_defined = false;
    };

#if LOG_EXPR_MUTATIONS
    static int debug_indent;

    Expr mutate(const Expr &e, ConstBounds *b) {
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Expr: " << e << "\n";
        debug_indent++;
        Expr new_e = Super::dispatch(e, b);
        debug_indent--;
        if (!new_e.same_as(e)) {
            debug(1)
                << spaces << "Before: " << e << "\n"
                << spaces << "After:  " << new_e << "\n";
        }
        internal_assert(e.type() == new_e.type());
        return new_e;
    }

#else
    HALIDE_ALWAYS_INLINE
    Expr mutate(const Expr &e, ConstBounds *b) {
        Expr new_e = Super::dispatch(e, b);
        internal_assert(new_e.type() == e.type()) << e << " -> " << new_e << "\n";
        return new_e;
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

    bool remove_dead_lets;
    bool no_float_simplify;

    HALIDE_ALWAYS_INLINE
    bool may_simplify(const Type &t) {
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

    Scope<VarInfo> var_info;
    Scope<ConstBounds> bounds_info;
    Scope<ModulusRemainder> alignment_info;

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

    // If we encounter a reference to a buffer (a Load, Store, Call,
    // or Provide), there's an implicit dependence on some associated
    // symbols.
    void found_buffer_reference(const std::string &name, size_t dimensions = 0);

    // Wrappers for as_const_foo that are more convenient to use in
    // the large chains of conditions in the visit methods
    // below. Unlike the versions in IROperator, these only match
    // scalars.
    bool const_float(const Expr &e, double *f);
    bool const_int(const Expr &e, int64_t *i);
    bool const_uint(const Expr &e, uint64_t *u);

    // Put the args to a commutative op in a canonical order
    HALIDE_ALWAYS_INLINE
    bool should_commute(const Expr &a, const Expr &b) {
        if (a.node_type() < b.node_type()) return true;
        if (a.node_type() > b.node_type()) return false;

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
        std::vector<Expr> truths, falsehoods;

        void learn_false(const Expr &fact);
        void learn_true(const Expr &fact);
        void learn_upper_bound(const Variable *v, int64_t val);
        void learn_lower_bound(const Variable *v, int64_t val);

        ScopedFact(Simplify *s) : simplify(s) {}
        ~ScopedFact();
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

    template <typename T>
    Expr hoist_slice_vector(Expr e);

    Stmt mutate_let_body(Stmt s, ConstBounds *) {return mutate(s);}
    Expr mutate_let_body(Expr e, ConstBounds *bounds) {return mutate(e, bounds);}

    template<typename T, typename Body>
    Body simplify_let(const T *op, ConstBounds *bounds);

    Expr visit(const IntImm *op, ConstBounds *bounds);
    Expr visit(const UIntImm *op, ConstBounds *bounds);
    Expr visit(const FloatImm *op, ConstBounds *bounds);
    Expr visit(const StringImm *op, ConstBounds *bounds);
    Expr visit(const Broadcast *op, ConstBounds *bounds);
    Expr visit(const Cast *op, ConstBounds *bounds);
    Expr visit(const Variable *op, ConstBounds *bounds);
    Expr visit(const Add *op, ConstBounds *bounds);
    Expr visit(const Sub *op, ConstBounds *bounds);
    Expr visit(const Mul *op, ConstBounds *bounds);
    Expr visit(const Div *op, ConstBounds *bounds);
    Expr visit(const Mod *op, ConstBounds *bounds);
    Expr visit(const Min *op, ConstBounds *bounds);
    Expr visit(const Max *op, ConstBounds *bounds);
    Expr visit(const EQ *op, ConstBounds *bounds);
    Expr visit(const NE *op, ConstBounds *bounds);
    Expr visit(const LT *op, ConstBounds *bounds);
    Expr visit(const LE *op, ConstBounds *bounds);
    Expr visit(const GT *op, ConstBounds *bounds);
    Expr visit(const GE *op, ConstBounds *bounds);
    Expr visit(const And *op, ConstBounds *bounds);
    Expr visit(const Or *op, ConstBounds *bounds);
    Expr visit(const Not *op, ConstBounds *bounds);
    Expr visit(const Select *op, ConstBounds *bounds);
    Expr visit(const Ramp *op, ConstBounds *bounds);
    Stmt visit(const IfThenElse *op);
    Expr visit(const Load *op, ConstBounds *bounds);
    Expr visit(const Call *op, ConstBounds *bounds);
    Expr visit(const Shuffle *op, ConstBounds *bounds);
    Expr visit(const Let *op, ConstBounds *bounds);
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
};

}
}

#endif
