#include "Halide.h"
#include <iostream>

#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int global_to_prevent_opt;
int null_call() {
    return global_to_prevent_opt;
}

int main(int argc, char **argv) {
    {
	global_to_prevent_opt = argc;
	double t = benchmark([&]() { null_call(); });
	std::cout << "No argument native call time " << t * 1e6 << "us.\n";
    }

    {
	Func f;
	f() = 42;

	f.compile_jit();

	double t = benchmark([&]() { f.realize(); });
	std::cout << "No argument Func realize time " << t * 1e6 << "us.\n";
    }

    {
	Func f;
	f() = 42;

	Pipeline p(f);
	p.compile_jit();

	double t = benchmark([&]() { p.realize(); });
	std::cout << "No argument Pipeline realize time " << t * 1e6 << "us.\n";
    }

    {
	Func f;
	f() = 42;

	Pipeline p(f);
	p.compile_jit();

	Buffer<int32_t> buf(1);
	Realization r(buf);
	Target target;
	ParamMap pm;
	double t = benchmark([&]() { p.realize(r, target, pm); });
	std::cout << "No argument Pipeline realize reusing Realization/Target/ParamMap time " << t * 1e6 << "us.\n";
    }

    {
	Func f;
	f() = 42;

	Pipeline p(f);
	p.compile_jit();

	Buffer<int32_t> buf(1);
	Realization r(buf);
	Target target("host-no_asserts-no_bounds_query");
	ParamMap pm;
	double t = benchmark([&]() { p.realize(r, target, pm); });
	std::cout << "No argument Pipeline realize reusing Realization/Target/ParamMap with no_asserts and no_bounds_query time " << t * 1e6 << "us.\n";
    }

    {
	Func f;
	Param<int> in;

	f() = in + 42;

	in.set(0);

	Buffer<int32_t> buf(1);
	double t = benchmark([&]() { f.realize(buf); });
	std::cout << "One argument Func realize to Buffer time " << t * 1e6 << "us.\n";
    }

    {
	Func f;
	Param<int> in;

	f() = in + 42;

	in.set(0);

	Pipeline p(f);
	p.compile_jit();

	Buffer<int32_t> buf(1);
	Realization r(buf);
	Target target;
	ParamMap pm;
	double t = benchmark([&]() { p.realize(r, target, pm); });
	std::cout << "One argument Pipeline realize reusing Realization/Target/ParamMap time " << t * 1e6 << "us.\n";
    }

    return 0;
}
