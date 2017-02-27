#ifndef PROCESS_H
#define PROCESS_H

#include "conv3x3a16_hvx128.h"
#include "conv3x3a16_hvx64.h"
#include "conv3x3a16_cpu.h"
#include "dilate3x3_hvx128.h"
#include "dilate3x3_hvx64.h"
#include "dilate3x3_cpu.h"
#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

enum bmark_run_mode_t {
    hvx64 = 1,
    hvx128 = 2,
    cpu = 3
};

template <typename T>
T clamp(T val, T min, T max) {
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

typedef int (*pipeline2)(buffer_t *, buffer_t *);
typedef int (*pipeline3)(buffer_t *, buffer_t *, buffer_t *);

struct PipelineDescriptorBase {
    virtual void init() = 0;
    virtual void identify_pipeline() = 0;
    virtual int run(bmark_run_mode_t mode) = 0;
    virtual bool verify(int W, int H) = 0;
};

template <typename T1, typename T2>
struct PipelineDescriptor : public PipelineDescriptorBase  {
    T1 pipeline_64, pipeline_128, pipeline_cpu;
 
    PipelineDescriptor(T1 pipeline_64, T1 pipeline_128, T1 pipeline_cpu) :
    pipeline_64(pipeline_64), pipeline_128(pipeline_128), pipeline_cpu(pipeline_cpu) {}
 
    void init() {
        static_cast<T2*>(this)->init();
    }
    int run(bmark_run_mode_t mode) {
        return static_cast<T2*>(this)->run(mode);
    }
    bool verify(int W, int H) {
        return static_cast<T2*>(this)->verify(W, H);
    }
};
typedef Halide::Runtime::Buffer<uint8_t> U8Buffer;
typedef Halide::Runtime::Buffer<int8_t> I8Buffer;

class Conv3x3a16Descriptor : public PipelineDescriptor<pipeline3, Conv3x3a16Descriptor> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
    Halide::Runtime::Buffer<int8_t> i8_mask;

public:
    Conv3x3a16Descriptor(pipeline3 pipeline_64, pipeline3 pipeline_128, pipeline3 pipeline_cpu,
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

    void identify_pipeline() { printf("Running conv3x3a16 ...\n"); }

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

class Dilate3x3Descriptor : public PipelineDescriptor<pipeline2, Dilate3x3Descriptor> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
 private:
    uint8_t max3(uint8_t a, uint8_t b, uint8_t c) {
        return std::max(std::max(a, b), c);
    }
 public:
 Dilate3x3Descriptor(pipeline2 pipeline_64, pipeline2 pipeline_128, pipeline2 pipeline_cpu,
                     int W, int H) :
    PipelineDescriptor<pipeline2, Dilate3x3Descriptor>(pipeline_64, pipeline_128, pipeline_cpu), u8_in(nullptr, W, H, 2),
        u8_out(nullptr, W, H, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.for_each_value([&](uint8_t &x) {
            x = 0;
        });
    }

    void identify_pipeline() { printf("Running dilate3x3 ...\n"); }

    bool verify(const int W, const int H) {
        u8_out.for_each_element([&](int x, int y) {
            uint8_t max_y[3];
            max_y[0] = max3(u8_in(clamp(x-1, 0, (W-1)), clamp(y-1, 0, (H-1))),
                            u8_in(clamp(x-1, 0, (W-1)), clamp(y, 0, (H-1))),
                            u8_in(clamp(x-1, 0, (W-1)), clamp(y+1, 0, (H-1))));

            max_y[1] = max3(u8_in(clamp(x, 0, (W-1)), clamp(y-1, 0, (H-1))),
                            u8_in(clamp(x, 0, (W-1)), clamp(y, 0, (H-1))),
                            u8_in(clamp(x, 0, (W-1)), clamp(y+1, 0, (H-1))));

            max_y[2] = max3(u8_in(clamp(x+1, 0, (W-1)), clamp(y-1, 0, (H-1))),
                            u8_in(clamp(x+1, 0, (W-1)), clamp(y, 0, (H-1))),
                            u8_in(clamp(x+1, 0, (W-1)), clamp(y+1, 0, (H-1))));

            uint8_t max_val = max3(max_y[0], max_y[1], max_y[2]);
            uint8_t out_xy = u8_out(x, y);
            if (max_val != out_xy) {
                printf("Dilate3x3: Mismatch at %d %d : %d != %d\n", x, y, out_xy, max_val);
                abort();
            }
        });

        return true;
    }

    int run(bmark_run_mode_t mode) {
        if (mode == bmark_run_mode_t::hvx64) {
            return pipeline_64(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return pipeline_128(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::cpu); {
            return pipeline_cpu(u8_in, u8_out);
        }
        abort();
    }

};
#endif
