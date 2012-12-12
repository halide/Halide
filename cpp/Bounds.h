#ifndef BOUNDS_H
#define BOUNDS_H

#include "IRVisitor.h"
#include "Scope.h"
#include <utility>
#include <vector>

namespace Halide {
namespace Internal {
    
using std::pair;
using std::vector;    
    
/* Given an expression in some variables, and a map from those
 * variables to their bounds (in the form of (minimum possible value,
 * maximum possible value)), compute two expressions that give the
 * minimum possible value and the maximum possible value of this
 * expression. Max or min may be undefined expressions if the value is
 * not bounded above or below.
 *
 * This is for tasks such as deducing the region of a buffer
 * loaded by a chunk of code.
 */
pair<Expr, Expr> bounds_of_expr_in_scope(Expr expr, const Scope<pair<Expr, Expr> > &scope);    

/* Compute a rectangular domain large enough to cover all the 'Call's
 * to a function that occur within a given statement. This is useful
 * for figuring out what regions of things to evaluate. */
vector<pair<Expr, Expr> > region_required(string func, Stmt s, const Scope<pair<Expr, Expr> > &scope);

/* Compute a rectangular domain large enough to cover all the
 * 'Provide's to a function the occur within a given statement. This
 * is useful for figuring out what region of a function a scattering
 * reduction (e.g. a histogram) will touch. */
vector<pair<Expr, Expr> > region_provided(string func, Stmt s, const Scope<pair<Expr, Expr> > &scope);

/* Compute the union of the above two */
vector<pair<Expr, Expr> > region_touched(string func, Stmt s, const Scope<pair<Expr, Expr> > &scope);


void bounds_test();
        
// TODO: Other useful things in src/bounds.ml, such as region of a func required by a stmt
        
//class Bounds : public IRVisitor {
// TODO
//};
}
}

#endif
