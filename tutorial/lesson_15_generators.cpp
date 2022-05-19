// Halide tutorial lesson 15: Generators part 1

// This lesson demonstrates how to encapsulate Halide pipelines into
// reusable components called generators.

// On linux, you can compile and run it like so:
// g++ lesson_15*.cpp <path/to/tools/halide_image_io.h>/GenGen.cpp -g -std=c++17 -fno-rtti -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_15_generate
// bash lesson_15_generators_usage.sh

// On os x:
// g++ lesson_15*.cpp <path/to/tools/halide_image_io.h>/GenGen.cpp -g -std=c++17 -fno-rtti -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -o lesson_15_generate
// bash lesson_15_generators_usage.sh

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_15_generators
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Generators are a more structured way to do ahead-of-time
// compilation of Halide pipelines. Instead of writing an int main()
// with an ad-hoc command-line interface like we did in lesson 10, we
// define a class that inherits from Halide::Generator.
class MyFirstGenerator : public Halide::Generator<MyFirstGenerator> {
public:
    // We declare the Inputs to the Halide pipeline as public
    // member variables. They'll appear in the signature of our generated
    // function in the same order as we declare them.
    Input<uint8_t> offset{"offset"};
    Input<Buffer<uint8_t, 2>> input{"input"};

    // We also declare the Outputs as public member variables.
    Output<Buffer<uint8_t, 2>> brighter{"brighter"};

    // Typically you declare your Vars at this scope as well, so that
    // they can be used in any helper methods you add later.
    Var x, y;

    // We then define a method that constructs and return the Halide
    // pipeline:
    void generate() {
        // In lesson 10, here is where we called
        // Func::compile_to_file. In a Generator, we just need to
        // define the Output(s) representing the output of the pipeline.
        brighter(x, y) = input(x, y) + offset;

        // Schedule it.
        brighter.vectorize(x, 16).parallel(y);
    }
};

// We compile this file along with tools/GenGen.cpp. That file defines
// an "int main(...)" that provides the command-line interface to use
// your generator class. We need to tell that code about our
// generator. We do this like so:
HALIDE_REGISTER_GENERATOR(MyFirstGenerator, my_first_generator)

// If you like, you can put multiple Generators in the one file. This
// could be a good idea if they share some common code. Let's define
// another more complex generator:
class MySecondGenerator : public Halide::Generator<MySecondGenerator> {
public:
    // This generator will take some compile-time parameters
    // too. These let you compile multiple variants of a Halide
    // pipeline. We'll define one that tells us whether or not to
    // parallelize in our schedule:
    GeneratorParam<bool> parallel{"parallel", /* default value */ true};

    // ... and another representing a constant scale factor to use:
    GeneratorParam<float> scale{"scale",
                                1.0f /* default value */,
                                0.0f /* minimum value */,
                                100.0f /* maximum value */};

    // You can define GeneratorParams of all the basic scalar
    // types. For numeric types you can optionally provide a minimum
    // and maximum value, as we did for scale above.

    // You can also define GeneratorParams for enums. To make this
    // work you must provide a mapping from strings to your enum
    // values.
    enum class Rotation { None,
                          Clockwise,
                          CounterClockwise };
    GeneratorParam<Rotation> rotation{"rotation",
                                      /* default value */
                                      Rotation::None,
                                      /* map from names to values */
                                      {{"none", Rotation::None},
                                       {"cw", Rotation::Clockwise},
                                       {"ccw", Rotation::CounterClockwise}}};

    // We'll use the same Inputs as before:
    Input<uint8_t> offset{"offset"};
    Input<Buffer<uint8_t, 2>> input{"input"};

    // And a similar Output. Note that we don't specify a type for the Buffer:
    // at compile-time, we must specify an explicit type via the "output.type"
    // GeneratorParam (which is implicitly defined for this Output).
    Output<Buffer<void, 2>> output{"output"};

    // And we'll declare our Vars here as before.
    Var x, y;

    void generate() {
        // Define the Func. We'll use the compile-time scale factor as
        // well as the runtime offset param.
        Func brighter;
        brighter(x, y) = scale * (input(x, y) + offset);

        // We'll possibly do some sort of rotation, depending on the
        // enum. To get the value of a GeneratorParam, cast it to the
        // corresponding type. This cast happens implicitly most of
        // the time (e.g. with scale above).

        Func rotated;
        switch ((Rotation)rotation) {
        case Rotation::None:
            rotated(x, y) = brighter(x, y);
            break;
        case Rotation::Clockwise:
            rotated(x, y) = brighter(y, 100 - x);
            break;
        case Rotation::CounterClockwise:
            rotated(x, y) = brighter(100 - y, x);
            break;
        }

        // We'll then cast to the desired output type.
        output(x, y) = cast(output.type(), rotated(x, y));

        // The structure of the pipeline depended on the generator
        // params. So will the schedule.

        // Let's start by vectorizing the output. We don't know the
        // type though, so it's hard to pick a good factor. Generators
        // provide a helper called "natural_vector_size" which will
        // pick a reasonable factor for you given the type and the
        // target you're compiling to.
        output.vectorize(x, natural_vector_size(output.type()));

        // Now we'll possibly parallelize it:
        if (parallel) {
            output.parallel(y);
        }

        // If there was a rotation, we'll schedule that to occur per
        // scanline of the output and vectorize it according to its
        // type.
        if (rotation != Rotation::None) {
            rotated
                .compute_at(output, y)
                .vectorize(x, natural_vector_size(rotated.types()[0]));
        }
    }
};

// Register our second generator:
HALIDE_REGISTER_GENERATOR(MySecondGenerator, my_second_generator)

// After compiling this file, see how to use it in
// lesson_15_generators_build.sh
