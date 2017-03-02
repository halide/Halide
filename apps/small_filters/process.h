#ifndef PROCESS_H
#define PROCESS_H

#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

#define DECL_WEAK_PIPELINE3S(x) int x##_cpu(buffer_t *, buffer_t *, buffer_t *) __attribute__((weak)); \
    int x##_hvx128(buffer_t *, buffer_t *, buffer_t *) __attribute__((weak)); \
    int x##_hvx64(buffer_t *, buffer_t *, buffer_t *) __attribute__((weak));

#define DECL_WEAK_PIPELINE2S(x) int x##_cpu(buffer_t *, buffer_t *) __attribute__((weak)); \
    int x##_hvx128(buffer_t *, buffer_t *) __attribute__((weak)); \
    int x##_hvx64(buffer_t *, buffer_t *) __attribute__((weak));


#ifdef CONV3X3A16
#include "conv3x3a16_hvx128.h"
#include "conv3x3a16_hvx64.h"
#include "conv3x3a16_cpu.h"
#else
DECL_WEAK_PIPELINE3S(conv3x3a16)
#endif

#ifdef DILATE3X3
#include "dilate3x3_hvx128.h"
#include "dilate3x3_hvx64.h"
#include "dilate3x3_cpu.h"
#else
DECL_WEAK_PIPELINE2S(dilate3x3)
#endif

#ifdef MEDIAN3X3
#include "median3x3_hvx128.h"
#include "median3x3_hvx64.h"
#include "median3x3_cpu.h"
#else
DECL_WEAK_PIPELINE2S(median3x3)
#endif

#ifdef GAUSSIAN5X5
#include "gaussian5x5_hvx128.h"
#include "gaussian5x5_hvx64.h"
#include "gaussian5x5_cpu.h"
#else
DECL_WEAK_PIPELINE2S(gaussian5x5)
#endif

#ifdef SOBEL
#include "sobel_hvx128.h"
#include "sobel_hvx64.h"
#include "sobel_cpu.h"
#else
DECL_WEAK_PIPELINE2S(sobel)
#endif


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
    virtual const char * name() = 0;
    virtual int run(bmark_run_mode_t mode) = 0;
    virtual bool verify(int W, int H) = 0;
    virtual bool defined() = 0;
};

template <typename T1>
struct PipelineDescriptor : public PipelineDescriptorBase  {
    T1 pipeline_64, pipeline_128, pipeline_cpu;
 
    PipelineDescriptor(T1 pipeline_64, T1 pipeline_128, T1 pipeline_cpu) :
    pipeline_64(pipeline_64), pipeline_128(pipeline_128), pipeline_cpu(pipeline_cpu) {}
 
    void init() {
        return;
    }
    const char *name() {
        return "";
    }
    int run(bmark_run_mode_t mode) {
        return -1;
    }
    bool verify(int W, int H) {
        return false;
    }
    bool defined() {
        return pipeline_64 && pipeline_128 && pipeline_cpu;
    }
};

class Conv3x3a16Descriptor : public PipelineDescriptor<pipeline3> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
    Halide::Runtime::Buffer<int8_t> i8_mask;

public:
    Conv3x3a16Descriptor(pipeline3 pipeline_64, pipeline3 pipeline_128, pipeline3 pipeline_cpu,
                         int W, int H) :
        PipelineDescriptor<pipeline3>(pipeline_64, pipeline_128, pipeline_cpu),
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

    const char *name() { return "conv3x3a16"; }

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
        } else if (mode == bmark_run_mode_t::cpu) {
            return pipeline_cpu(u8_in, i8_mask, u8_out);
        }
        abort();
    }
};

class Dilate3x3Descriptor : public PipelineDescriptor<pipeline2> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
 private:
    uint8_t max3(uint8_t a, uint8_t b, uint8_t c) {
        return std::max(std::max(a, b), c);
    }
 public:
 Dilate3x3Descriptor(pipeline2 pipeline_64, pipeline2 pipeline_128, pipeline2 pipeline_cpu,
                     int W, int H) :
    PipelineDescriptor<pipeline2>(pipeline_64, pipeline_128, pipeline_cpu), u8_in(nullptr, W, H, 2),
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

    const char *name() { return "dilate3x3"; }

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
        } else if (mode == bmark_run_mode_t::cpu) {
            return pipeline_cpu(u8_in, u8_out);
        }
        abort();
    }

};

class Median3x3Descriptor : public PipelineDescriptor<pipeline2> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;

 public:
 Median3x3Descriptor(pipeline2 pipeline_64, pipeline2 pipeline_128, pipeline2 pipeline_cpu,
                     int W, int H) :
    PipelineDescriptor<pipeline2>(pipeline_64, pipeline_128, pipeline_cpu), u8_in(nullptr, W, H, 2),
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

    const char *name() { return "median3x3"; };

    bool verify(const int W, const int H) {
        u8_out.for_each_element([&](int x, int y) {
            uint8_t inp9[9] = { u8_in(clamp(x-1, 0, W-1), clamp(y-1, 0, H-1)),
                                u8_in(clamp(x, 0, W-1), clamp(y-1, 0, H-1)),
                                u8_in(clamp(x+1, 0, W-1), clamp(y-1, 0, H-1)),

                                u8_in(clamp(x-1, 0, W-1), clamp(y, 0, H-1)),
                                u8_in(clamp(x, 0, W-1), clamp(y, 0, H-1)),
                                u8_in(clamp(x+1, 0, W-1), clamp(y, 0, H-1)),

                                u8_in(clamp(x-1, 0, W-1), clamp(y+1, 0, H-1)),
                                u8_in(clamp(x, 0, W-1), clamp(y+1, 0, H-1)),
                                u8_in(clamp(x+1, 0, W-1), clamp(y+1, 0, H-1))
                           };

            std::nth_element(&inp9[0], &inp9[4], &inp9[9]);
            uint8_t median_val = inp9[4];
            uint8_t out_xy = u8_out(x, y);
            if (median_val != out_xy) {
                printf("Median3x3: Mismatch at %d %d : %d != %d\n", x, y, out_xy, median_val);
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
        } else if (mode == bmark_run_mode_t::cpu) {
            return pipeline_cpu(u8_in, u8_out);
        }
        abort();
    }
};

class Gaussian5x5Descriptor : public PipelineDescriptor<pipeline2> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;

 public:
 Gaussian5x5Descriptor(pipeline2 pipeline_64, pipeline2 pipeline_128, pipeline2 pipeline_cpu,
                     int W, int H) :
    PipelineDescriptor<pipeline2>(pipeline_64, pipeline_128, pipeline_cpu), u8_in(nullptr, W, H, 2),
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

    const char *name() { return "gaussian5x5"; };

    bool verify(const int W, const int H) {
        const int16_t coeffs[5] = { 1, 4, 6, 4, 1 };
        u8_out.for_each_element([&](int x, int y) {
            int16_t blur = 0;
            for (int rx = -2; rx < 3; ++rx) {
                int16_t blur_y = 0;
                for (int ry = -2; ry < 3; ++ry) {
                    int16_t val = static_cast<int16_t>(u8_in(clamp(x+rx, 0, W-1), clamp(y+ry, 0, H-1)));
                    blur_y += val * coeffs[ry + 2];
                }
                blur += blur_y * coeffs[rx + 2];
            }
            uint8_t blur_val = blur >> 8;
            uint8_t out_xy = u8_out(x, y);
            if (blur_val != out_xy) {
                printf("Gaussian5x5: Mismatch at %d %d : %d != %d\n", x, y, out_xy, blur_val);
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
        } else if (mode == bmark_run_mode_t::cpu) {
            return pipeline_cpu(u8_in, u8_out);
        }
        abort();
    }
};

class SobelDescriptor : public PipelineDescriptor<pipeline2> {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;

 public:
 SobelDescriptor(pipeline2 pipeline_64, pipeline2 pipeline_128, pipeline2 pipeline_cpu,
                     int W, int H) :
    PipelineDescriptor<pipeline2>(pipeline_64, pipeline_128, pipeline_cpu), u8_in(nullptr, W, H, 2),
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

    const char *name() { return "sobel"; };
    uint16_t sobel3(uint16_t a, uint16_t b, uint16_t c) {
        return (a + 2*b + c);
    }
    bool verify(const int W, const int H) {
        u8_out.for_each_element([&](int x, int y) {
            uint16_t sobel_x_avg0 = sobel3(static_cast<uint16_t>(u8_in(clamp(x-1, 0, W-1), clamp(y-1, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x, 0, W-1), clamp(y-1, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x+1, 0, W-1), clamp(y-1, 0, H-1))));
            uint16_t sobel_x_avg1 = sobel3(static_cast<uint16_t>(u8_in(clamp(x-1, 0, W-1), clamp(y+1, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x, 0, W-1), clamp(y+1, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x+1, 0, W-1), clamp(y+1, 0, H-1))));
            uint16_t sobel_x = abs(sobel_x_avg0 - sobel_x_avg1);

            uint16_t sobel_y_avg0 = sobel3(static_cast<uint16_t>(u8_in(clamp(x-1, 0, W-1), clamp(y-1, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x-1, 0, W-1), clamp(y, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x-1, 0, W-1), clamp(y+1, 0, H-1))));
            uint16_t sobel_y_avg1 = sobel3(static_cast<uint16_t>(u8_in(clamp(x+1, 0, W-1), clamp(y-1, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x+1, 0, W-1), clamp(y, 0, H-1))),
                                           static_cast<uint16_t>(u8_in(clamp(x+1, 0, W-1), clamp(y+1, 0, H-1))));
            uint16_t sobel_y = abs(sobel_y_avg0 - sobel_y_avg1);

            uint8_t sobel_val = static_cast<uint8_t>(clamp(sobel_x + sobel_y, 0, 255));

            uint8_t out_xy = u8_out(x, y);
            if (sobel_val != out_xy) {
                printf("Sobel: Mismatch at %d %d : %d != %d\n", x, y, out_xy, sobel_val);
                abort();
            }
        });
        {
        }
        return true;
    }

    int run(bmark_run_mode_t mode) {
        if (mode == bmark_run_mode_t::hvx64) {
            return pipeline_64(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return pipeline_128(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return pipeline_cpu(u8_in, u8_out);
        }
        abort();
    }
};
#endif
