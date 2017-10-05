// Halide tutorial lesson 13: Tuples

// This lesson describes how to write Funcs that evaluate to multiple
// values.

// On linux, you can compile and run it like so:
// g++ lesson_13*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_13 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_13

// On os x:
// g++ lesson_13*.cpp -g -I ../include -L ../bin -lHalide -o lesson_13 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_13

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_13_tuples
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>
#include <algorithm>
using namespace Halide;

int main(int argc, char **argv) {

    // So far Funcs (such as the one below) have evaluated to a single
    // scalar value for each point in their domain.
    Func single_valued;
    Var x, y;
    single_valued(x, y) = x + y;

    // One way to write a Func that returns a collection of values is
    // to add an additional dimension that indexes that
    // collection. This is how we typically deal with color. For
    // example, the Func below represents a collection of three values
    // for every x, y coordinate indexed by c.
    Func color_image;
    Var c;
    color_image(x, y, c) = select(c == 0, 245, // Red value
                                  c == 1, 42,  // Green value
                                  132);        // Blue value

    // This method is often convenient because it makes it easy to
    // operate on this Func in a way that treats each item in the
    // collection equally:
    Func brighter;
    brighter(x, y, c) = color_image(x, y, c) + 10;

    // However this method is also inconvenient for three reasons.
    //
    // 1) Funcs are defined over an infinite domain, so users of this
    // Func can for example access color_image(x, y, -17), which is
    // not a meaningful value and is probably indicative of a bug.
    //
    // 2) It requires a select, which can impact performance if not
    // bounded and unrolled:
    // brighter.bound(c, 0, 3).unroll(c);
    //
    // 3) With this method, all values in the collection must have the
    // same type. While the above two issues are merely inconvenient,
    // this one is a hard limitation that makes it impossible to
    // express certain things in this way.

    // It is also possible to represent a collection of values as a
    // collection of Funcs:
    Func func_array[3];
    func_array[0](x, y) = x + y;
    func_array[1](x, y) = sin(x);
    func_array[2](x, y) = cos(y);

    // This method avoids the three problems above, but introduces a
    // new annoyance. Because these are separate Funcs, it is
    // difficult to schedule them so that they are all computed
    // together inside a single loop over x, y.

    // A third alternative is to define a Func as evaluating to a
    // Tuple instead of an Expr. A Tuple is a fixed-size collection of
    // Exprs. Each Expr in a Tuple may have a different type. The
    // following function evaluates to an integer value (x+y), and a
    // floating point value (sin(x*y)).
    Func multi_valued;
    multi_valued(x, y) = Tuple(x + y, sin(x * y));

    // Realizing a tuple-valued Func returns a collection of
    // Buffers. We call this a Realization. It's equivalent to a
    // std::vector of Buffer objects:
    {
        Realization r = multi_valued.realize(80, 60);
        assert(r.size() == 2);
        Buffer<int> im0 = r[0];
        Buffer<float> im1 = r[1];
        assert(im0(30, 40) == 30 + 40);
        assert(im1(30, 40) == sinf(30 * 40));
    }

    // All Tuple elements are evaluated together over the same domain
    // in the same loop nest, but stored in distinct allocations. The
    // equivalent C++ code to the above is:
    {
        int multi_valued_0[80*60];
        float multi_valued_1[80*60];
        for (int y = 0; y < 80; y++) {
            for (int x = 0; x < 60; x++) {
                multi_valued_0[x + 60*y] = x + y;
                multi_valued_1[x + 60*y] = sinf(x*y);
            }
        }
    }

    // When compiling ahead-of-time, a Tuple-valued Func evaluates
    // into multiple distinct output buffer_t structs. These appear in
    // order at the end of the function signature:
    // int multi_valued(...input buffers and params...,
    //                  buffer_t *output_1, buffer_t *output_2);

    // You can construct a Tuple by passing multiple Exprs to the
    // Tuple constructor as we did above. Perhaps more elegantly, you
    // can also take advantage of C++11 initializer lists and just
    // enclose your Exprs in braces:
    Func multi_valued_2;
    multi_valued_2(x, y) = {x + y, sin(x*y)};

    // Calls to a multi-valued Func cannot be treated as Exprs. The
    // following is a syntax error:
    // Func consumer;
    // consumer(x, y) = multi_valued_2(x, y) + 10;

    // Instead you must index a Tuple with square brackets to retrieve
    // the individual Exprs:
    Expr integer_part = multi_valued_2(x, y)[0];
    Expr floating_part = multi_valued_2(x, y)[1];
    Func consumer;
    consumer(x, y) = {integer_part + 10, floating_part + 10.0f};

    // Tuple reductions.
    {
        // Tuples are particularly useful in reductions, as they allow
        // the reduction to maintain complex state as it walks along
        // its domain. The simplest example is an argmax.

        // First we create a Buffer to take the argmax over.
        Func input_func;
        input_func(x) = sin(x);
        Buffer<float> input = input_func.realize(100);

        // Then we define a 2-valued Tuple which tracks the index of
        // the maximum value and the value itself.
        Func arg_max;

        // Pure definition.
        arg_max() = {0, input(0)};

        // Update definition.
        RDom r(1, 99);
        Expr old_index = arg_max()[0];
        Expr old_max   = arg_max()[1];
        Expr new_index = select(old_max < input(r), r, old_index);
        Expr new_max   = max(input(r), old_max);
        arg_max() = {new_index, new_max};

        // The equivalent C++ is:
        int arg_max_0 = 0;
        float arg_max_1 = input(0);
        for (int r = 1; r < 100; r++) {
            int old_index = arg_max_0;
            float old_max = arg_max_1;
            int new_index = old_max < input(r) ? r : old_index;
            float new_max = std::max(input(r), old_max);
            // In a tuple update definition, all loads and computation
            // are done before any stores, so that all Tuple elements
            // are updated atomically with respect to recursive calls
            // to the same Func.
            arg_max_0 = new_index;
            arg_max_1 = new_max;
        }

        // Let's verify that the Halide and C++ found the same maximum
        // value and index.
        {
            Realization r = arg_max.realize();
            Buffer<int> r0 = r[0];
            Buffer<float> r1 = r[1];
            assert(arg_max_0 == r0(0));
            assert(arg_max_1 == r1(0));
        }

        // Halide provides argmax and argmin as built-in reductions
        // similar to sum, product, maximum, and minimum. They return
        // a Tuple consisting of the point in the reduction domain
        // corresponding to that value, and the value itself. In the
        // case of ties they return the first value found. We'll use
        // one of these in the following section.
    }

    // Tuples for user-defined types.
    {
        // Tuples can also be a convenient way to represent compound
        // objects such as complex numbers. Defining an object that
        // can be converted to and from a Tuple is one way to extend
        // Halide's type system with user-defined types.
        struct Complex {
            Expr real, imag;

            // Construct from a Tuple
            Complex(Tuple t) : real(t[0]), imag(t[1]) {}

            // Construct from a pair of Exprs
            Complex(Expr r, Expr i) : real(r), imag(i) {}

            // Construct from a call to a Func by treating it as a Tuple
            Complex(FuncRef t) : Complex(Tuple(t)) {}

            // Convert to a Tuple
            operator Tuple() const {
                return {real, imag};
            }

            // Complex addition
            Complex operator+(const Complex &other) const {
                return {real + other.real, imag + other.imag};
            }

            // Complex multiplication
            Complex operator*(const Complex &other) const {
                return {real * other.real - imag * other.imag,
                        real * other.imag + imag * other.real};
            }

            // Complex magnitude, squared for efficiency
            Expr magnitude_squared() const {
                return real * real + imag * imag;
            }

            // Other complex operators would go here. The above are
            // sufficient for this example.
        };

        // Let's use the Complex struct to compute a Mandelbrot set.
        Func mandelbrot;

        // The initial complex value corresponding to an x, y coordinate
        // in our Func.
        Complex initial(x/15.0f - 2.5f, y/6.0f - 2.0f);

        // Pure definition.
        Var t;
        mandelbrot(x, y, t) = Complex(0.0f, 0.0f);

        // We'll use an update definition to take 12 steps.
        RDom r(1, 12);
        Complex current = mandelbrot(x, y, r-1);

        // The following line uses the complex multiplication and
        // addition we defined above.
        mandelbrot(x, y, r) = current*current + initial;

        // We'll use another tuple reduction to compute the iteration
        // number where the value first escapes a circle of radius 4.
        // This can be expressed as an argmin of a boolean - we want
        // the index of the first time the given boolean expression is
        // false (we consider false to be less than true).  The argmax
        // would return the index of the first time the expression is
        // true.

        Expr escape_condition = Complex(mandelbrot(x, y, r)).magnitude_squared() < 16.0f;
        Tuple first_escape = argmin(escape_condition);

        // We only want the index, not the value, but argmin returns
        // both, so we'll index the argmin Tuple expression using
        // square brackets to get the Expr representing the index.
        Func escape;
        escape(x, y) = first_escape[0];

        // Realize the pipeline and print the result as ascii art.
        Buffer<int> result = escape.realize(61, 25);
        const char *code = " .:-~*={}&%#@";
        for (int y = 0; y < result.height(); y++) {
            for (int x = 0; x < result.width(); x++) {
                printf("%c", code[result(x, y)]);
            }
            printf("\n");
        }
    }


    printf("Success!\n");

    return 0;
}
