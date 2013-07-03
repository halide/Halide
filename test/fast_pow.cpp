#include <Halide.h>
#include <stdio.h>

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

HalideExtern_2(float, powf, float, float);

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    f(x, y) = powf((x+1)/512.0f, (y+1)/512.0f);    
    g(x, y) = pow((x+1)/512.0f, (y+1)/512.0f);
    h(x, y) = fast_pow((x+1)/512.0f, (y+1)/512.0f);
    f.vectorize(x, 8);
    g.vectorize(x, 8);
    h.vectorize(x, 8);
    
    f.compile_jit();
    g.compile_jit();
    h.compile_jit();

    Image<float> correct_result(1024, 768);
    Image<float> fast_result(1024, 768);
    Image<float> faster_result(1024, 768);

    double t1 = currentTime();
    f.realize(correct_result);
    double t2 = currentTime();
    g.realize(fast_result);
    double t3 = currentTime();
    h.realize(faster_result);
    double t4 = currentTime();

    RDom r(correct_result);
    Func fast_error, faster_error;
    Expr fast_delta = correct_result(r.x, r.y) - fast_result(r.x, r.y);
    Expr faster_delta = correct_result(r.x, r.y) - faster_result(r.x, r.y);
    fast_error() += fast_delta * fast_delta;
    faster_error() += faster_delta * faster_delta;

    Image<float> fast_err = fast_error.realize();
    Image<float> faster_err = faster_error.realize();

    int N = correct_result.width() * correct_result.height();
    fast_err(0) = sqrtf(fast_err(0)/N);
    faster_err(0) = sqrtf(faster_err(0)/N);

    printf("powf: %f ns per pixel\n"
           "Halide's pow: %f ns per pixel (rms error = %f)\n"
           "Halide's fast_pow: %f ns per pixel (rms error = %f)\n", 
           1000000*(t2-t1) / N, 
           1000000*(t3-t2) / N, fast_err(0),
           1000000*(t4-t3) / N, faster_err(0));

    if (fast_err(0) > 0.0000001) {
        printf("Error for pow too large\n");
        return -1;
    }

    if (faster_err(0) > 0.0001) {
        printf("Error for fast_pow too large\n");
        return -1;
    }

    if (t2-t1 < t3-t2) {
        printf("powf is faster than Halide's pow\n");
        return -1;
    }

    if (t3-t2 < t4-t3) {
        printf("pow is faster than fast_pow\n");
        return -1;
    }

    printf("Success!\n");
    
    return 0;
}
