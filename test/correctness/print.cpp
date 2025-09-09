#include "Halide.h"
#include <gtest/gtest.h>

#include <limits>
#include <stdio.h>
#include <string>
#include <vector>

using namespace Halide;

namespace {
struct PrintTestContext : JITUserContext {
    std::function<void(const char *, int)> checker;
    int print_count = 0;
    explicit PrintTestContext(const std::function<void(const char *, int)> &checker)
        : checker(checker) {
        handlers.custom_print = my_print;
    }
    static void my_print(JITUserContext *ctx, const char *message) {
        auto *self = static_cast<PrintTestContext *>(ctx);
        int current_print_count = self->print_count++;
        self->checker(message, current_print_count);
    }
};

class PrintTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    Var x{"x"};
    void SetUp() override {
        if (target.has_feature(Target::Profile)) {
            // The profiler adds lots of extra prints, so counting the
            // number of prints is not useful.
            GTEST_SKIP() << "Test incompatible with profiler.";
        }

        if (target.has_feature(Target::Debug)) {
            // Same thing here: the runtime debug adds lots of extra prints,
            // so counting the number of prints is not useful.
            GTEST_SKIP() << "Test incompatible with debug runtime.";
        }
    }
};
}  // namespace

TEST_F(PrintTest, Basic) {
    Func f;
    f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));

    PrintTestContext ctx([](const char *msg, int i) {
        long square;
        float forty_two;
        unsigned long one_forty_five;

        int scan_count = sscanf(msg, "%ld the answer is %f unsigned %lu",
                                &square, &forty_two, &one_forty_five);
        ASSERT_EQ(scan_count, 3);
        EXPECT_EQ(square, static_cast<long long>(i * i));
        EXPECT_EQ(forty_two, 42.0f);
        EXPECT_EQ(one_forty_five, 145);
    });
    Buffer<int32_t> result = f.realize(&ctx, {10});

    EXPECT_EQ(ctx.print_count, 10);
    for (int32_t i = 0; i < 10; i++) {
        EXPECT_EQ(result(i), i * i);
    }
}

TEST_F(PrintTest, FormatSpecifierVerbatim) {
    Param<int> param;
    param.set(127);

    Func f;
    f(x) = print_when(x == 3, x * x, "g", 42.0f, "%s", param);

    PrintTestContext ctx([](const char *msg, int) {
        long nine;
        float forty_two;
        long p;
        int scan_count = sscanf(msg, "%ld g %f %%s %ld", &nine, &forty_two, &p);
        ASSERT_EQ(scan_count, 3);
        EXPECT_EQ(nine, 9);
        EXPECT_EQ(forty_two, 42.0f);
        EXPECT_EQ(p, 127);
    });
    Buffer<int32_t> result = f.realize(&ctx, {10});

    for (int32_t i = 0; i < 10; i++) {
        EXPECT_EQ(result(i), i * i);
    }

    EXPECT_EQ(ctx.print_count, 1);
}

TEST_F(PrintTest, MessageLongerThan8KB) {
    std::vector<Expr> args;
    for (int i = 0; i < 500; i++) {
        uint64_t n = i;
        n *= n;
        n *= n;
        n *= n;
        n *= n;
        n += 100;
        uint64_t hi = n >> 32;
        uint64_t lo = n & 0xffffffff;
        args.push_back((Expr(hi) << 32) | Expr(lo));
        Expr dn = cast<double>((float)(n));
        args.push_back(dn);
    }

    Func f;
    f(x) = print(args);

    PrintTestContext ctx([](const char *msg, int i) {
        ASSERT_EQ(i, 0);
        EXPECT_EQ(strlen(msg), 8191);
    });
    Buffer<uint64_t> result = f.realize(&ctx, {1});

    EXPECT_EQ(result(0), 100);
}

TEST_F(PrintTest, MatchesPrintfFloat) {
#ifdef _WIN32
    GTEST_SKIP() << "msvc's library has different ideas about how %f should come out.";
#else
    constexpr int N = 100000;

    // Make sure we cover some special values.
    Expr e = select(
        x == 0, 0.0f,
        x == 1, -0.0f,
        x == 2, std::numeric_limits<float>::infinity(),
        x == 3, -std::numeric_limits<float>::infinity(),
        x == 4, std::numeric_limits<float>::quiet_NaN(),
        x == 5, -std::numeric_limits<float>::quiet_NaN(),
        x == 6, std::numeric_limits<float>::denorm_min(),
        x == 7, -std::numeric_limits<float>::denorm_min(),
        x == 8, std::numeric_limits<float>::min(),
        x == 9, -std::numeric_limits<float>::min(),
        x == 10, std::numeric_limits<float>::max(),
        x == 11, -std::numeric_limits<float>::max(),
        x == 12, 1.0f - 1.0f / (1 << 22),
        reinterpret(Float(32), random_uint()));

    Func f;
    f(x) = print(e);

    std::vector<std::string> messages;
    PrintTestContext ctx([&](const char *msg, int) {
        messages.emplace_back(msg);
    });
    Buffer<float> im = f.realize(&ctx, {N});

    EXPECT_EQ(ctx.print_count, N);
    for (int i = 0; i < messages.size(); i++) {
        char correct[1024];
        snprintf(correct, sizeof(correct), "%f\n", im(i));
        // Some versions of the std library can emit some NaN patterns
        // as "-nan", due to sloppy conversion (or not) of the sign bit.
        // Halide considers all NaN's equivalent, so paper over this
        // noise in the test by normalizing all -nan -> nan.
        if (messages[i] == "-nan\n") {
            messages[i] = "nan\n";
        }
        if (strcmp(correct, "-nan\n") == 0) {
            strcpy(correct, "nan\n");
        }
        EXPECT_EQ(messages[i], correct) << "imf(i) = " << im(i);
    }
#endif
}

TEST_F(PrintTest, MatchesPrintfDouble) {
#ifdef _WIN32
    GTEST_SKIP() << "msvc's library has different ideas about how %e should come out.";
#else
    constexpr int N = 100000;

    // Make sure we cover some special values.
    Expr e = select(
        x == 0, Expr(0.0),
        x == 1, Expr(-0.0),
        x == 2, Expr(std::numeric_limits<double>::infinity()),
        x == 3, Expr(-std::numeric_limits<double>::infinity()),
        x == 4, Expr(std::numeric_limits<double>::quiet_NaN()),
        x == 5, Expr(-std::numeric_limits<double>::quiet_NaN()),
        x == 6, Expr(std::numeric_limits<double>::denorm_min()),
        x == 7, Expr(-std::numeric_limits<double>::denorm_min()),
        x == 8, Expr(std::numeric_limits<double>::min()),
        x == 9, Expr(-std::numeric_limits<double>::min()),
        x == 10, Expr(std::numeric_limits<double>::max()),
        x == 11, Expr(-std::numeric_limits<double>::max()),
        x == 12, Expr(1.0 - 1.0 / (1 << 22)),
        reinterpret(Float(64), cast<uint64_t>(random_uint()) << 32 | random_uint()));

    Func g;
    std::vector<std::string> messages;
    PrintTestContext ctx([&](const char *msg, int) {
        messages.emplace_back(msg);
    });

    g(x) = print(e);
    Buffer<double> img = g.realize(&ctx, {N});

    assert(messages.size() == (size_t)N);

    for (int i = 0; i < N; i++) {
        char correct[1024];
        snprintf(correct, sizeof(correct), "%e\n", img(i));
        // Some versions of the std library can emit some NaN patterns
        // as "-nan", due to sloppy conversion (or not) of the sign bit.
        // Halide considers all NaN's equivalent, so paper over this
        // noise in the test by normalizing all -nan -> nan.
        if (messages[i] == "-nan\n") {
            messages[i] = "nan\n";
        }
        if (!strcmp(correct, "-nan\n")) {
            strcpy(correct, "nan\n");
        }
        ASSERT_EQ(messages[i], correct) << "img(i) = " << img(i);
    }
#endif
}

TEST_F(PrintTest, VectorizedPrint) {
    Func f;
    f(x) = print(x * 3);
    f.vectorize(x, 32);
    if (target.has_feature(Target::HVX)) {
        f.hexagon();
        // The Hexagon simulator prints directly to stderr, so we
        // can't read the messages.
        ASSERT_NO_THROW(f.realize({128}));
    } else {
        PrintTestContext ctx([&](const char *msg, int i) {
            ASSERT_EQ(msg, std::to_string(i * 3) + "\n");
        });
        Buffer<int> result = f.realize(&ctx, {128});
        ASSERT_EQ(ctx.print_count, result.width());
    }
}

TEST_F(PrintTest, VectorizedPrintWhen) {
    Func f;
    f(x) = print_when(x % 2 == 0, x * 3);
    f.vectorize(x, 32);
    if (target.has_feature(Target::HVX)) {
        f.hexagon();
        ASSERT_NO_THROW(f.realize({128}));
    } else {
        PrintTestContext ctx([&](const char *msg, int i) {
            ASSERT_EQ(msg, std::to_string(i * 2 * 3) + "\n");
        });
        Buffer<int> result = f.realize(&ctx, {128});
        ASSERT_EQ(ctx.print_count, result.width() / 2);
    }
}
