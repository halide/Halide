#include <stdio.h>

#include "../support/benchmark.h"
#include "conv3x3a16_hvx128.h"
#include "conv3x3a16_hvx64.h"
#include "conv3x3a16_cpu.h"
#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"
#include "process.h"

class Conv3x3a16Descriptor : public PipelineDescriptor<pipeline3, Conv3x3a16Descriptor> {
    typedef Halide::Runtime::Buffer<uint8_t> U8Buffer;
    typedef Halide::Runtime::Buffer<int8_t> I8Buffer;
    
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
    Halide::Runtime::Buffer<int8_t> i8_mask;

public:
    Conv3x3a16Descriptor(pipeline3 pipeline_64, pipeline3 pipeline_128, pipeline3 pipeline3_cpu,
                         int W, int H) :
        PipelineDescriptor<pipeline3, Conv3x3a16Descriptor>(pipeline_64, pipeline_128, pipeline_cpu),
        u8_in(nullptr, W, H, 2),
        u8_out(nullptr, W, H, 2),
        i8_mask(nullptr, 3, 3, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());
        i8_mask.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.for_each_value([&](uint8_t &x) {
            x = 0;
        });

        i8_mask(0, 0) = 1;
        i8_mask(1, 0) = -4;
        i8_mask(2, 0) = 7;

        i8_mask(0, 0) = 2;
        i8_mask(1, 0) = -5;
        i8_mask(2, 0) = 8;

        i8_mask(0, 0) = 3;
        i8_mask(1, 0) = -6;
        i8_mask(2, 0) = 7;
    }

    bool verify(const int W, const int H) {
        u8_out.for_each_element([&](int x, int y) {
            int16_t sum = 0;
            for (int ry = -1; ry <= 1; ry++) {
                for (int rx = -1; rx <= 1; rx++) {
                    int clamped_x = (x + rx < 0) ? 0 : x + rx;
                    clamped_x = (clamped_x >= W) ? (W-1) : clamped_x;

                    int clamped_y = (y + ry < 0) ? 0 : y + ry;
                    clamped_y = (clamped_y >= H) ? (H-1) : clamped_y;

                    sum += static_cast<int16_t>(u8_in(clamped_x, clamped_y)) * static_cast<int16_t>(i8_mask(rx+1, ry+1));
                }
            }
            sum = sum >> 4;
            if (sum > 255) {
                sum = 255;
            } else if (sum < 0) {
                sum = 0;
            }
            uint8_t out_xy = u8_out(x, y);
            if (sum != out_xy) {
                printf("Conv3x3a16: Mismatch at %d %d : %d != %d\n", x, y, out_xy, sum);
                abort();
            }
        });
        return true;
    }

    void identify_pipeline() { printf ("Running conv3x3a16...\n"); }

    int run(bmark_run_mode_t mode) {
        if (mode == bmark_run_mode_t::hvx64) {
            return pipeline_64(u8_in, i8_mask, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return pipeline_128(u8_in, i8_mask, u8_out);
        } else if (mode == bmark_run_mode_t::cpu); {
            return pipeline_cpu(u8_in, i8_mask, u8_out);
        }
        abort();
    }
};

void usage(char *prg_name) {
    const char usage_string[] = " Run a bunch of small filters\n\n"
                                "\t -m -> hvx_mode - options are hvx64, hvx128. Default is to run hvx64, hvx128 and cpu\n"
                                "\t -n -> number of iterations\n"
                                "\t -h -> print this help message\n";
    printf ("%s - %s", prg_name, usage_string);

}

const char *to_string(bmark_run_mode_t mode) {
    if (mode == bmark_run_mode_t::hvx64) {
        return "(64 byte mode)";
    } else if (mode == bmark_run_mode_t::hvx128) {
        return "(128 byte mode)";
    } else {
        return "(cpu)";
    }
}

int main(int argc, char **argv) {
    // Set some defaults first.
    const int W = 1024;
    const int H = 1024;
    std::vector<bmark_run_mode_t> modes = {bmark_run_mode_t::hvx64, bmark_run_mode_t::hvx128, bmark_run_mode_t::cpu};
    int iterations = 10;

    // Process command line args.
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'm':
                {
                    std::string mode_to_run = argv[i+1];
                    modes.clear();
                    if (mode_to_run == "hvx64") {
                        modes.push_back(bmark_run_mode_t::hvx64);
                    } else if (mode_to_run == "hvx128") {
                        modes.push_back(bmark_run_mode_t::hvx128);
                    } else if (mode_to_run == "cpu") {
                        modes.push_back(bmark_run_mode_t::cpu);
                    } else {
                        usage(argv[0]);
                        abort();
                    }
                    i++;
                }
                break;
            case 'h':
                usage(argv[0]);
                return 0;
                break;
            case 'n':
                iterations = atoi(argv[i+1]);
                i++;
                break;
            }
        }
    }

    Conv3x3a16Descriptor conv3x3a16_pipeline(conv3x3a16_hvx64, conv3x3a16_hvx128, conv3x3a16_cpu, W, H);
    std::vector<PipelineDescriptorBase *> pipelines = {&conv3x3a16_pipeline};

    for (bmark_run_mode_t m : modes) {
        for (PipelineDescriptorBase *p : pipelines) {
            p->init();
            p->identify_pipeline();

            halide_hexagon_set_performance_mode(NULL, halide_hvx_power_turbo);
            halide_hexagon_power_hvx_on(NULL);

            double time = benchmark(iterations, 10, [&]() {
                    int result = p->run(m);
                    if (result != 0) {
                        printf("pipeline failed! %d\n", result);
                    }
                });
            printf("Done, time: %g s %s\n", time, to_string(m));

            // We're done with HVX, power it off.
            halide_hexagon_power_hvx_off(NULL);

            if (!p->verify(W, H)) {
                abort();
            }
        }
    }

    printf("Success!\n");
    return 0;
}
