// Halide tutorial lesson 22: JIT compilation performance

// This lesson demonstrates the various performance implications of the
// various Halide methods of doing "Just-In-Time" compilation.

// On linux, you can compile and run it like so:
// g++ lesson_22*.cpp -g -I <path/to/Halide.h> -I <path/to/tools/halide_benchmark.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_22 -std=c++17
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_20

// On os x:
// g++ lesson_22*.cpp -g -I <path/to/Halide.h> -I <path/to/tools/halide_benchmark.h> -L <path/to/libHalide.so> -lHalide -o lesson_22 -std=c++17
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

// Let's define a helper function to construct a simple pipeline that we'll use for our performance tests.
Pipeline make_pipeline() {
    // We'll start with a simple transpose operation...
    Func input("input"), output("output");
    Var x("x"), y("y");

    // Fill the input with a linear combination of the coordinate values...
    input(x, y) = cast<uint16_t>(x + y);
    input.compute_root();

    // Transpose the rows and cols 
    output(x, y) = input(y, x);

    // Schedule it ... there's a number of possibilities here to do an efficient block-wise transpose.
    Var xi("xi"), yi("yi");
    
    // Let's focus on 8x8 subtiles, and then vectorize across X, and unroll across Y.
    output.tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi);

    // For more advanced scheduling: 
    //
    // We can improve this even more by using the .in() directive (see Tutorial 19), 
    // which allows us to interpose new Funcs in between input and output.
    // 
    // Here we can inject a block_transpose function to allow us to do 8 vectorized loads from the input.
    Func block_transpose("block_transpose"), block("block");
    block_transpose = input.in(output).compute_at(output, x).vectorize(x).unroll(y);
    //
    // And now Let's reorder and vectorize in X across the block.
    block = block_transpose.in(output).reorder_storage(y, x).compute_at(output, x).vectorize(x).unroll(y);

    // Return the constructed pipeline
    return Pipeline(output);
}

int main(int argc, char **argv) {
    // Since we'll be using the same sample and iteration counts for our benchmarking,
    // let's define them here in the outermost scope.
    constexpr int samples = 100;
    constexpr int iterations = 1;
    
    // Now, let's measure the performance of constructing and executing a simple pipeline from scratch...
    {
        size_t count = 0;
        double t = benchmark(samples, iterations, [&]() {

            // First, create an output buffer to hold the results.
            Buffer<uint16_t> result(1024, 1024);
            
            // Now, construct our pipeline from scratch.
            Pipeline pipeline = make_pipeline();

            // And then call realize to execute the pipeline.
            pipeline.realize(result);
            ++count;
        });

        // On a MacBook Pro M1, we should get around ~1800 times/sec.
        std::cout << "Compile & Execute Pipeline (from scratch): " << int(count / t) << " times/sec\n";
    }

    // This time, let's create the pipeline outside the timing loop and re-use it for each execution...
    {
        // Create our pipeline, and re-use it in the loop below
        Pipeline pipeline = make_pipeline();

        size_t count = 0;
        double t = benchmark(samples, iterations, [&]() {

            // Create our output buffer
            Buffer<uint16_t> result(1024, 1024);
            
            // Now, call realize
            pipeline.realize(result);
            ++count;
        });

        // On a MacBook Pro M1, we should get around ~175000 times/sec (almost 95-100x times faster!).
        std::cout << "Compile & Execute Pipeline (re-use pipeline): " << int(count / t) << " times/sec\n";
    }

    // Let's do the same thing as before, but explicitly JIT compile before we realize...
    {
        Pipeline pipeline = make_pipeline();

        // Let's JIT compile for our target before we realize, and see what happens...
        const Target target = get_jit_target_from_environment();
        pipeline.compile_jit(target);

        size_t count = 0;
        double t = benchmark(samples, iterations, [&]() {
            Buffer<uint16_t> result(1024, 1024);
            pipeline.realize(result);
            ++count;
        });
 
        // On a MacBook Pro M1, this should be about the same as the previous run (about ~175000 times/sec)
        //
        // This may seem somewhat surprising, since compiling before realizing doesn't seem to make 
        // much of a difference to the previous case.  However, the first call to realize() will implicitly
        // JIT-compile and cache the generated code associated with the Pipeline object, which is basically 
        // what we've done here. Each subsequent call to realize uses the cached version of the native code, 
        // so there's no additional overhead, and the cost is amortized as we re-use the pipeline.
        std::cout << "Execute Pipeline (compile before realize): " << int(count / t) << " times/sec\n";

        // Another subtlety is the creation of the result buffer ... the declaration implicitly
        // allocates memory which will add overhead to each loop iteration. This time, let's try 
        // using the realize({1024, 1024}) call which will use the buffer managed by the pipeline 
        // object for the outputs...
        count = 0;
        t = benchmark(samples, iterations, [&]() {
            Buffer<uint16_t> result = pipeline.realize({1024, 1024});
            ++count;
        });

        // On a MacBook Pro M1, this should be about the same as the previous run (about ~175000 times/sec).
        std::cout << "Execute Pipeline (same but with realize({})): " << int(count / t) << " times/sec\n";

        // Or ... we could move the declaration of the result buffer outside the timing loop, and
        // re-use the allocation (with the caveat that we will be stomping over its contents on each 
        // execution).
        Buffer<uint16_t> result(1024, 1024);

        count = 0;
        t = benchmark(samples, iterations, [&]() {
            pipeline.realize(result);
            ++count;
        });

        // On a MacBook Pro M1, this should be much more efficient ... ~200000 times/sec (or 10-12% faster).
        std::cout << "Execute Pipeline (re-use buffer with realize): " << int(count / t) << " times/sec\n";
    }

    // Alternatively, we could compile to a Callable object...
    {
        Pipeline pipeline = make_pipeline();
        const Target target = get_jit_target_from_environment();

        // Here, we can ask the pipeline for its argument list (these are either Params,
        // ImageParams, or Buffers) so that we can construct a Callable object with the same 
        // calling convention.
        auto arguments = pipeline.infer_arguments();

        // The Callable object acts as a convenient way of invoking the compiled code like
        // a function call, using an argv-like syntax for the argument list. It also caches 
        // the JIT compiled code, so there's no code generation overhead when invoking the
        // callable object and executing the pipeline.
        Callable callable = pipeline.compile_to_callable(arguments, target);

        // Again, we'll pre-allocate and re-use the result buffer.
        Buffer<uint16_t> result(1024, 1024);

        size_t count = 0;
        double t = benchmark(samples, iterations, [&]() {
            callable(result);
            ++count;
        });

        // This should be about the same as the previous run (about ~200000 times/sec).
        std::cout << "Execute Pipeline (compile to callable): " << int(count / t) << " times/sec\n";

        // Perhaps even more convient, we can create a std::function object from the callable,
        // which allows cleaner type checking for the parameters, and slightly less overhead
        // for invoking the function. The list used for the template parameters needs to match
        // the list for the parameters of the pipeline.  Here, we have a single result buffer,
        // so we specify Buffer<uint16_t> in our call to .make_std_function<>. If we had other 
        // scalar parameters, input buffers or output buffers, we'd pass them in the template 
        // parameter list too.
        auto function = callable.make_std_function<Buffer<uint16_t>>();

        count = 0;
        t = benchmark(samples, iterations, [&]() {
            function(result);
            ++count;
        });

        // On a MacBook Pro M1, this should be slightly more efficient than the callable (~1% faster).
        std::cout << "Execute Pipeline (compile to std::function): " << int(count / t) << " times/sec\n";
    }

    // Let's see how much time is spent on just compiling...
    {
        Pipeline pipeline = make_pipeline();

        // Only the first call to compile_jit() is expensive ... after the code is generated,
        // it gets stored in a cache for later re-use, so repeatedly calling compile_jit has
        // very little overhead after its been cached.

        size_t count = 0;
        double t = benchmark(samples, iterations, [&]() {
            pipeline.compile_jit();
            ++count;
        });

        // Only the first call does any work and the rest are essentially free.
        // On a MacBook Pro M1, we should expect ~2 billion times/sec.
        std::cout << "Compile JIT (using cache): " << int(count / t) << " times/sec\n";

        // You can invalidate the cache manually, which will destroy all the compiled state.
        count = 0;
        t = benchmark(samples, iterations, [&]() {
            pipeline.invalidate_cache();
            pipeline.compile_jit();
            ++count;
        });

        // This is an intentionally expensive loop, and very slow!
        // On a MacBook Pro M1, we should see only ~2000 times/sec.
        std::cout << "Compile JIT (from scratch): " << int(count / t) << " times/sec\n";
    }

    // Alternatively we could compile to a Module...
    {
        Pipeline pipeline = make_pipeline();
        auto args = pipeline.infer_arguments();

        // Compiling to a module generates a self-contained Module containing an internal-representation
        // of the lowered code suitable for further compilation. So, it's not directly
        // runnable, but it can be used to link/combine Modules and generate object files,
        // static libs, bitcode, etc.

        size_t count = 0;
        double t = benchmark(samples, iterations, [&]() {
            Module m = pipeline.compile_to_module(args, "transpose");
            ++count;
        });

        // On a MacBook Pro M1, this should be around ~10000 times/sec
        std::cout << "Compile to Module: " << int(count / t) << " times/sec\n";
    }

    printf("DONE!\n");
    return 0;
}
