#include <cassert>
#include <stdio.h>
#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    // Implicit based input function used in pointwise function
    {
	Var x("x"), y("y");
	IVar x_implicit("x_implicit"), y_implicit("y_implicit");
	Func input("input"), f("f"), g("g"), h("h"), output("output");

	input(x, y) = x + y * 256;

	f() = input(x_implicit, y_implicit);

	g() = f() + 42;
	h(x, y) = input(x, y) * 2;
	output(x_implicit, y_implicit) = g() + h(x_implicit, y_implicit);

	Image<int32_t> result = output.realize(10, 10);
	// TODO: change this to assertions
	for (int y = 0; y < 10; y++) {
	    for (int x = 0; x < 10; x++) {
		printf("%d ", result(x, y));
	    }
	    printf("\n");
	}
    }

    // Implicit based input function used in reduction
    {
	Var x("x"), y("y");
	IVar x_implicit("x_implicit"), y_implicit("y_implicit");
	Func input("input"), f("f"), g("g"), h("h"), output("output");

	Func hist_in("hist_in");
	hist_in(x, y) = cast<uint8_t>(x + 3 * x_implicit + 5 * (y + 3 * y_implicit)) & ~1;

	Var bin;
	Func histogram("histogram");
	RDom range(0, 3, 0, 3);
	histogram(hist_in(range.x, range.y)) += 1;
	histogram.compute_root();

	output(x_implicit, y_implicit, bin) = histogram(bin);
	Image<int32_t> hists = output.realize(2, 2, 31);

	// TODO: change this to assertions
	for (int y = 0; y < 2; y++) {
	    for (int x = 0; x < 2; x++) {
		for (int bin = 0; bin < 31; bin++) {
		    printf("%d ", hists(x, y, bin));
		}
		printf("\n");
	    }
	}
    }

    // Implicit used in expression only
    {
        Var x("x");
	IVar y("y");
	Func whythoff("whythoff"), row("row");
	RDom r(0, 10);

	Expr psi = (float)(1 + sqrt(5.0f)) / 2.0f;

	row(x) = 0;
	row(r.x) = select(r.x == 1, cast<int32_t>(floor(floor(y * psi) * psi)),
			select(r.x == 2, cast<int32_t>(floor(floor(y * psi) * psi * psi)),
			       row(r.x - 2) + row(r.x - 1)));
	whythoff(x, y) = row(x);

	Image<int32_t> result = whythoff.realize(10, 10);
	// TODO: change this to assertions
	for (int y = 0; y < 10; y++) {
	    for (int x = 0; x < 10; x++) {
	        printf("%d ", result(x, y));
	    }
	    printf("\n");
	}
    }

    // Implicit used in where clause of RDom
    {
        Var k("k");
	IVar n("n");
	RDom rk(1, 10);
	rk.where(rk.x <= n);

	Func fact("fact");
	fact(k) = 1;
	fact(rk.x) = rk.x * fact(rk.x - 1);

	Func pascal("pascal");
	pascal(k) = 0;
	pascal(0) = 1;
	pascal(rk.x) = fact(n) / (fact(rk.x) * fact(n - rk.x));

	Func pascal_unwrap("pascal_wrap");
	pascal_unwrap(k, n) = pascal(k);

	Image<int32_t> result = pascal_unwrap.realize(10, 10);
	// TODO: change this to assertions
	for (int y = 0; y < 10; y++) {
	    int field_width = (9 - y) * 2 + 1;
	    for (int x = 0; x <= y; x++) {
	      printf("%*d", field_width, result(x, y));
	      field_width = 4;
	    }
	    printf("\n");
	}
    }

    // Implicit used with define_extern
    {
    }

    printf("Success!\n");
    return 0;
}
