#ifndef HALIDE_VAR_H
#define HALIDE_VAR_H

/** \file
 * Defines the Var - the front-end variable 
 */

#include <string>
#include "Util.h"
#include "IR.h"
#include <sstream>

namespace Halide {

/** A Halide variable, to be used when defining functions. It is just
 * a name, and can be reused in places where no name conflict will
 * occur. It can be used in the left-hand-side of a function
 * definition, or as an Expr. As an Expr, it always has type
 * Int(32). */
class Var {
    std::string _name;
public:
    /** Construct a Var with the given name */
    Var(const std::string &n) : _name(n) {}

    /** Construct a Var with an automatically-generated unique name */
    Var() : _name(Internal::unique_name('v')) {}

    /** Get the name of a Var */
    const std::string &name() const {return _name;}

    /** Test if two Vars are the same. This simply compares the names. */
    bool same_as(const Var &other) {return _name == other._name;}

    /** Implicit var constructor. Implicit variables are injected
     * automatically into a function call if the number of arguments
     * to the function are fewer than its dimensionality. Defining a
     * function to equal an expression containing implicit variables
     * similarly appends those implicit variables, in the same order,
     * to the left-hand-side of the definition. 
     * 
     * For example, consider the definition:
     *
     \code
     Func f, g;
     Var x, y;
     f(x, y) = 3;
     \endcode
     * 
     * A call to f with fewer than two arguments will have implicit
     * arguments injected automatically, so f(2) is equivalent to f(2,
     * i0), where i0 = Var::implicit(0), and f() (and indeed f when
     * cast to an Expr) is equivalent to f(i0, i1). The following
     * definitions are all equivalent, differing only in the variable
     * names.
     * 
     \code
     g = f*3;
     g() = f()*3;
     g(x) = f(x)*3;
     g(x, y) = f(x, y)*3;
     \endcode
     *
     * These are expanded internally as follows:
     *
     \code
     Var i0 = Var::implicit(0), i1 = Var::implicit(1);
     g(i0, i1) = f(i0, i1)*3;
     g(i0, i1) = f(i0, i1)*3;
     g(x, i0) = f(x, i0)*3;
     g(x, y) = f(x, y)*3;
     \endcode
     *
     * The following, however, defines g as four dimensional:
     \code
     g(x, y) = f*3;
     \endcode
     *
     * It is equivalent to:
     *
     \code
     g(x, y, i0, i1) = f(i0, i1)*3;
     \endcode
     * 
     * Expressions requiring differing numbers of implicit variables
     * can be combined. The left-hand-side of a definition injects
     * enough implicit variables to cover all of them:
     *
     \code
     Func h;
     h(x) = x*3;
     g(x) = h + (f + f(x)) * f(x, y);
     \endcode
     * 
     * expands to:
     *
     \code
     Func h;
     h(x) = x*3;
     g(x, i0, i1) = h(i0) + (f(i0, i1) + f(x, i0)) * f(x, y);
     \endcode
     *
     * While it is possible to use Var::implicit to create expressions
     * that can be treated as small anonymous functions, you should
     * not do this. Instead use \ref lambda.
     */
    static Var implicit(int n) {
        std::ostringstream str;
        str << "iv." << n;
        return Var(str.str());
    }
    
    /** A Var can be treated as an Expr of type Int(32) */
    operator Expr() {
        return new Internal::Variable(Int(32), name());
    }
};


}

#endif
