#include <stdio.h>
#include <string>
#include <vector>
#include <limits>
#include "Halide.h"

using namespace Halide;

std::vector<std::string> messages;

extern "C" void halide_print(void *user_context, const char *message) {
    //printf("%s", message);
    messages.push_back(message);
}

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().has_feature(Target::Profile)) {
        // The profiler adds lots of extra prints, so counting the
        // number of prints is not useful.
        printf("Skipping test because profiler is active\n");
        return 0;
    }

    Var x;

    {
        Func f;

        f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
        f.set_custom_print(halide_print);
        Buffer<int32_t> result = f.realize(10);

        for (int32_t i = 0; i < 10; i++) {
            if (result(i) != i * i) {
                return -1;
            }
        }

        assert(messages.size() == 10);
        for (size_t i = 0; i < messages.size(); i++) {
            long square;
            float forty_two;
            unsigned long one_forty_five;

            int scan_count = sscanf(messages[i].c_str(), "%ld the answer is %f unsigned %lu",
                                    &square, &forty_two, &one_forty_five);
            assert(scan_count == 3);
            assert(square == static_cast<long long>(i * i));
            assert(forty_two == 42.0f);
            assert(one_forty_five == 145);
        }
    }

    messages.clear();

    {
        Func f;
        Param<int> param;
        param.set(127);

        // Test a string containing a printf format specifier (It should print it as-is).
        f(x) = print_when(x == 3, x * x, "g", 42.0f, "%s", param);
        f.set_custom_print(halide_print);
        Buffer<int32_t> result = f.realize(10);

        for (int32_t i = 0; i < 10; i++) {
            if (result(i) != i * i) {
                return -1;
            }
        }

        assert(messages.size() == 1);
        long nine;
        float forty_two;
        long p;

        int scan_count = sscanf(messages[0].c_str(), "%ld g %f %%s %ld",
                                &nine, &forty_two, &p);
        assert(scan_count == 3);
        assert(nine == 9);
        assert(forty_two == 42.0f);
        assert(p == 127);

    }

    messages.clear();

    {
        Func f;

        // Test a single message longer than 8K.
        std::vector<Expr> args;
        for (int i = 0; i < 500; i++) {
            uint64_t n = i;
            n *= n;
            n *= n;
            n *= n;
            n *= n;
            n += 100;
            int32_t hi = n >> 32;
            int32_t lo = n & 0xffffffff;
            args.push_back((cast<uint64_t>(hi) << 32) | lo);
            Expr dn = cast<double>((float)(n));
            args.push_back(dn);
        }
        f(x) = print(args);
        f.set_custom_print(halide_print);
        Buffer<uint64_t> result = f.realize(1);

        if (result(0) != 100) {
            return -1;
        }

        assert(messages.back().size() == 8191);
    }

    messages.clear();

    // Check that Halide's stringification of floats and doubles
    // matches %f and %e respectively.

    #ifndef _WIN32
    // msvc's library has different ideas about how %f and %e should come out.
    {
        Func f, g;

        const int N = 1000000;

        Expr e = reinterpret(Float(32), random_uint());
        // Make sure we cover some special values.
        e = select(x == 0, 0.0f,
                   x == 1, -0.0f,
                   x == 2, std::numeric_limits<float>::infinity(),
                   x == 3, -std::numeric_limits<float>::infinity(),
                   x == 4, std::numeric_limits<float>::quiet_NaN(),
                   x == 5, -std::numeric_limits<float>::quiet_NaN(),
                   e);
        e = select(x == 5, std::numeric_limits<float>::denorm_min(),
                   x == 6, -std::numeric_limits<float>::denorm_min(),
                   x == 7, std::numeric_limits<float>::min(),
                   x == 8, -std::numeric_limits<float>::min(),
                   x == 9, std::numeric_limits<float>::max(),
                   x == 10, -std::numeric_limits<float>::max(),
                   x == 11, 1.0f - 1.0f / (1 << 22),
                   e);

        f(x) = print(e);

        f.set_custom_print(halide_print);
        Buffer<float> imf = f.realize(N);

        assert(messages.size() == (size_t)N);

        char correct[1024];
        for (int i = 0; i < N; i++) {
            snprintf(correct, sizeof(correct), "%f\n", imf(i));
            // Some versions of the std library can emit some NaN patterns
            // as "-nan", due to sloppy conversion (or not) of the sign bit.
            // Halide considers all NaN's equivalent, so paper over this
            // noise in the test by normalizing all -nan -> nan.
            if (messages[i] == "-nan\n") messages[i] = "nan\n";
            if (!strcmp(correct, "-nan\n")) strcpy(correct, "nan\n");
            if (messages[i] != correct) {
                printf("float %d: %s vs %s for %10.20e\n", i, messages[i].c_str(), correct, imf(i));
                return -1;
            }
        }

        messages.clear();

        g(x) = print(reinterpret(Float(64), (cast<uint64_t>(random_uint()) << 32) | random_uint()));
        g.set_custom_print(halide_print);
        Buffer<double> img = g.realize(N);

        assert(messages.size() == (size_t)N);

        for (int i = 0; i < N; i++) {
            snprintf(correct, sizeof(correct), "%e\n", img(i));
            // Some versions of the std library can emit some NaN patterns
            // as "-nan", due to sloppy conversion (or not) of the sign bit.
            // Halide considers all NaN's equivalent, so paper over this
            // noise in the test by normalizing all -nan -> nan.
            if (messages[i] == "-nan\n") messages[i] = "nan\n";
            if (!strcmp(correct, "-nan\n")) strcpy(correct, "nan\n");
            if (messages[i] != correct) {
                printf("double %d: %s vs %s for %10.20e\n", i, messages[i].c_str(), correct, img(i));
                return -1;
            }
        }


    }
    #endif


    printf("Success!\n");
    return 0;
}
