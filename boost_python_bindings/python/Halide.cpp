#include <boost/python.hpp>

#include "Var.h"
#include "Expr.h"
#include "Func.h"
#include "Param.h"
#include "Type.h"
#include "IROperator.h"

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


BOOST_PYTHON_MODULE(halide)
{
    using namespace boost::python;
    def("greet", greet);

    defineVar();
    defineExpr();
    defineFunc();
    defineType();
    defineParam();
    defineOperators();
}
