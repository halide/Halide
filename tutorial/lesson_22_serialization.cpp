// Halide tutorial lesson 22: Serialization

// This lesson describes how to serialize pipelines into a binary format 
// which can be saved on disk, and later deserialized and loaded for 
// evaluation.

// Note that you'll need to be using a build of Halide that was configured
// using the WITH_SERIALIZATION=ON macro defined in order for this tutorial
// to work.

// On linux, you can compile this tutorial and run it like so:
// g++ lesson_22*.cpp -g -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_22 -std=c++17
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_22

// On os x:
// g++ lesson_22*.cpp -g -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -o lesson_22 -std=c++17
// DYLD_LIBRARY_PATH=<path/to/libHalide.dylib> ./lesson_22

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_22_serialization
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <algorithm>
#include <stdio.h>
using namespace Halide;

void print_ascii(Buffer<int> result) {
    const char code[] = " .:-~*={}&%#@";
    const size_t code_count = sizeof(code) / sizeof(char);
    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            int value = result(x, y);
            if(value < code_count) {
                printf("%c", code[value]);
            } else {
                printf("X");
            }
        }
        printf("\n");
    }
}

int main(int argc, char **argv) {

    int width = 64;
    int height = 32;

    // Let's create a reasonably complicated Pipeline that computes a Julia Set fractal
    {
        struct Complex {
            Expr real, imag;

            // Construct from a Tuple
            Complex(Tuple t)
                : real(t[0]), imag(t[1]) {
            }

            // Construct from a pair of Exprs
            Complex(Expr r, Expr i)
                : real(r), imag(i) {
            }

            // Construct from a call to a Func by treating it as a Tuple
            Complex(FuncRef t)
                : Complex(Tuple(t)) {
            }

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

        // Let's use the Complex struct to compute a Julia set.
        Func julia;
        Var x, y;

        // Lets define the coordinate mapping from pixel coordinates to values in the complex plane
        Complex extent(2.0f, 2.0f);
        Expr scale = max(extent.real / width, extent.imag / height);
        Complex position(scale * (x - cast<float>(width) / 2.0f), scale * (y - cast<float>(height) / 2.0f));
        
        // Let's center the fractal around a pretty position in the complex plane
        Complex initial(-0.79f, 0.15f); 

        // Pure definition.
        Var t;
        julia(x, y, t) = position;

        // We'll use an update definition to take 12 steps.
        RDom r(1, 12);
        Complex current = julia(x, y, r - 1);

        // The following line uses the complex multiplication and
        // addition we defined above.
        julia(x, y, r) = current * current + initial;

        // We'll use another tuple reduction to compute the iteration
        // number where the value first escapes a circle of radius 2.
        // This can be expressed as an argmin of a boolean - we want
        // the index of the first time the given boolean expression is
        // false (we consider false to be less than true).  The argmax
        // would return the index of the first time the expression is
        // true.
        Expr escape_condition = Complex(julia(x, y, r)).magnitude_squared() < 4.0f;
        Tuple first_escape = argmin(escape_condition);

        // We only want the index, not the value, but argmin returns
        // both, so we'll index the argmin Tuple expression using
        // square brackets to get the Expr representing the index.
        Func escape;
        escape(x, y) = first_escape[0];

        // Now lets serialize the pipeline to disk (must use the .hlpipe file extension)
        std::map<std::string, Internal::Parameter> params; // params are not used in this example
        serialize_pipeline(escape, "julias.hlpipe", params);
    }

    // new scope ... everything above is now destroyed!
    {
        // Lets construct a new pipeline from scratch by deserializing the file we wrote to disk
        std::map<std::string, Internal::Parameter> params; // params are not used in this example
        Pipeline deserialized = deserialize_pipeline("julias.hlpipe", params);

        // Now lets realize it ... and print the results as ascii art
        Buffer<int> result = deserialized.realize({width, height});
        print_ascii(result);
    }

    // new scope ... everything above is now destroyed!
    {
        // Lets do the same thing again ... construct a new pipeline from scratch by deserializing the file we wrote to disk
        std::map<std::string, Internal::Parameter> params; // params are not used in this example
        Pipeline julia = deserialize_pipeline("julias.hlpipe", params);

        // Now, lets serialize it to an in memory buffer ... rather than writing it to disk
        std::vector<uint8_t> data;
        serialize_pipeline(julia, data, params);

        // Now lets deserialize it ... and run it!
        Pipeline deserialized = deserialize_pipeline(data, params);
        Buffer<int> result = deserialized.realize({width, height});
    }

    printf("Success!\n");
    return 0;
}
