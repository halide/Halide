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

int main(int argc, char **argv) {
    Var x;

    ImageParam a(Int(32), 1);
    Image<int> b(1), c(1);
    b(0) = 17;
    c(0) = 0;
    a.set(c);


    double t1, t2;
    t1 = currentTime();

    for (int i = 0; i < 100; i++) {
        Func f;
        f(x) = a(x) + b(x);
        f.realize(c);
        assert(c(0) == (i+1)*17);
    }    

    t2 = currentTime();
    int elapsed = (int)(10.0 * (t2-t1));

    printf("%d us per jit compilation\n", elapsed);

    printf("Success!\n");
    return 0;
}
