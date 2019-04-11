#ifndef HALIDE_VAR_H
#define HALIDE_VAR_H

/** \file
 * Defines the Var - the front-end variable
 */

#include "IR.h"

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
    Var(const std::string &n);

    /** Construct a Var with an automatically-generated unique name. */
    Var();

    /** Get the name of a Var */
    const std::string &name() const {return _name;}

    /** Test if two Vars are the same. This simply compares the names. */
    bool same_as(const Var &other) const {return _name == other._name;}

    /** Implicit var constructor. Implicit variables are injected
     * automatically into a function call if the number of arguments
     * to the function are fewer than its dimensionality and a
     * placeholder ("_") appears in its argument list. Defining a
     * function to equal an expression containing implicit variables
     * similarly appends those implicit variables, in the same order,
     * to the left-hand-side of the definition where the placeholder
     * ('_') appears.
     *
     * For example, consider the definition:
     *
     \code
     Func f, g;
     Var x, y;
     f(x, y) = 3;
     \endcode
     *
     * A call to f with the placeholder symbol \ref _
     * will have implicit arguments injected automatically, so f(2, \ref _)
     * is equivalent to f(2, \ref _0), where \ref _0 = Var::implicit(0), and f(\ref _)
     * (and indeed f when cast to an Expr) is equivalent to f(\ref _0, \ref _1).
     * The following definitions are all equivalent, differing only in the
     * variable names.
     *
     \code
     g(_) = f*3;
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
     *
     * While it is possible to use Var::implicit or the predeclared
     * implicits to create expressions that can be treated as small
     * anonymous functions (e.g. Func(_0 + _1)) this is considered
     * poor style. Instead use \ref lambda.
     */
    static Var implicit(int n);

    /** Return whether a variable name is of the form for an implicit argument.
     * TODO: This is almost guaranteed to incorrectly fire on user
     * declared variables at some point. We should likely prevent
     * user Var declarations from making names of this form.
     */
    //{
    static bool is_implicit(const std::string &name);
    bool is_implicit() const {
        return is_implicit(name());
    }
    //}

    /** Return the argument index for a placeholder argument given its
     *  name. Returns 0 for \ref _0, 1 for \ref _1, etc. Returns -1 if
     *  the variable is not of implicit form.
     */
    //{
    static int implicit_index(const std::string &name) {
        return is_implicit(name) ? atoi(name.c_str() + 1) : -1;
    }
    int implicit_index() const {
        return implicit_index(name());
    }
    //}

    /** Test if a var is the placeholder variable \ref _ */
    //{
    static bool is_placeholder(const std::string &name) {
        return name == "_";
    }
    bool is_placeholder() const {
        return is_placeholder(name());
    }
    //}

    /** A Var can be treated as an Expr of type Int(32) */
    operator Expr() const {
        return Internal::Variable::make(Int(32), name());
    }

    /** Vars to use for scheduling producer/consumer pairs on the gpu. Deprecated. */
    // @{
    HALIDE_ATTRIBUTE_DEPRECATED("Var::gpu_blocks() is deprecated.")
    static Var gpu_blocks() {
        return Var("__deprecated_block_id_x");
    }
    HALIDE_ATTRIBUTE_DEPRECATED("Var::gpu_threads() is deprecated.")
    static Var gpu_threads() {
        return Var("__deprecated_thread_id_x");
    }
    // @}

    /** A Var that represents the location outside the outermost loop. */
    static Var outermost() {
        return Var("__outermost");
    }
};

template<int N = -1>
struct ImplicitVar {
    Var to_var() const {
        if (N >= 0) {
            return Var::implicit(N);
        } else {
            return Var("_");
        }
    }

    operator Var() const {
        return to_var();
    }
    operator Expr() const {
        return to_var();
    }
};

/** A placeholder variable for infered arguments. See \ref Var::implicit */
static constexpr ImplicitVar<> _;

/** The first ten implicit Vars for use in scheduling. See \ref Var::implicit */
// @{
static constexpr ImplicitVar<0> _0;
static constexpr ImplicitVar<1> _1;
static constexpr ImplicitVar<2> _2;
static constexpr ImplicitVar<3> _3;
static constexpr ImplicitVar<4> _4;
static constexpr ImplicitVar<5> _5;
static constexpr ImplicitVar<6> _6;
static constexpr ImplicitVar<7> _7;
static constexpr ImplicitVar<8> _8;
static constexpr ImplicitVar<9> _9;
// @}

namespace Internal {

/** Make a list of unique arguments for definitions with unnamed
    arguments. */
std::vector<Var> make_argument_list(int dimensionality);

}  // namespace Internal

}  // namespace Halide

#endif
