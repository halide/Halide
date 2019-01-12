/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#include "Halide.h"
#include "../autoscheduler/SimpleAutoSchedule.h"

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
        std::string use_simple_autoscheduler =
            Halide::Internal::get_env_variable("HL_USE_SIMPLE_AUTOSCHEDULER");
        bool skip_schedule = use_simple_autoscheduler == "1" || auto_schedule;
        Func alignment = align(inputs, inputs.width(), inputs.height(), skip_schedule);
        Func merged = merge(inputs, inputs.width(), inputs.height(), inputs.dim(2).extent(), alignment, skip_schedule);
        WhiteBalance wb = { white_balance_r, white_balance_g0, white_balance_g1, white_balance_b };
        Func finished = finish(merged, inputs.width(), inputs.height(), black_point, white_point, wb, compression, gain, skip_schedule);

        output = finished;

        // Schedule
        // (n/a, handled inside included functions)
        if (use_simple_autoscheduler == "1") {
            Halide::SimpleAutoscheduleOptions options;
            options.gpu = get_target().has_gpu_feature();
            options.gpu_tile_channel = 1;
            Func output_func = output;
            Halide::simple_autoschedule(output_func,
                    {{"black_point", 2050},
                     {"white_point", 15464},
                     {"white_balance_r", 2.29102f},
                     {"white_balance_g0", 1.f},
                     {"white_balance_g1", 1.f},
                     {"white_balance_b", 1.26855f},
                     {"compression", 3.8f},
                     {"gain", 1.1f},
                     {"input.min.0", 0},
                     {"input.extent.0", 5218},
                     {"input.min.1", 0},
                     {"input.extent.1", 3482},
                     {"input.min.2", 0},
                     {"input.extent.2", 7}},
                    {{0, 5218},
                     {0, 3482},
                     {0, 7}},
                    options);
        }

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
