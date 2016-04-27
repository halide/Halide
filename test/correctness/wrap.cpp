#include "Halide.h"
#include <map>
#include <stdio.h>

using std::map;
using std::string;
using namespace Halide;

map<string, bool> results;

int my_trace(void *user_context, const halide_trace_event *e) {
    if ((e->event == halide_trace_begin_realization) && results.count(std::string(e->func)))  {
    	results[std::string(e->func)] = true;
    }
    return 0;
}

int func_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    f.compute_root();

    g(x, y) = f(x);
    Func wrapper = g.wrap(f).compute_root();

    g.set_custom_trace(&my_trace);
    wrapper.trace_realizations();

    results.clear();
    results[wrapper.name()] = false;

    Image<int> im = g.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    if (!results[wrapper.name()]) {
    	std::cerr << "Expect " << wrapper.name() << " to be realized\n";
    	return -1;
    }
    return 0;
}

int rdom_after_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    Func wrapper = g.wrap(f).compute_root();

    // Update of 'g' is defined after g.wrap(f) is called. Expect the update
    // to use 'f' instead of its wrapper.
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += 2*f(r.x, r.y);

    g.set_custom_trace(&my_trace);
    wrapper.trace_realizations();

    results.clear();
    results[wrapper.name()] = false;

    Image<int> im = g.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 2*correct : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    if (!results[wrapper.name()]) {
    	std::cerr << "Expect " << wrapper.name() << " to be realized\n";
    	return -1;
    }
    return 0;
}

int image_param_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    Image<int> buf = f.realize(200, 200);
    ImageParam input(Int(32), 2, "input");
    input.set(buf);

    g(x, y) = 2*x;
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += input(r.x, r.y);
	Func wrapper = g.wrap(input).compute_at(g, r.y);

	g.set_custom_trace(&my_trace);
    wrapper.trace_realizations();

    results.clear();
    results[wrapper.name()] = false;

    Image<int> im = g.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = 2*x;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                correct += (x < y) ? x + y : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    if (!results[wrapper.name()]) {
    	std::cerr << "Expect " << wrapper.name() << " to be realized\n";
    	return -1;
    }
    return 0;
}

int rdom_wrapper() {
	// Scheduling initialization + update on the same compute level using wrapper
	Func f("f"), g("g"), result("result");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = 10;
    g(x, y) += 2 * f(x, x);
    g(x, y) += 3 * f(y, y);
    result(x, y) = g(x, y) + 20;
    Func wrapper = result.wrap(g).compute_at(result, x);

    result.set_custom_trace(&my_trace);
    wrapper.trace_realizations();

    results.clear();
    results[wrapper.name()] = false;

    Image<int> im = result.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = 4*x + 6* y + 30;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    if (!results[wrapper.name()]) {
    	std::cerr << "Expect " << wrapper.name() << " to be realized\n";
    	return -1;
    }
    return 0;
}


int main(int argc, char **argv) {
	printf("Running func wrap test\n");
    if (func_wrap_test() != 0) {
        return -1;
    }

	printf("Running rdom is defined after wrap test\n");
    if (rdom_after_wrap_test() != 0) {
        return -1;
    }

    printf("Running image param wrap test\n");
    if (image_param_wrap_test() != 0) {
        return -1;
    }

    printf("Running rdom wrapper test\n");
    if (rdom_wrapper() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}



