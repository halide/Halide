#!/usr/bin/python3
# Halide tutorial lesson 13: Tuples

# This lesson describes how to write Funcs that evaluate to multiple
# values.

# On linux, you can compile and run it like so:
# g++ lesson_13*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_13 -std=c++11
# LD_LIBRARY_PATH=../bin ./lesson_13

# On os x:
# g++ lesson_13*.cpp -g -I ../include -L ../bin -lHalide -o lesson_13 -std=c++11
# DYLD_LIBRARY_PATH=../bin ./lesson_13

# If you have the entire Halide source tree, you can also build it by
# running:
#    make tutorial_lesson_13_tuples
# in a shell with the current directory at the top of the halide
# source tree.

#include "Halide.h"
#include <stdio.h>
#include <algorithm>
#using namespace Halide
from halide import *

import numpy
import math

min_, max_ = __builtins__.min, __builtins__.max

def main():

    # So far Funcs (such as the one below) have evaluated to a single
    # scalar value for each point in their domain.
    single_valued = Func()
    x, y = Var("x"), Var("y")
    single_valued[x, y] = x + y

    # One way to write a Func that returns a collection of values is
    # to add an additional dimension which indexes that
    # collection. This is how we typically deal with color. For
    # example, the Func below represents a collection of three values
    # for every x, y coordinate indexed by c.
    color_image = Func()
    c = Var("c")
    color_image[x, y, c] = select(c == 0, 245, # Red value
                                  c == 1, 42,  # Green value
                                  132)        # Blue value

    # This method is often convenient because it makes it easy to
    # operate on this Func in a way that treats each item in the
    # collection equally:
    brighter = Func()
    brighter[x, y, c] = color_image[x, y, c] + 10

    # However this method is also inconvenient for three reasons.
    #
    # 1) Funcs are defined over an infinite domain, so users of this
    # Func can for example access color_image(x, y, -17), which is
    # not a meaningful value and is probably indicative of a bug.
    #
    # 2) It requires a select, which can impact performance if not
    # bounded and unrolled:
    # brighter.bound(c, 0, 3).unroll(c)
    #
    # 3) With this method, all values in the collection must have the
    # same type. While the above two issues are merely inconvenient,
    # this one is a hard limitation that makes it impossible to
    # express certain things in this way.

    # It is also possible to represent a collection of values as a
    # collection of Funcs:
    func_array = [Func() for i in range(3)]
    func_array[0][x, y] = x + y
    func_array[1][x, y] = sin(x)
    func_array[2][x, y] = cos(y)

    # This method avoids the three problems above, but introduces a
    # new annoyance. Because these are separate Funcs, it is
    # difficult to schedule them so that they are all computed
    # together inside a single loop over x, y.

    # A third alternative is to define a Func as evaluating to a
    # Tuple instead of an Expr. A Tuple is a fixed-size collection of
    # Exprs which may have different type. The following function
    # evaluates to an integer value (x+y), and a floating point value
    # (sin(x*y)).
    multi_valued = Func("multi_valued")
    multi_valued[x, y] = (x + y, sin(x * y))

    # Realizing a tuple-valued Func returns a collection of
    # Buffers. We call this a Realization. It's equivalent to a
    # std::vector of Buffer/Image objects:
    if True:
        (im1, im2) = multi_valued.realize(80, 60)
        assert type(im1) is Buffer_int32
        assert type(im2) is Buffer_float32
        assert im1(30, 40) == 30 + 40
        assert numpy.isclose(im2(30, 40), math.sin(30 * 40))


    # All Tuple elements are evaluated together over the same domain
    # in the same loop nest, but stored in distinct allocations. The
    # equivalent C++ code to the above is:
    if True:
        multi_valued_0 = numpy.empty((80*60), dtype=numpy.int32)
        multi_valued_1 = numpy.empty((80*60), dtype=numpy.int32)

        for yy in range(80):
            for xx in range(60):
                multi_valued_0[xx + 60*yy] = xx + yy
                multi_valued_1[xx + 60*yy] = math.sin(xx*yy)


    # When compiling ahead-of-time, a Tuple-valued Func evaluates
    # into multiple distinct output buffer_t structs. These appear in
    # order at the end of the function signature:
    # int multi_valued(...input buffers and params..., buffer_t *output_1, buffer_t *output_2)

    # You can construct a Tuple by passing multiple Exprs to the
    # Tuple constructor as we did above. Perhaps more elegantly, you
    # can also take advantage of C++11 initializer lists and just
    # enclose your Exprs in braces:
    multi_valued_2 = Func("multi_valued_2")
    multi_valued_2[x, y] = (x + y, sin(x * y))

    # Calls to a multi-valued Func cannot be treated as Exprs. The
    # following is a syntax error:
    # Func consumer
    # consumer[x, y] = multi_valued_2[x, y] + 10

    # Instead you must index the returned object with square brackets
    # to retrieve the individual Exprs:
    integer_part = multi_valued_2[x, y][0]
    floating_part = multi_valued_2[x, y][1]
    assert type(integer_part) is FuncTupleElementRef
    assert type(floating_part) is FuncTupleElementRef

    consumer = Func()
    consumer[x, y] = (integer_part + 10, floating_part + 10.0)

    # Tuple reductions.
    if True:
        # Tuples are particularly useful in reductions, as they allow
        # the reduction to maintain complex state as it walks along
        # its domain. The simplest example is an argmax.

        # First we create an Image to take the argmax over.
        input_func = Func()
        input_func[x] = sin(x)
        input = input_func.realize(100)
        assert type(input) is Buffer_float32

        # Then we defined a 2-valued Tuple which tracks the maximum value
        # its index.
        arg_max = Func()

        # Pure definition.
        # (using [()] for zero-dimensional Funcs is a convention of this python interface)
        arg_max[()] = (0, input(0))

        # Update definition.
        r = RDom(1, 99)
        old_index = arg_max[()][0]
        old_max   = arg_max[()][1]
        new_index = select(old_max > input[r], r, old_index)
        new_max   = max(input[r], old_max)
        arg_max[()] = (new_index, new_max)

        # The equivalent C++ is:
        arg_max_0 = 0
        arg_max_1 = float(input(0))
        for r in range(1, 100):
            old_index = arg_max_0
            old_max = arg_max_1
            new_index = r if (old_max > input(r)) else old_index
            new_max = max_(input(r), old_max)
            # In a tuple update definition, all loads and computation
            # are done before any stores, so that all Tuple elements
            # are updated atomically with respect to recursive calls
            # to the same Func.
            arg_max_0 = new_index
            arg_max_1 = new_max


        # Let's verify that the Halide and C++ found the same maximum
        # value and index.
        if True:
            (r0, r1) = arg_max.realize()

            assert type(r0) is Buffer_int32
            assert type(r1) is Buffer_float32
            assert arg_max_0 == r0(0)
            assert numpy.isclose(arg_max_1, r1(0))


        # Halide provides argmax and argmin as built-in reductions
        # similar to sum, product, maximum, and minimum. They return
        # a Tuple consisting of the point in the reduction domain
        # corresponding to that value, and the value itself. In the
        # case of ties they return the first value found. We'll use
        # one of these in the following section.


    # Tuples for user-defined types.
    if True:
        # Tuples can also be a convenient way to represent compound
        # objects such as complex numbers. Defining an object that
        # can be converted to and from a Tuple is one way to extend
        # Halide's type system with user-defined types.
        class Complex:

            def __init__(self, r, i=None):
                if type(r) is float and type(i) is float:
                    self.real = Expr(r)
                    self.imag = Expr(i)
                elif i is not None:
                    self.real = r
                    self.imag = i
                else:
                    self.real = r[0]
                    self.imag = r[1]

            def as_tuple(self):
                "Convert to a Tuple"
                return (self.real, self.imag)


            def __add__(self, other):
                "Complex addition"
                return Complex(self.real + other.real, self.imag + other.imag)


            def __mul__(self, other):
                "Complex multiplication"
                return Complex(self.real * other.real - self.imag * other.imag,
                               self.real * other.imag + self.imag * other.real)

            def __getitem__(self, idx):
                return (self.real, self.imag)[idx]

            def __len__(self):
                return 2

            def magnitude(self):
                "Complex magnitude"
                return (self.real * self.real) + (self.imag * self.imag)


            # Other complex operators would go here. The above are
            # sufficient for this example.


        # Let's use the Complex struct to compute a Mandelbrot set.
        mandelbrot = Func()

        # The initial complex value corresponding to an x, y coordinate
        # in our Func.
        initial = Complex(x/15.0 - 2.5, y/6.0 - 2.0)

        # Pure definition.
        t = Var("t")
        mandelbrot[x, y, t] = Complex(0.0, 0.0)

        # We'll use an update definition to take 12 steps.
        r = RDom(1, 12)
        current = Complex(mandelbrot[x, y, r-1])

        # The following line uses the complex multiplication and
        # addition we defined above.
        mandelbrot[x, y, r] = (Complex(current*current) + initial)

        # We'll use another tuple reduction to compute the iteration
        # number where the value first escapes a circle of radius 4.
        # This can be expressed as an argmin of a boolean - we want
        # the index of the first time the given boolean expression is
        # false (we consider false to be less than true).  The argmax
        # would return the index of the first time the expression is
        # true.

        escape_condition = Complex(mandelbrot[x, y, r]).magnitude() < 16.0
        first_escape = argmin(escape_condition)
        assert type(first_escape) is tuple
        # We only want the index, not the value, but argmin returns
        # both, so we'll index the argmin Tuple expression using
        # square brackets to get the Expr representing the index.
        escape = Func()
        escape[x, y] = first_escape[0]

        # Realize the pipeline and print the result as ascii art.
        result = escape.realize(61, 25)
        assert type(result) is Buffer_int32
        code = " .:-~*={&%#@"
        for yy in range(result.height()):
            for xx in range(result.width()):
                index = result(xx, yy)
                if index < len(code):
                    print("%c" % code[index], end="")
                else:
                    pass # is lesson 13 cpp version buggy ?
            print("\n")


    print("Success!")

    return 0


if __name__ == "__main__":
    main()
