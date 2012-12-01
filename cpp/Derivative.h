#ifndef DERIVATIVE_H
#define DERIVATIVE_H

#include "IRVisitor.h"
#include <string>

namespace HalideInternal {
    
    using std::string;

    /* Compute the analytic derivative of the expression with respect
     * to the variable, or return false if it's non-differentiable
     * (e.g. if it calls an extern function that we have no knowledge
     * of). */
    bool derivative(Expr expr, const string &var, Expr *result);

    /* 
     * Compute the finite difference version of the derivative:
     * expr(var+1) - expr(var). The reason to do this as a derivative,
     * instead of just explicitly constructing expr(var+1) -
     * expr(var), is so that we don't have to do so much
     * simplification later. For example, the finite-difference
     * derivative of 2*x is trivially 2, whereas 2*(x+1) - 2*x may or
     * may not simplify down to 2, depending on the quality of our
     * simplification routine.
     *
     * Most rules for the finite difference and the true derivative
     * are the same. The quotient and product rules are not.
     *
     */
    bool finite_difference(Expr expr, const string &var, Expr *result);

    // class Derivative : public IRVisitor {
    // TODO
    // };
};

#endif
