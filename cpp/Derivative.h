#ifndef DERIVATIVE_H
#define DERIVATIVE_H

#include "IRVisitor.h"
#include <string>

namespace HalideInternal {
    
    using std::string;

    /* For floating point expressions, compute the analytic derivative
     * of the expression with respect to the variable, or return false
     * if it's non-differentiable (e.g. if it calls an extern function
     * that we have no knowledge of).
     *
     * For integer expressions, compute the finite difference:
     * expr(var+1) - expr(var). The reason to do this as a derivative,
     * instead of just explicitly constructing expr(var+1) -
     * expr(var), is so that we don't have to do so much
     * simplification later. For example, the finite-difference
     * derivative of 2*x is trivially 2, whereas 2*(x+1) - 2*x may or
     * may not simplify down to 2, depending on the quality of our
     * simplification routine.
     *
     * Most rules for the finite difference and the true derivative are
     * the same, which is why these are done by the same function. The
     * quotient and product rules are not.
     */
    bool derivative(Expr expr, const string &var, Expr *result);

    // class Derivative : public IRVisitor {
    // TODO
    // };
};

#endif
