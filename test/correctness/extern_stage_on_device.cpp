#include "Halide.h"
#include "HalideRuntime.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// An extern stage implemented by a Halide pipeline running
// either on host or device. The outer Halide filter must
// override the "device_api" parameter of Func::define_extern
// when using the extern_stage on device.
extern "C" HALIDE_EXPORT_SYMBOL int extern_stage_on_device(  //
    bool extern_on_device,
    bool outer_filter_on_device,
    halide_buffer_t *out  //
) {
    if (!out->is_bounds_query()) {
        if (extern_on_device && outer_filter_on_device) {
            // If both the extern and the outer filter are running on
            // device, the host allocation shall be null and the device
            // allocation must exist before entering the extern stage.
            assert(out->host == nullptr);
            assert(out->device != 0);
        } else {
            // For other cases, the host allocation must exist.
            assert(out->host);
        }
        assert(out->type == halide_type_of<int32_t>());
        assert(out->dimensions == 2);
        Var x, y;

        Func f("f");
        f(x, y) = x + y;

        if (extern_on_device) {
            Var xi, yi;
            f.gpu_tile(x, y, xi, yi, 16, 16);
        }
        f.realize(out);
    }
    return 0;
}

class ExternStageOnDeviceTest : public ::testing::TestWithParam<std::tuple<bool, bool>> {
protected:
    const Target target{get_jit_target_from_environment()};
    const DeviceAPI device_api{get_default_device_api_for_target(target)};

    void SetUp() override {
        if (!target.has_gpu_feature()) {
            GTEST_SKIP() << "No GPU target enabled.";
        }
    }
};
}  // namespace

TEST_P(ExternStageOnDeviceTest, ExternStageOnDevice) {
    bool extern_on_device = std::get<0>(GetParam());
    bool sink_on_device = std::get<1>(GetParam());

    Var x, y;
    Var xi, yi;

    Func source("source");
    source.define_extern("extern_stage_on_device",
                         {extern_on_device, sink_on_device},
                         Int(32),
                         {x, y},
                         NameMangling::Default,
                         extern_on_device ? device_api : DeviceAPI::Host);

    Func sink("sink");
    sink(x, y) = source(x, y) - (x + y);

    source.compute_root();
    sink.compute_root();
    if (sink_on_device) {
        sink.gpu_tile(x, y, xi, yi, 16, 16);
    }

    Buffer<int32_t> output = sink.realize({100, 100});
    for (int xx = 0; xx < output.width(); xx++) {
        for (int yy = 0; yy < output.height(); yy++) {
            ASSERT_EQ(output(xx, yy), 0) << "xx = " << xx << " yy = " << yy;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    ExternStageOnDeviceTest,
    ExternStageOnDeviceTest,
    testing::Combine(
        testing::Values(false, true),
        testing::Values(false, true)));
