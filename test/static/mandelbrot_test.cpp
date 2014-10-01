#include <mandelbrot.h>
#include <static_image.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv) {
    Image<int> output(100, 30);
    const char *code = " .:-~*={}&%#@";
    const int iters = strlen(code) - 1;

    // Compute 100 different julia sets
    for (float t = 0; t < 100; t++) {
        float fx = cos(t/10.0f), fy = sin(t/10.0f);
        int result = mandelbrot(-2.0f, 2.0f, -1.4f, 1.4f, fx, fy, iters, output.width(), output.height(), output);
        if (result != 0) {
            printf("filter failed: %d\n", result);
            return -1;
        }
    }

    char buf[4096];
    char *buf_ptr = buf;
    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            *buf_ptr++ = code[output(x, y)];
        }
        *buf_ptr++ = '\n';
    }
    *buf_ptr++ = 0;
    printf("%s", buf);
    fflush(stdout);

    printf("Success!\n");
    return 0;
}
