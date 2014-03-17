#include <Halide.h>
#include <stdio.h>

using namespace Halide;

void set_pixels() {
    Func f;
    Var x, y, c;

    f(x, y, c) = cast<uint8_t>(select(c == 0, 10*x + y,
                                      select(c == 1, 127, 12)));

    Image<uint8_t> out(10, 10, 3);
    f.glsl(x, y, c, 3);
    f.realize(out);

    out.copy_to_host();
    for (int y=0; y<out.height(); y++) {
        for (int x=0; x<out.width(); x++) {
            if (!(out(x, y, 0) == 10*x+y && out(x, y, 1) == 127 && out(x, y, 2) == 12)) {
                fprintf(stderr, "Incorrect pixel (%d, %d, %d) at x=%d y=%d.\n",
                        out(x, y, 0), out(x, y, 1), out(x, y, 2),
                        x, y);
            }
        }
    }
    printf("set_pixels finished!\n");
}

void copy_pixels() {
    Image<uint8_t> input(255, 10, 3);
    for (int y=0; y<input.height(); y++) {
        for (int x=0; x<input.width(); x++) {
            for (int c=0; c<3; c++) {
              input(x, y, c) = 10*x + y + c;
            }
        }
    }

    Func f, g;
    Var x, y, c;
    // f(x, y, c) = cast<float>(input(x, y, c)) / 255.f;
    // g(x, y, c) = cast<uint8_t>(f(x, y, c) * 255.f);

    g(x, y, c) = input(x, y, c);

    Image<uint8_t> out(255, 10, 3);
    g.glsl(x, y, c, 3);
    g.realize(out);
    out.copy_to_host();

    for (int y=0; y<out.height(); y++) {
        for (int x=0; x<out.width(); x++) {
            if (!(out(x, y, 0) == input(x, y, 0) &&
                  out(x, y, 1) == input(x, y, 1) &&
                  out(x, y, 2) == input(x, y, 2))) {
                fprintf(stderr, "Incorrect pixel (%d,%d,%d) != (%d,%d,%d) at x=%d y=%d.\n",
                        out(x, y, 0), out(x, y, 1), out(x, y, 2),
                        input(x, y, 0), input(x, y, 1), input(x, y, 2),
                        x, y);
            }
        }
    }
    printf("update_pixels finished!\n");
}

int main() {
    set_pixels();
    copy_pixels();
    return 0;
}
