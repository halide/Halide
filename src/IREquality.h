#ifndef HALIDE_IR_EQUALITY_H
#define HALIDE_IR_EQUALITY_H

/** \file
 * Methods to test Exprs and Stmts for equality of value.
 *
 * These methods traverse the entire IR tree. For equality of reference, use
 * Expr::same_as. If you're comparing non-CSE'd Exprs, use graph_equal or
 * graph_less_than, which is safe for nasty graphs of IR nodes.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

// We want to inline a few quick checks into the caller. These are the actual
// implementations that get called after those quick checks.
bool equal_impl(const IRNode &a, const IRNode &b);
bool graph_equal_impl(const IRNode &a, const IRNode &b);
bool less_than_impl(const IRNode &a, const IRNode &b);
bool graph_less_than_impl(const IRNode &a, const IRNode &b);

/** Compare an Expr to an int literal. This is a somewhat common use of equal in
 * tests. Making this separate avoids constructing an Expr out of the int
 * literal just to check if it's equal to a. */
HALIDE_ALWAYS_INLINE
bool equal(const Expr &a, int b) {
    if (const IntImm *i = a.as<IntImm>()) {
        return (a.type() == Int(32) && i->value == b);
    } else {
        return false;
    }
}

/** Check if two defined Stmts or Exprs are equal. */
HALIDE_ALWAYS_INLINE
bool equal(const IRNode &a, const IRNode &b) {
    if (&a == &b) {
        return true;
    } else if (a.node_type != b.node_type) {
        return false;
    } else {
        return equal_impl(a, b);
    }
}

/** Check if two possible-undefined Stmts or Exprs are equal. */
HALIDE_ALWAYS_INLINE
bool equal(const IRHandle &a, const IRHandle &b) {
    if (!a.defined()) {
        return !b.defined();
    } else if (!b.defined()) {
        return false;
    } else {
        return equal(*(a.get()), *(b.get()));
    }
}

/** Check if two defined Stmts or Exprs are equal. Safe to call on Exprs that
 * haven't been passed to common_subexpression_elimination. */
HALIDE_ALWAYS_INLINE
bool graph_equal(const IRNode &a, const IRNode &b) {
    if (&a == &b) {
        return true;
    } else if (a.node_type != b.node_type) {
        return false;
    } else {
        return equal_impl(a, b);
    }
}

/** Check if two possibly-undefined Stmts or Exprs are equal. Safe to call on
 * Exprs that haven't been passed to common_subexpression_elimination. */
HALIDE_ALWAYS_INLINE
bool graph_equal(const IRHandle &a, const IRHandle &b) {
    if (!a.defined()) {
        return !b.defined();
    } else if (!b.defined()) {
        return false;
    } else {
        return equal(*(a.get()), *(b.get()));
    }
}

/** Check if two defined Stmts or Exprs are in a lexicographic order. For use in
 * map keys. */
HALIDE_ALWAYS_INLINE
bool less_than(const IRNode &a, const IRNode &b) {
    if (&a == &b) {
        return false;
    } else if (a.node_type < b.node_type) {
        return true;
    } else {
        return less_than_impl(a, b);
    }
}

/** Check if two possibly-undefined Stmts or Exprs are in a lexicographic
 * order. For use in map keys. */
HALIDE_ALWAYS_INLINE
bool less_than(const IRHandle &a, const IRHandle &b) {
    if (a.get() == b.get()) {
        return false;
    } else if (!a.defined()) {
        return true;
    } else if (!b.defined()) {
        return false;
    } else {
        return less_than(*(a.get()), *(b.get()));
    }
}

/** Check if two defined Stmts or Exprs are in a lexicographic order. For use in
 * map keys. Safe to use on Exprs that haven't been passed to
 * common_subexpression_elimination. */
HALIDE_ALWAYS_INLINE
bool graph_less_than(const IRNode &a, const IRNode &b) {
    if (&a == &b) {
        return false;
    } else if (a.node_type < b.node_type) {
        return true;
    } else {
        return graph_less_than_impl(a, b);
    }
}

/** Check if two possibly-undefined Stmts or Exprs are in a lexicographic
 * order. For use in map keys. Safe to use on Exprs that haven't been passed to
 * common_subexpression_elimination. */
HALIDE_ALWAYS_INLINE
bool graph_less_than(const IRHandle &a, const IRHandle &b) {
    if (a.get() == b.get()) {
        return false;
    } else if (!a.defined()) {
        return true;
    } else if (!b.defined()) {
        return false;
    } else {
        return graph_less_than(*(a.get()), *(b.get()));
    }
}

/** A compare struct built around less_than, for use as the comparison
 * object in a std::map or std::set. */
struct IRDeepCompare {
    bool operator()(const IRHandle &a, const IRHandle &b) const {
        return less_than(a, b);
    }
};

/** A compare struct built around graph_less_than, for use as the comparison
 * object in a std::map or std::set. */
struct IRGraphDeepCompare {
    bool operator()(const IRHandle &a, const IRHandle &b) const {
        return graph_less_than(a, b);
    }
};

void ir_equality_test();

}  // namespace Internal
}  // namespace Halide

#endif
