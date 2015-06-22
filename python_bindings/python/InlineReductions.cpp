#include "InlineReductions.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/InlineReductions.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;

//Expr sum(Expr, const std::string &s = "sum");
//Expr product(Expr, const std::string &s = "product");
//Expr maximum(Expr, const std::string &s = "maximum");
//Expr minimum(Expr, const std::string &s = "minimum");

h::Expr sum0(h::Expr e, const std::string name)
{
    return h::sum(e, name);
}

h::Expr sum1(h::RDom r, h::Expr e, const std::string name)
{
    return h::sum(r, e, name);
}

h::Expr product0(h::Expr e, const std::string name)
{
    return h::product(e, name);
}

h::Expr product1(h::RDom r, h::Expr e, const std::string name)
{
    return h::product(r, e, name);
}

h::Expr maximum0(h::Expr e, const std::string name)
{
    return h::maximum(e, name);
}

h::Expr maximum1(h::RDom r, h::Expr e, const std::string name)
{
    return h::maximum(r, e, name);
}


h::Expr minimum0(h::Expr e, const std::string name)
{
    return h::minimum(e, name);
}

h::Expr minimum1(h::RDom r, h::Expr e, const std::string name)
{
    return h::minimum(r, e, name);
}

h::Tuple argmin0(h::Expr e, const std::string name)
{
    return h::argmin(e, name);
}

h::Tuple argmin1(h::RDom r, h::Expr e, const std::string name)
{
    return h::argmin(r, e, name);
}

h::Tuple argmax0(h::Expr e, const std::string name)
{
    return h::argmax(e, name);
}

h::Tuple argmax1(h::RDom r, h::Expr e, const std::string name)
{
    return h::argmax(r, e, name);
}


void defineInlineReductions()
{
    // Defines some inline reductions: sum, product, minimum, maximum.

    p::def("sum", &sum0, (p::arg("e"), p::arg("name")="sum"),
           "An inline reduction.");
    p::def("sum", &sum1, (p::arg("r"), p::arg("e"), p::arg("name")="sum"),
           "An inline reduction.");

    p::def("product", &product0, (p::arg("e"), p::arg("name")="product"),
           "An inline reduction.");
    p::def("product", &product1, (p::arg("r"), p::arg("e"), p::arg("name")="product"),
           "An inline reduction.");

    p::def("maximum", &maximum0, (p::arg("e"), p::arg("name")="maximum"),
           "An inline reduction.");
    p::def("maximum", &maximum1, (p::arg("r"), p::arg("e"), p::arg("name")="maximum"),
           "An inline reduction.");

    p::def("minimum", &minimum0, (p::arg("e"), p::arg("name")="minimum"),
           "An inline reduction.");
    p::def("minimum", &minimum1, (p::arg("r"), p::arg("e"), p::arg("name")="minimum"),
           "An inline reduction.");

    p::def("argmin", &argmin0, (p::arg("e"), p::arg("name")="argmin"),
           "An inline reduction.");
    p::def("argmin", &argmin1, (p::arg("r"), p::arg("e"), p::arg("name")="argmin"),
           "An inline reduction.");

    p::def("argmax", &argmax0, (p::arg("e"), p::arg("name")="argmax"),
           "An inline reduction.");
    p::def("argmax", &argmax1, (p::arg("r"), p::arg("e"), p::arg("name")="argmax"),
           "An inline reduction.");

    ///** An inline reduction. This is suitable for convolution-type
    // * operations - the reduction will be computed in the innermost loop
    // * that it is used in. The argument may contain free or implicit
    // * variables, and must refer to some reduction domain. The free
    // * variables are still free in the return value, but the reduction
    // * domain is captured - the result expression does not refer to a
    // * reduction domain and can be used in a pure function definition.
    // *
    // * An example using \ref sum :
    // *
    // \code
    // Func f, g;
    // Var x;
    // RDom r(0, 10);
    // f(x) = x*x;
    // g(x) = sum(f(x + r));
    // \endcode
    // *
    // * Here g computes some blur of x, but g is still a pure function. The
    // * sum is being computed by an anonymous reduction function that is
    // * scheduled innermost within g.*/
    //Expr sum(Expr, const std::string &s = "sum");
    //Expr product(Expr, const std::string &s = "product");
    //Expr maximum(Expr, const std::string &s = "maximum");
    //Expr minimum(Expr, const std::string &s = "minimum");

    ///** Variants of the inline reduction in which the RDom is stated
    // * explicitly. The expression can refer to multiple RDoms, and only
    // * the inner one is captured by the reduction. This allows you to
    // * write expressions like:
    // \code
    // RDom r1(0, 10), r2(0, 10), r3(0, 10);
    // Expr e = minimum(r1, product(r2, sum(r3, r1 + r2 + r3)));
    // \endcode*/
    //Expr sum(RDom, Expr, const std::string &s = "sum");
    //Expr product(RDom, Expr, const std::string &s = "product");
    //Expr maximum(RDom, Expr, const std::string &s = "maximum");
    //Expr minimum(RDom, Expr, const std::string &s = "minimum");


    ///** Returns an Expr or Tuple representing the coordinates of the point
    // * in the RDom which minimizes or maximizes the expression. The
    // * expression must refer to some RDom. Also returns the extreme value
    // * of the expression as the last element of the tuple. */
    //Tuple argmax(Expr, const std::string &s = "argmax");
    //Tuple argmin(Expr, const std::string &s = "argmin");
    //Tuple argmax(RDom, Expr, const std::string &s = "argmax");
    //Tuple argmin(RDom, Expr, const std::string &s = "argmin");

    return;
}

