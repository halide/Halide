#ifndef HALIDE_IR_EQUALITY_H
#define HALIDE_IR_EQUALITY_H

/** \file
 * Methods to test Exprs and Stmts for equality of value
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** A compare struct suitable for use in std::map and std::set that
 * computes a lexical ordering on IR nodes. */
struct IRDeepCompare {
    bool operator()(const Expr &a, const Expr &b) const;
    bool operator()(const Stmt &a, const Stmt &b) const;
};

struct IRGraphDeepCompare {
    bool operator()(const Expr &a, const Expr &b) const;
    bool operator()(const Stmt &a, const Stmt &b) const;
};

/** Compare IR nodes for equality of value. Traverses entire IR
 * tree. For equality of reference, use Expr::same_as. If you're
 * comparing non-CSE'd Exprs, use graph_equal, which is safe for nasty
 * graphs of IR nodes. */
// @{
bool equal(const Expr &a, const Expr &b);
bool equal(const Stmt &a, const Stmt &b);
bool graph_equal(const Expr &a, const Expr &b);
bool graph_equal(const Stmt &a, const Stmt &b);
// @}

/** Order unsanitized IRNodes for use in a map key */
// @{
bool graph_less_than(const Expr &a, const Expr &b);
bool graph_less_than(const Stmt &a, const Stmt &b);
// @}

void ir_equality_test();

}  // namespace Internal
}  // namespace Halide

#endif
