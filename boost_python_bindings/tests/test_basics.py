#!/usr/bin/python3

# to be called via nose, for example
# nosetests-3.4 --nocapture -v path_to/tests/test_basics.py

from halide import *

def test_basics():

    input = ImageParam(UInt(16), 2, 'input')
    x, y = Var('x'), Var('y')

    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    print("ping 0.0")
    input[x,y]
    print("ping 0.1")
    input[x+1,y]
    print("ping 0.2")
    (input[x,y]+input[x+1,y])
    print("ping 0.3")
    (input[x,y]+input[x+1,y]) / 2
    print("ping 0.4")
    blur_x[x,y] = input[x,y]

    print("ping 0.5")
    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    print("ping 1")
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

    xi, yi = Var('xi'), Var('yi')
    print("ping 2")
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)


    return


if __name__ == "__main__":
    test_basics()