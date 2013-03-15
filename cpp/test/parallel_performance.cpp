#include <stdio.h>
#include <Halide.h>

#ifdef _WIN32
extern "C" bool QueryPerformanceCounter(uint64_t *);
extern "C" bool QueryPerformanceFrequency(uint64_t *);
double currentTime() {
    uint64_t t, freq;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&freq);
    return (t * 1000.0) / freq;
}
#else
#include <sys/time.h>
double currentTime() {
    timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000.0 + t.tv_usec / 1000.0f;
}
#endif

using namespace Halide;

#define W 10240
#define H 16

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    Expr math = cast<float>(x+y);
    for (int i = 0; i < 10; i++) math = sqrt(cos(sin(math)));
    f(x, y) = math;
    g(x, y) = math;

    f.parallel(y);

    Image<float> imf = f.realize(W, H);

    double t1, t2;

    t1 = currentTime();
    f.realize(imf);
    t2 = currentTime();

    double parallelTime = t2 - t1;

    printf("Realizing g\n");
    Image<float> img = g.realize(W, H);
    printf("Done realizing g\n");

    t1 = currentTime();
    g.realize(img);
    t2 = currentTime();
    double serialTime = t2 - t1;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (imf(x, y) != img(x, y)) {
                printf("imf(%d, %d) = %f\n", x, y, imf(x, y));
                printf("img(%d, %d) = %f\n", x, y, img(x, y));
                return -1;
            }
        }
    }

    printf("Times: %f %f\n", serialTime, parallelTime);
    double speedup = serialTime / parallelTime;
    printf("Speedup: %f\n", speedup);

    if (speedup < 1.5) {
        fprintf(stderr, "WARNING: Parallel should be faster\n");
        return 0;
    }
    
    printf("Success!\n");
    return 0;
}
