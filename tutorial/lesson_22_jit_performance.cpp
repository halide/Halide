// Halide tutorial lesson 22: JIT compilation performance

// This lesson demonstrates the various performance implications of the
// various Halide methods of doing "Just-In-Time" compilation.

// On linux, you can compile and run it like so:
// g++ lesson_22*.cpp -g -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_22 -std=c++17
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_20

// On os x:
// g++ lesson_22*.cpp -g -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -o lesson_22 -std=c++17
// DYLD_LIBRARY_PATH=<path/to/libHalide.dylib> ./lesson_22

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_22_jit_performance
// in a shell at the top of the halide source tree.

#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Tools; // for benchmark()

// Lets define a helper function to construct a simple pipeline that we'll use for our performance tests
Pipeline make_pipeline() {

    // We'll start with a simple transpose operation 
    Func input("input"), output("output");
    Var x("x"), y("y");

    // Fill the input with a linear combination of the coordinate values
    input(x, y) = cast<uint16_t>(x + y);
    input.compute_root();

    // Transpose the rows and cols 
    output(x, y) = input(y, x);

    // Schedule it ... there's a number of possibilities here to do an efficient block-wise transpose
    Var xi("xi"), yi("yi");
    
    // Let's focus on 8x8 subtiles, and then vectorize across X, and unroll across Y
    output.tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi);

    // For more advanced scheduling: 
    //
    // We can improve this even more by using the .in() directive (see Tutorial 19), 
    // which allows us to interpose new Funcs in between input and output.
    // 
    // Here we can inject a block_transpose function to allow us to do 8 vectorized loads from the input.
    // Func block_transpose("block_transpose"), block("block");
    // block_transpose = input.in(output).compute_at(output, x).vectorize(x).unroll(y);
    //
    // And now lets reorder and vectorize in X across the block 
    // block = block_transpose.in(output).reorder_storage(y, x).compute_at(output, x).vectorize(x).unroll(y);

    return Pipeline(output);
}

int main(int argc, char **argv) {

    
    // Lets measure the performance of calling realize on a simple pipeline
    {
        Pipeline pipeline = make_pipeline();

        // Create an 1024x1024 output buffer to hold the results 
        Buffer<uint16_t> result(1024, 1024);

        // Lets realize the pipeline (which implicitly JIT compiles, dynamically links and executes the code)
        size_t count = 0;
        double t = benchmark([&]() {
            pipeline.realize(result);
            ++count;
        });

        std::cout << "Execute Pipeline (realize): " << int(count / t) << " times/sec\n";
    }

    // This time let's explicitly invoke the JIT compile, before we realize and execute the pipeline
    {
        Pipeline pipeline = make_pipeline();
        Buffer<uint16_t> result(1024, 1024);

        // Let's explicitly JIT compile before we realize, which will cache the generated code
        // associated with the Pipeline object. Now when we call realize, there's no code generation 
        // overhead when we execute the pipeline.
        pipeline.compile_jit();

        size_t count = 0;
        double t = benchmark([&]() {
            pipeline.realize(result);
            ++count;
        });

        std::cout << "Execute Pipeline (compile before realize): " << int(count / t) << " times/sec\n";
    }

    // Alternatively we could compile to a Callable object ...
    {
        Pipeline pipeline = make_pipeline();
        Buffer<uint16_t> result(1024, 1024);

        auto arguments = pipeline.infer_arguments();
        const Target target = get_jit_target_from_environment();
        Callable callable = pipeline.compile_to_callable(arguments, target);

        // The Callable object acts as a convienient way of invoking the compiled code like
        // a function call, using an argv like syntax for the argument list. It also caches 
        // the JIT compiled code, so there's no code generation overhead when invoking the
        // callable object and executing the pipeline.

        size_t count = 0;
        double t = benchmark([&]() {
            callable(result);
            ++count;
        });

        std::cout << "Execute Pipeline (compile to callable): " << int(count / t) << " times/sec\n";
    }

    // Let's see how much time is spent on just compiling ...
    {
        Pipeline pipeline = make_pipeline();
        Buffer<uint16_t> result(1024, 1024);

        // Only the first call to compile_jit() is expensive ... after the code is generated,
        // it gets stored in a cache for later re-use, so repeatedly calling compile_jit has
        // very little overhead after its been cached.

        size_t count = 0;
        double t = benchmark([&]() {
            pipeline.compile_jit();
            ++count;
        });

        std::cout << "Compile JIT (using cache): " << int(count / t) << " times/sec\n";

        // You can invalidate the cache manually, which will destroy all the compiled state.
        count = 0;
        t = benchmark([&]() {
            pipeline.invalidate_cache();
            pipeline.compile_jit();
            ++count;
        });
        std::cout << "Compile JIT (from scratch): " << int(count / t) << " times/sec\n";

    }

    // Alternatively we could compile to a Module ...
    {
        Pipeline pipeline = make_pipeline();
        Buffer<uint16_t> result(1024, 1024);
        auto args = pipeline.infer_arguments();

        // Compiling to a module generates a self-contained Module containing an internal-representation
        // of the lowered code suitable for further compilation. So, it's not directly
        // runnable, but it can be used to link/combine Modules and generate object files,
        // static libs, bitcode, etc.

        size_t count = 0;
        double t = benchmark([&]() {
            Module m = pipeline.compile_to_module(args, "transpose");
            ++count;
        });

        std::cout << "Compile to Module: " << int(count / t) << " times/sec\n";
    }


    printf("DONE!\n");
    return 0;
}
