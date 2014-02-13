#include <mandelbrot.h>
#include <static_image.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

int main(int argc, char **argv) {
    Image<int> output(100, 30);
    const char *code = " .:-~*={}&%#@";
    const int iters = strlen(code) - 1;

    struct timeval t1, t2;

    gettimeofday(&t1, NULL);
    // Compute 100 different julia sets
    for (float t = 0; t < 100; t++) {
        float fx = cos(t/10.0f), fy = sin(t/10.0f);
        mandelbrot(-2.0f, 2.0f, -1.4f, 1.4f, fx, fy, iters, output.width(), output.height(), output);
    }
    gettimeofday(&t2, NULL);

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

    int64_t time = (t2.tv_usec - t1.tv_usec) / 1000;
    time += (t2.tv_sec - t1.tv_sec) * 1000;
    printf("Success (%lld ms)!\n", time);
    return 0;
}
