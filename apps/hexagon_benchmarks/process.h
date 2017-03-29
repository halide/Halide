#ifndef PROCESS_H
#define PROCESS_H

#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

#ifdef CONV3X3A16
#include "conv3x3a16_hvx128.h"
#include "conv3x3a16_hvx64.h"
#include "conv3x3a16_cpu.h"
#endif

#ifdef DILATE3X3
#include "dilate3x3_hvx128.h"
#include "dilate3x3_hvx64.h"
#include "dilate3x3_cpu.h"
#endif

#ifdef MEDIAN3X3
#include "median3x3_hvx128.h"
#include "median3x3_hvx64.h"
#include "median3x3_cpu.h"
#endif

#ifdef GAUSSIAN5X5
#include "gaussian5x5_hvx128.h"
#include "gaussian5x5_hvx64.h"
#include "gaussian5x5_cpu.h"
#endif

#ifdef SOBEL
#include "sobel_hvx128.h"
#include "sobel_hvx64.h"
#include "sobel_cpu.h"
#endif

#ifdef CONV3X3A32
#include "conv3x3a32_hvx128.h"
#include "conv3x3a32_hvx64.h"
#include "conv3x3a32_cpu.h"
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

struct PipelineDescriptorBase {
    virtual void init() = 0;
    virtual const char * name() = 0;
    virtual int run(bmark_run_mode_t mode) = 0;
    virtual bool verify(int W, int H) = 0;
    virtual bool defined() = 0;
    virtual void finalize() = 0;
};

class Conv3x3a16Descriptor : public PipelineDescriptorBase {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
    Halide::Runtime::Buffer<int8_t> i8_mask;

public:
    Conv3x3a16Descriptor(int W, int H) : u8_in(nullptr, W, H, 2),
                                         u8_out(nullptr, W, H, 2),
                                         i8_mask(nullptr, 3, 3, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());
        i8_mask.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.fill(0);

        i8_mask(0, 0) = 1;
        i8_mask(1, 0) = -4;
        i8_mask(2, 0) = 7;

        i8_mask(0, 1) = 2;
        i8_mask(1, 1) = -5;
        i8_mask(2, 1) = 8;

        i8_mask(0, 2) = 3;
        i8_mask(1, 2) = -6;
        i8_mask(2, 2) = 9;
    }


    const char *name() { return "conv3x3a16"; }

    bool defined() {
#ifdef CONV3X3A16
        return true;
#else
        return false;
#endif
    }

    bool verify(const int W, const int H) {
        u8_out.copy_to_host();
        u8_out.for_each_element([&](int x, int y) {
            int16_t sum = 0;
            for (int ry = -1; ry <= 1; ry++) {
                for (int rx = -1; rx <= 1; rx++) {
                    sum += static_cast<int16_t>(u8_in(clamp(x+rx, 0, W-1), clamp(y+ry, 0, H-1)))
                                                * static_cast<int16_t>(i8_mask(rx+1, ry+1));
                }
            }
            sum = sum >> 4;
            sum = clamp<int16_t>(sum, 0, 255);
            uint8_t out_xy = u8_out(x, y);
            if (sum != out_xy) {
                printf("Conv3x3a16: Mismatch at %d %d : %d != %d\n", x, y, out_xy, sum);
                abort();
            }
        });
        return true;
    }

    int run(bmark_run_mode_t mode) {
#ifdef CONV3X3A16
        if (mode == bmark_run_mode_t::hvx64) {
            return conv3x3a16_hvx64(u8_in, i8_mask, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return conv3x3a16_hvx128(u8_in, i8_mask, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return conv3x3a16_cpu(u8_in, i8_mask, u8_out);
        }
#endif
        return 1;
    }
    void finalize() {
        u8_in.device_free();
        i8_mask.device_free();
        u8_out.device_free();
    }
};

class Dilate3x3Descriptor : public PipelineDescriptorBase {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
 private:
    static uint8_t max3(uint8_t a, uint8_t b, uint8_t c) {
        return std::max(std::max(a, b), c);
    }
 public:
     Dilate3x3Descriptor(int W, int H) : u8_in(nullptr, W, H, 2),
                                         u8_out(nullptr, W, H, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.fill(0);
    }

    const char *name() { return "dilate3x3"; }

    bool defined() {
#ifdef DILATE3X3
        return true;
#else
        return false;
#endif
    }

    bool verify(const int W, const int H) {
        u8_out.copy_to_host();
        u8_out.for_each_element([&](int x, int y) {
            auto u8_in_bounded = [&](int x_, int y_) { return u8_in(clamp(x_, 0, W-1), clamp(y_, 0, H-1)); };

            uint8_t max_y[3];
            max_y[0] = max3(u8_in_bounded(x-1, y-1), u8_in_bounded(x-1, y), u8_in_bounded(x-1, y+1));

            max_y[1] = max3(u8_in_bounded(x, y-1), u8_in_bounded(x, y), u8_in_bounded(x, y+1));

            max_y[2] = max3(u8_in_bounded(x+1, y-1), u8_in_bounded(x+1, y), u8_in_bounded(x+1, y+1));

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
#ifdef DILATE3X3
        if (mode == bmark_run_mode_t::hvx64) {
            return dilate3x3_hvx64(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return dilate3x3_hvx128(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return dilate3x3_cpu(u8_in, u8_out);
        }
#endif
        return 1;
    }
    void finalize() {
        u8_in.device_free();
        u8_out.device_free();
    }
};

class Median3x3Descriptor : public PipelineDescriptorBase {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;

 public:
      Median3x3Descriptor(int W, int H) : u8_in(nullptr, W, H, 2),
                                          u8_out(nullptr, W, H, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.fill(0);
    }

    const char *name() { return "median3x3"; };

    bool defined() {
#ifdef MEDIAN3X3
        return true;
#else
        return false;
#endif
    }

    bool verify(const int W, const int H) {
        u8_out.copy_to_host();
        u8_out.for_each_element([&](int x, int y) {
            auto u8_in_bounded = [&](int x_, int y_) { return u8_in(clamp(x_, 0, W-1), clamp(y_, 0, H-1)); };

            uint8_t inp9[9] = { u8_in_bounded(x-1, y-1), u8_in_bounded(x, y-1), u8_in_bounded(x+1, y-1),
                                u8_in_bounded(x-1, y), u8_in_bounded(x, y), u8_in_bounded(x+1, y),
                                u8_in_bounded(x-1, y+1), u8_in_bounded(x, y+1), u8_in_bounded(x+1, y+1) };

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
#ifdef MEDIAN3X3
        if (mode == bmark_run_mode_t::hvx64) {
            return median3x3_hvx64(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return median3x3_hvx128(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return median3x3_cpu(u8_in, u8_out);
        }
#endif
        return 1;
    }
    void finalize() {
        u8_in.device_free();
        u8_out.device_free();
    }
};

class Gaussian5x5Descriptor : public PipelineDescriptorBase {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;

 public:
     Gaussian5x5Descriptor(int W, int H) : u8_in(nullptr, W, H, 2),
                                           u8_out(nullptr, W, H, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.fill(0);
    }

    const char *name() { return "gaussian5x5"; };

    bool defined() {
#ifdef GAUSSIAN5X5
        return true;
#else
        return false;
#endif
    }

    bool verify(const int W, const int H) {
        const int16_t coeffs[5] = { 1, 4, 6, 4, 1 };
        u8_out.copy_to_host();
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
#ifdef GAUSSIAN5X5
        if (mode == bmark_run_mode_t::hvx64) {
            return gaussian5x5_hvx64(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return gaussian5x5_hvx128(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return gaussian5x5_cpu(u8_in, u8_out);
        }
#endif
        return 1;
    }
    void finalize() {
        u8_in.device_free();
        u8_out.device_free();
    }
};

class SobelDescriptor : public PipelineDescriptorBase {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;

 public:
     SobelDescriptor(int W, int H) : u8_in(nullptr, W, H, 2),
                                     u8_out(nullptr, W, H, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.fill(0);
    }

    const char *name() { return "sobel"; };

    uint16_t sobel3(uint16_t a, uint16_t b, uint16_t c) {
        return (a + 2*b + c);
    }

    bool defined() {
#ifdef SOBEL
        return true;
#else
        return false;
#endif
    }

    bool verify(const int W, const int H) {
        u8_out.copy_to_host();
        u8_out.for_each_element([&](int x, int y) {
            auto u16_in_bounded = [&](int x_, int y_) { return static_cast<uint16_t>(u8_in(clamp(x_, 0, W-1), clamp(y_, 0, H-1))); };

            uint16_t sobel_x_avg0 = sobel3(u16_in_bounded(x-1, y-1), u16_in_bounded(x, y-1), u16_in_bounded(x+1, y-1));
            uint16_t sobel_x_avg1 = sobel3(u16_in_bounded(x-1, y+1), u16_in_bounded(x, y+1), u16_in_bounded(x+1, y+1));
            uint16_t sobel_x = abs(sobel_x_avg0 - sobel_x_avg1);


            uint16_t sobel_y_avg0 = sobel3(u16_in_bounded(x-1, y-1), u16_in_bounded(x-1, y), u16_in_bounded(x-1, y+1));
            uint16_t sobel_y_avg1 = sobel3(u16_in_bounded(x+1, y-1), u16_in_bounded(x+1, y), u16_in_bounded(x+1, y+1));
            uint16_t sobel_y = abs(sobel_y_avg0 - sobel_y_avg1);

            uint8_t sobel_val = static_cast<uint8_t>(clamp(sobel_x + sobel_y, 0, 255));

            uint8_t out_xy = u8_out(x, y);
            if (sobel_val != out_xy) {
                printf("Sobel: Mismatch at %d %d : %d != %d\n", x, y, out_xy, sobel_val);
                abort();
            }
        });
        return true;
    }

    int run(bmark_run_mode_t mode) {
#ifdef SOBEL
        if (mode == bmark_run_mode_t::hvx64) {
            return sobel_hvx64(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return sobel_hvx128(u8_in, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return sobel_cpu(u8_in, u8_out);
        }
#endif
        return 1;
    }
    void finalize() {
        u8_in.device_free();
        u8_out.device_free();
    }
};

class Conv3x3a32Descriptor : public PipelineDescriptorBase {
    Halide::Runtime::Buffer<uint8_t> u8_in, u8_out;
    Halide::Runtime::Buffer<int8_t> i8_mask;

public:
    Conv3x3a32Descriptor(int W, int H) : u8_in(nullptr, W, H, 2),
                                         u8_out(nullptr, W, H, 2),
                                         i8_mask(nullptr, 3, 3, 2) {}

    void init() {
        u8_in.device_malloc(halide_hexagon_device_interface());
        u8_out.device_malloc(halide_hexagon_device_interface());
        i8_mask.device_malloc(halide_hexagon_device_interface());

        u8_in.for_each_value([&](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });
        u8_out.fill(0);

        i8_mask(0, 0) = 1;
        i8_mask(1, 0) = -4;
        i8_mask(2, 0) = 7;

        i8_mask(0, 1) = 2;
        i8_mask(1, 1) = -5;
        i8_mask(2, 1) = 8;

        i8_mask(0, 2) = 3;
        i8_mask(1, 2) = -6;
        i8_mask(2, 2) = 9;
    }

    const char *name() { return "conv3x3a32"; }

    bool defined() {
#ifdef CONV3X3A32
        return true;
#else
        return false;
#endif
    }

    bool verify(const int W, const int H) {
        u8_out.copy_to_host();
        u8_out.for_each_element([&](int x, int y) {
            int32_t sum = 0;
            for (int ry = -1; ry <= 1; ry++) {
                for (int rx = -1; rx <= 1; rx++) {
                    sum += static_cast<int16_t>(u8_in(clamp(x+rx, 0, W-1), clamp(y+ry, 0, H-1)))
                                                * static_cast<int16_t>(i8_mask(rx+1, ry+1));
                }
            }
            sum = sum >> 4;
            sum = clamp(sum, 0, 255);
            uint8_t out_xy = u8_out(x, y);
            if (sum != out_xy) {
                printf("Conv3x3a32: Mismatch at %d %d : %d != %d\n", x, y, out_xy, sum);
                abort();
            }
        });
        return true;
    }

    int run(bmark_run_mode_t mode) {
#ifdef CONV3X3A32
        if (mode == bmark_run_mode_t::hvx64) {
            return conv3x3a32_hvx64(u8_in, i8_mask, u8_out);
        } else if (mode == bmark_run_mode_t::hvx128) {
            return conv3x3a32_hvx128(u8_in, i8_mask, u8_out);
        } else if (mode == bmark_run_mode_t::cpu) {
            return conv3x3a32_cpu(u8_in, i8_mask, u8_out);
        }
#endif
        return 1;
    }
    void finalize() {
        u8_in.device_free();
        i8_mask.device_free();
        u8_out.device_free();
    }
};

#endif
