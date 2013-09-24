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
     * to the function are fewer than its dimensionality and a
     * placeholder ("_") appears in its argument list. Defining a
     * function to equal an expression containing implicit variables
     * similarly appends those implicit variables, in the same order,
     * to the left-hand-side of the definition where the placeholder
     * ('_') appears. A Func without any argument list can be used as
     * an Expr and all arguments will be implicit.
     * 
     * For example, consider the definition:
     *
     \code
     Func f, g;
     Var x, y;
     f(x, y) = 3;
     \endcode
     * 
     * A call to f with fewer than two arguments and a placeholder
     * will have implicit arguments injected automatically, so f(2, _)
     * is equivalent to f(2, _0), where _0 = Var::implicit(0), and f(_)
     * (and indeed f when cast to an Expr) is equivalent to f(_0, _1).
     * The following definitions are all equivalent, differing only in the
     * variable names.
     * 
     \code
     g = f*3;
     g(_) = f(_)*3;
     g(x, _) = f(x, _)*3;
     g(x, y) = f(x, y)*3;
     \endcode
     *
     * These are expanded internally as follows:
     *
     \code
     g(_0, _1) = f(_0, _1)*3;
     g(_0, _1) = f(_0, _1)*3;
     g(x, _0) = f(x, _0)*3;
     g(x, y) = f(x, y)*3;
     \endcode
     *
     * The following, however, defines g as four dimensional:
     \code
     g(x, y, _) = f*3;
     \endcode
     *
     * It is equivalent to:
     *
     \code
     g(x, y, _0, _1) = f(_0, _1)*3;
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
     g(x, _0, _1) = h(_0) + (f(_0, _1) + f(x, _0)) * f(x, y);
     \endcode
     *
     * The first ten implicits, _0 through _9, are predeclared in this
     * header and can be used for scheduling. They should never be
     * used as arguments in a declaration or used in a call.
     * TODO: Think if there is a way to enforce this, or possibly
     * remove these and introduce a scheduling specific syntax for
     * naming them.
     *
     * While it is possible to use Var::implicit to create expressions
     * that can be treated as small anonymous functions, you should
     * not do this. Instead use \ref lambda.
     */
    static Var implicit(int n) {
        std::ostringstream str;
        str << "_" << n;
        return Var(str.str());
    }
   
    /** Return whether a variable name is of the form for an implicit argument.
     * TODO: This is almost guaranteed to incorrectly fire on user
     * declared variables at some point. We should likely prevent
     * user Var declarations from making names of this form.
     */
    static bool is_implicit(const std::string &name) {
        return Internal::starts_with(name, "_") &&
            name.find_first_not_of("0123456789", 1) == std::string::npos;
    }

    /** Return the argument index, i.e. slot number in a call, for an
     *  argument given its name. Returns -1 if the variable is not of
     *  implicit form.
     */
    static int implicit_index(const std::string &name) {
        return is_implicit(name) ? atoi(name.c_str() + 1) : -1;
    }

    static bool is_placeholder(const std::string &name) {
        return name == "_";
    }

    /** A Var can be treated as an Expr of type Int(32) */
    operator Expr() {
        return Internal::Variable::make(Int(32), name());
    }
};

/** This is used to provide a grace period during which old style
 * implicits code will continue to compile with a warning. This makes
 * it easier to convert to the new required placeholder ('_') style.
 * Both variants will work during the transition period. The code under
 * these #ifdef blocks can be completely removed after we finalize the
 * language change.
 */
#define HALIDE_WARNINGS_FOR_OLD_IMPLICITS 1

/** Placeholder for infered arguments.
 */
extern Var _;

/** Predeclare the first ten implicit Vars so they can be used in scheduling.
 */
extern Var _0, _1, _2, _3, _4, _5, _6, _7, _8, _9;

}

#endif
