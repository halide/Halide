#include <Halide.h>

#include <stdio.h>
#include <stdlib.h>

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

Image<uint16_t> input;
Image<uint16_t> output;
Image<uint16_t> output_ref;

#define MIN 1
#define MAX 1020

double test(Func f) {
    f.compile_to_assembly(f.name() + ".s", Internal::vec<Argument>(input), f.name());
    f.compile_jit();
    f.realize(output);
    double t1 = currentTime();
    for (int i = 0; i < 100; i++) {
        f.realize(output);
    }
    return currentTime() - t1;
}

void randomize_input() {
    for (int x = 0; x < input.width(); x++) {
        for (int y = 0; y < input.height(); y++) {
            input(x, y) = rand() & 0xfff;
        }
    }
}

void clear_outputs() {
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            output(x, y) = 0;
            output_ref(x, y) = 0;
        }
    }
}

bool compare_outputs(Func f, Func ref) {
    randomize_input();
    clear_outputs();
    f.compile_to_assembly(f.name() + ".s", Internal::vec<Argument>(input), f.name());
    f.compile_jit();
    f.realize(output);
    ref.compile_to_assembly(ref.name() + ".s", Internal::vec<Argument>(input), ref.name());
    ref.compile_jit();
    ref.realize(output_ref);
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            if (output(x, y) != output_ref(x, y)) {
                printf("Compare failed. First mismatch at x=%d, y=%d", x, y);
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    // Try doing vector loads with a boundary condition in various
    // ways and compare the performance.
    
    input = Image<uint16_t>(1024+8, 32);
    output = Image<uint16_t>(1024, 32);
    output_ref = Image<uint16_t>(1024, 32);
        
    Var x, y;    

    double t_ref, t_clamped, t_scalar, t_pad;

    {
        // Do an unclamped load to get a reference number
        Func f;
        f(x, y) = input(x, y) * 3 + input(x+1, y);

        f.vectorize(x, 8);

        t_ref = test(f);        
    }

    {
        // Variant 1 - do the clamped vector load
        Func g;
        g(x, y) = input(clamp(x, MIN, MAX), y);
        
        Func f;
        f(x, y) = g(x, y) * 3 + g(x+1, y);

        f.vectorize(x, 8);

        t_clamped = test(f);

    }

    {
        // Variant 2 - do the load as a scalar op just before the vectorized stuff
        Func g;
        g(x, y) = input(clamp(x, MIN, MAX), y);
        
        Func f;
        f(x, y) = g(x, y) * 3 + g(x+1, y);

        f.vectorize(x, 8);
        g.compute_at(f, x);

        t_scalar = test(f);
    }

    {
        // Variant 3 - pad each scanline using scalar code
        Func g;
        g(x, y) = input(clamp(x, MIN, MAX), y);
        
        Func f;
        f(x, y) = g(x, y) * 3 + g(x+1, y);

        f.vectorize(x, 8);
        g.compute_at(f, y);

        t_pad = test(f);
    }

    {
        // Variant 4 - make sure we don't do the wrong thing with more complex load expressions
        Func g;
        g(x, y) = input(clamp(clamp(x, MIN, MAX) + clamp(x * y, MIN, MAX) + clamp(-x, MIN, MAX), MIN, MAX), y);
        //.g(x, y) = input(clamp(2*x * x, MIN, MAX), y);

        Func f;
        f(x, y) = g(x, y) * 3 + g(x+1, y);

        f.vectorize(x, 8);

        test(f);
    }

    {
        // Check correctness
        for (int offset = 0; offset < 8; offset++) {
            // Clamped vector load
            Func g;
            g(x, y) = input(clamp(x, MIN+offset, MAX-offset), y);
            Func f;
            
            f(x, y) = g(x, y) * 3 + g(x+1, y);
            f.vectorize(x, 8);
            // Scalar load
            Func g_ref;
            g_ref(x, y) = input(clamp(x, MIN+offset, MAX-offset), y);
            Func f_ref;
            f_ref(x, y) = g_ref(x, y) * 3 + g_ref(x+1, y);
            
            f_ref.vectorize(x, 8);
            g_ref.compute_at(f_ref, x);
            
            if (!compare_outputs(f, f_ref)) {
                printf(" (at offset %d)\n", offset);
                break;
            };
        }
    }
    
    if (t_clamped > 2.0f * t_ref || t_clamped > t_scalar || t_clamped > t_pad) {
        printf("Clamped load timings suspicious:\n"
               "Unclamped: %f\n"
               "Clamped: %f\n"
               "Scalarize the load: %f\n"
               "Pad the input: %f\n", 
               t_ref, t_clamped, t_scalar, t_pad);
    }

    printf("Success!\n");

    return 0;
}
