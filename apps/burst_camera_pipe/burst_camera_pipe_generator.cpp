/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#include "Halide.h"

#include "align.h"
#include "merge.h"
#include "finish.h"

namespace {

class BurstCameraPipe : public Halide::Generator<BurstCameraPipe> {
public:
    // 'inputs' is really a series of raw 2d frames; extent[2] specifies the count
    Input<Buffer<uint16_t>> inputs{"inputs", 3};
    Input<uint16_t> black_point{"black_point"};
    Input<uint16_t> white_point{"white_point"};
    Input<float> white_balance_r{"white_balance_r"};
    Input<float> white_balance_g0{"white_balance_g0"};
    Input<float> white_balance_g1{"white_balance_g1"};
    Input<float> white_balance_b{"white_balance_b"};
    Input<float> compression{"compression"};
    Input<float> gain{"gain"};

    // RGB output
    Output<Buffer<uint8_t>> output{"output", 3};

    void generate() {
        // Algorithm
        bool skip_schedule = auto_schedule;
        Func alignment = align(inputs, inputs.width(), inputs.height(), skip_schedule);
        Func merged = merge(inputs, inputs.width(), inputs.height(), inputs.dim(2).extent(), alignment, skip_schedule);
        WhiteBalance wb = { white_balance_r, white_balance_g0, white_balance_g1, white_balance_b };
        Func finished = finish(merged, inputs.width(), inputs.height(), black_point, white_point, wb, compression, gain, skip_schedule);

        output = finished;

        // Schedule
        // (n/a, handled inside included functions)

        // Estimates
        {
            constexpr int w = 5218;
            constexpr int h = 3482;
            constexpr int num_frames = 7;
            inputs.dim(0).set_bounds_estimate(0, w)
                    .dim(1).set_bounds_estimate(0, h)
                    .dim(2).set_bounds_estimate(0, num_frames);
            // taken from eos-1dx.cr2
            black_point.set_estimate(2050);
            white_point.set_estimate(15464);
            white_balance_r.set_estimate(2.29102);
            white_balance_g0.set_estimate(1);
            white_balance_g1.set_estimate(1);
            white_balance_b.set_estimate(1.26855);
            compression.set_estimate(3.8);
            gain.set_estimate(1.1);
            output.dim(0).set_bounds_estimate(0, w)
                    .dim(1).set_bounds_estimate(0, h)
                    .dim(2).set_bounds_estimate(0, 3);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(BurstCameraPipe, burst_camera_pipe)
