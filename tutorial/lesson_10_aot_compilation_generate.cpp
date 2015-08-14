// Halide tutorial lesson 10: AOT compilation part 1

// This lesson demonstrates how to use Halide as an more traditional
// ahead-of-time (AOT) compiler.

// This lesson is split across two files. The first (this one), builds
// a Halide pipeline and compiles it to an object file and header. The
// second (lesson_10_aot_compilation_run.cpp), uses that object file
// to actually run the pipeline. This means that compiling this code
// is a multi-step process.

// On linux, you can compile and run it like so:
// g++ lesson_10*generate.cpp -g -std=c++11 -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_10_generate
// LD_LIBRARY_PATH=../bin ./lesson_10_generate
// g++ lesson_10*run.cpp lesson_10_halide.o -lpthread -o lesson_10_run
// ./lesson_10_run

// On os x:
// g++ lesson_10*generate.cpp -g -std=c++11 -I ../include -L ../bin -lHalide -o lesson_10_generate
// DYLD_LIBRARY_PATH=../bin ./lesson_10_generate
// g++ lesson_10*run.cpp lesson_10_halide.o -o lesson_10_run
// ./lesson_10_run

// The benefits of this approach are that the final program:
// - Doesn't do any jit compilation at runtime, so it's fast.
// - Doesn't depend on libHalide at all, so it's a small, easy-to-deploy binary.

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_10_aot_compilation_run
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>
using namespace Halide;

int main(int argc, char **argv) {
	Var x, y, c;
	ImageParam ip(type_of<uint8_t>(), 3);

	Func result;
	Func bcip;
	// causes an internal assert if combined with vectorize in schedule...
	bcip = BoundaryConditions::constant_exterior(ip, 0);

	// totally works.
	// bcip = BoundaryConditions::repeat_edge(ip);

	// compiles, correctly errors out due to out of bounds indexing
	// bcip(x, y, c) = ip(x, y, c);

	Param<int> width(7, 1, 40);

	RDom r(0, width, 0, width);

	result(x, y, c) = cast<uint8_t>(
		sum(cast<float>(bcip(
		  (x / width) * width + r.x,
		  (y / width) * width + r.y,
		  c
		))) / (width * width)
	);

	// the schedule should really be in width x width tiles and compute
	// the answer for each one only once (since they're all the same)
	result.parallel(y, 4).vectorize(x, 4);

    std::vector<Argument> args = {ip, width};
    result.compile_to_file("lesson_10_halide", args);

    printf("Halide pipeline compiled, but not yet run.\n");

    // To continue this lesson, look in the file lesson_10_aot_compilation_run.cpp

    return 0;
}
