#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/operators.hpp>
#include <boost/python/str.hpp>
#include <boost/python.hpp>

//#include <Halide.h>
//#include "../../build/include/Halide.h"
#include "../../src/Var.h"
#include "../../src/Expr.h"
#include "../../src/IROperator.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;

char const* greet()
{
    return "hello, world from Halide python bindings";
}

/*
input = ImageParam(UInt(16), 2, 'input')
        x, y = Var('x'), Var('y')

blur_x = Func('blur_x')
        blur_y = Func('blur_y')

        blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
        blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

        xi, yi = Var('xi'), Var('yi')

        blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
        blur_x.compute_at(blur_y, x).vectorize(x, 8)

        maxval = 255
        in_image = Image(UInt(16), builtin_image('rgb.png'), scale=1.0) # Set scale to 1 so that we only use 0...255 of the UInt(16) range
        eval_func = filter_image(input, blur_y, in_image, disp_time=True, out_dims = (OUT_DIMS[0]-8, OUT_DIMS[1]-8), times=5)
        I = eval_func()
        if len(sys.argv) >= 2:
        I.save(sys.argv[1], maxval)
        else:
        I.show(maxval)
*/

template<typename PythonClass>
void add_operators(PythonClass &class_instance)
{
    using namespace boost::python;

    class_instance
            .def(self + self)
            .def(self - self)
            .def(self * self)
            .def(self / self)
            .def(self % self)
            //.def(pow(self, p::other<float>))
            .def(pow(self, self))
            .def(self & self) // and
            .def(self | self) // or
            .def(-self) // neg
            .def(~self) // invert
            .def(self < self)
            .def(self <= self)
            .def(self == self)
            .def(self != self)
            .def(self > self)
            .def(self >= self);

    return;
}

void defineVar()
{
    using Halide::Var;
    auto var_class = p::class_<Var>("Var",
                                    "A Halide variable, to be used when defining functions. It is just" \
                                    "a name, and can be reused in places where no name conflict will" \
                                    "occur. It can be used in the left-hand-side of a function" \
                                    "definition, or as an Expr. As an Expr, it always has type Int(32).\n" \
                                    "\n" \
                                    "Constructors::\n" \
                                    "Var()      -- Construct Var with an automatically-generated unique name\n" \
                                    "Var(name)  -- Construct Var with the given string name.\n",
                                    p::init<std::string>())
            .def(p::init<>())
            //.add_property("name", &Var::name) // "Get the name of a Var.")
            .def("name", &Var::name, boost::python::return_value_policy<boost::python:: copy_const_reference>())
            .def("same_as", &Var::same_as, "Test if two Vars are the same.")
            //.def(self == p::other<Var>())
            .def("implicit", &Var::implicit, "Construct implicit Var from int n.");

    add_operators(var_class);
    return;
}


void defineExpr()
{
    using Halide::Expr;

    auto expr_class = p::class_<Expr>("Expr",
                                      "An expression or fragment of Halide code.\n" \
                                      "One can explicitly coerce most types to Expr via the Expr(x) constructor." \
                                      "The following operators are implemented over Expr, and also other types" \
                                      "such as Image, Func, Var, RVar generally coerce to Expr when used in arithmetic::\n\n" \
                                      "+ - * / % ** & |\n" \
                                      "-(unary) ~(unary)\n" \
                                      " < <= == != > >=\n" \
                                      "+= -= *= /=\n" \
                                      "The following math global functions are also available::\n" \
                                      "Unary:\n" \
                                      "  abs acos acosh asin asinh atan atanh ceil cos cosh exp\n" \
                                      "  fast_exp fast_log floor log round sin sinh sqrt tan tanh\n" \
                                      "Binary:\n" \
                                      "  hypot fast_pow max min pow\n\n" \
                                      "Ternary:\n" \
                                      "  clamp(x, lo, hi)                  -- Clamp expression to [lo, hi]\n" \
                                      "  select(cond, if_true, if_false)   -- Return if_true if cond else if_false\n",
                                      p::init<std::string>())
            .def(p::init<int>()) // Make an expression representing a const 32-bit int (i.e. an IntImm)
            .def(p::init<float>()) // Make an expression representing a const 32-bit float (i.e. a FloatImm)
            .def(p::init<double>()) /* Make an expression representing a const 32-bit float, given a
                                             * double. Also emits a warning due to truncation. */
            .def(p::init<std::string>()) // Make an expression representing a const string (i.e. a StringImm)
            .def(p::init<const h::Internal::BaseExprNode *>()) //Expr(const Internal::BaseExprNode *n) : IRHandle(n) {}
            .def("type", &Expr::type); // Get the type of this expression node

    add_operators(expr_class);

    return;
}

BOOST_PYTHON_MODULE(halide)
{
    using namespace boost::python;
    def("greet", greet);

    defineVar();
    defineExpr();
}
