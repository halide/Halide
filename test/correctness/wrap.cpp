#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
	{
	    Func f("f"), g("g");
	    Var x("x"), y("y");

	    f(x, y) = x + y;

	    g(x, y) = f(x, y);
	    g.wrap(f).compute_root();

	    RDom r(0, 100, 0, 100);
	    r.where(r.x < r.y);
	    g(r.x, r.y) += 2*f(r.x, r.y);

	    f.compute_root();

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
	}

    printf("Success!\n");
    return 0;
}



