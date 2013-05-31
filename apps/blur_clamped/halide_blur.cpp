#include <Halide.h>
#include <cstdio>
using namespace Halide;

int main(int argc, char **argv) {

    ImageParam input(UInt(16), 3);
    Func blur_x("blur_x"), blur_y("blur_y");
    Var x("x"), y("y"), xi("xi"), yi("yi"), c("c");
    
    // The algorithm
    blur_x(x, y, c) = (input(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c)/3 +
		    input(clamp(x+1, 0, input.width()-1), clamp(y, 0, input.height()-1), c)/3 +
		    input(clamp(x+2, 0, input.width()-1), clamp(y, 0, input.height()-1), c)/3);
    blur_y(x, y, c) = blur_x(x, y, c)/3 + blur_x(x, y+1, c)/3 + blur_x(x, y+2, c)/3;
    
    // How to schedule it
    blur_y.split(y, y, yi, 8).parallel(y).vectorize(x, 8);
    blur_x.store_at(blur_y, y).compute_at(blur_y, yi).vectorize(x, 8);  

    printf("compiling unoptimized version\n");
    setenv("HL_ENABLE_CLAMPED_VECTOR_LOAD", "0", 1);
    blur_y.compile_to_file("halide_blur", input);
    printf("compiling version with clamped vector load enabled\n");
    setenv("HL_ENABLE_CLAMPED_VECTOR_LOAD", "1", 1);
    blur_y.compile_to_file("halide_blur_cvl", input);

    return 0;
}
