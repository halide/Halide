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
    Target target = get_jit_target_from_environment();
    Var x;

    {
        Func f;

        f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
        f.set_custom_print(halide_print);
        Image<int32_t> result = f.realize(10);

        for (int32_t i = 0; i < 10; i++) {
            if (result(i) != i * i) {
                return -1;
            }
        }

        assert(messages.size() == 10);
        for (size_t i = 0; i < messages.size(); i++) {
            long long square;
            float forty_two;
            unsigned long long one_forty_five;

            int scan_count = sscanf(messages[i].c_str(), "%lld the answer is %f unsigned %llu",
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
        Image<int32_t> result = f.realize(10);

        for (int32_t i = 0; i < 10; i++) {
            if (result(i) != i * i) {
                return -1;
            }
        }

        assert(messages.size() == 1);
        long long nine;
        float forty_two;
        long long p;

        int scan_count = sscanf(messages[0].c_str(), "%lld g %f %%s %lld",
                                &nine, &forty_two, &p);
        assert(scan_count == 3);
        assert(nine == 9);
        assert(forty_two == 42.0f);
        assert(p == 127);

    }

    messages.clear();

    if (target.has_feature(Target::JavaScript)) {
        // TODO: Add JavaScript support for 64-bit integers
        printf("Skipping int64_t based print test for JavaScript as it depends on 64-bit integer support.\n");
    } else {
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
        Image<uint64_t> result = f.realize(1);

        if (result(0) != 100) {
            return -1;
        }

        assert(messages.back().size() == 8191);
    }

    messages.clear();

    // Check that Halide's stringification of floats and doubles
    // matches %f and %e respectively.

    #ifndef _MSC_VER
    // msvc's library has different ideas about how %f and %e should come out.
    // And so does JavaScript, plus rounding, etc. Basically these tests are too tight
    // in their behavior.
    if (target.has_feature(Target::JavaScript)) {
        printf("Skipping floating-point print tests for JavaScript as they depend on exact formatting and rounding.\n");
    } else {
        Func f, g;

        const int N = 1000000;

        Expr e = reinterpret(Float(32), random_int());
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
        Image<float> imf = f.realize(N);

        assert(messages.size() == (size_t)N);

        char correct[1024];
        for (int i = 0; i < N; i++) {
            snprintf(correct, sizeof(correct), "%f\n", imf(i));
            // OS X prints -nan as nan
            #ifdef __APPLE__
            if (messages[i] == "-nan\n") messages[i] = "nan\n";
            #endif
            if (target.has_feature(Target::JavaScript)) {
                if (messages[i] == "Infinity\n") {
                    messages[i] = "inf\n";
                } else if (messages[i] == "-Infinity\n") {
                    messages[i] = "-inf\n";
                } else if (messages[i] == "NaN\n") {
                    messages[i] = "nan\n";
                } else if (messages[i] == "-NaN\n") {
                    messages[i] = "-nan\n";
                } else if (imf(i) >= 1e21 || imf(i) <= -1e21) {
                    // The following does not work as JavaScript uses a varying number of characters
                    // after the decimal point.
                    // snprintf(correct, sizeof(correct), "%.16e\n", imf(i));
                    // TODO: Figure out a way to test this.
                    continue;
                }
            }
            if (messages[i] != correct) {
                printf("float %d: %s vs %s for %10.20e\n", i, messages[i].c_str(), correct, imf(i));
                return -1;
            }
        }

        messages.clear();

        if (target.has_feature(Target::JavaScript)) {
            // TODO: Add JavaScript support for 64-bit integers
            printf("Skipping int64_t based print test for JavaScript as it depends on 64-bit integer support.\n");
        } else {
            g(x) = print(reinterpret(Float(64), (cast<uint64_t>(random_int()) << 32) | random_int()));
            g.set_custom_print(halide_print);
            Image<double> img = g.realize(N);

            assert(messages.size() == (size_t)N);

            for (int i = 0; i < N; i++) {
                snprintf(correct, sizeof(correct), "%e\n", img(i));
                #ifdef __APPLE__
                if (messages[i] == "-nan\n") messages[i] = "nan\n";
                #endif
                if (messages[i] != correct) {
                    printf("double %d: %s vs %s for %10.20e\n", i, messages[i].c_str(), correct, img(i));
                    return -1;
                }
            }
        }
    }
    #endif


    printf("Success!\n");
    return 0;
}
