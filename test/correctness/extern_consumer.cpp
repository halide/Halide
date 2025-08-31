#include "Halide.h"
#include "halide_test_dirs.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;

namespace {
extern "C" HALIDE_EXPORT_SYMBOL int dump_to_stdout(halide_buffer_t *input,
                                                   int desired_min, int desired_extent,
                                                   halide_buffer_t *) {
    // Note the final output buffer argument is unused.
    if (input->is_bounds_query()) {
        // Request some range of the input buffer
        input->dim[0].min = desired_min;
        input->dim[0].extent = desired_extent;
    } else {
        // Depending on the schedule, other consumers, etc, Halide may
        // have evaluated more than we asked for, so don't assume that
        // the min and extents match what we requested.
        const int *base = reinterpret_cast<int *>(input->host) - input->dim[0].min;
        for (int i = desired_min; i < desired_min + desired_extent; i++) {
            printf("%d\n", base[i]);
        }
    }

    return 0;
}

void check_result(const std::string &result) {
    const char *correct =
        "0\n"
        "1\n"
        "4\n"
        "9\n"
        "16\n"
        "25\n"
        "36\n"
        "49\n"
        "64\n"
        "81\n";
    EXPECT_THAT(result, testing::StartsWith(correct));
}

class ExternConsumerTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment().with_feature(Target::Debug)};

    Param<int> min{"min"}, extent{"extent"};
    Var x{"x"};

    void SetUp() override {
        if (target.arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.";
        }
    }
};
}  // namespace

TEST_F(ExternConsumerTest, ExternConsumer) {
    // Define a pipeline that dumps some squares to a file using an
    // external consumer stage.
    Func source;
    source(x) = x * x;

    Func sink;
    std::vector<ExternFuncArgument> args;
    args.push_back(source);
    args.push_back(min);
    args.push_back(extent);
    sink.define_extern("dump_to_stdout", args, Int(32), 0);

    // Extern stages still have an outermost var.
    source.compute_at(sink, Var::outermost());

    testing::internal::CaptureStdout();
    min.set(0);
    extent.set(10);
    sink.realize();
    check_result(testing::internal::GetCapturedStdout());
}

TEST_F(ExternConsumerTest, ImageParam) {
    // Test ImageParam ExternFuncArgument via passed in image.
    Func source;
    source(x) = x * x;

    Buffer<int32_t> buf = source.realize({10});
    EXPECT_EQ(buf.width(), 10);

    ImageParam passed_in(Int(32), 1);
    passed_in.set(buf);

    Func sink;
    std::vector<ExternFuncArgument> args;
    args.push_back(passed_in);
    args.push_back(min);
    args.push_back(extent);
    sink.define_extern("dump_to_stdout", args, Int(32), 0);

    testing::internal::CaptureStdout();
    min.set(0);
    extent.set(10);
    sink.realize();  // TODO: figure out why this runs 5 times.
    check_result(testing::internal::GetCapturedStdout());
}
