#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "stereobm.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

#if STEREOBM_BUILD_OPENCV
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

using namespace Halide::Tools;

#if STEREOBM_BUILD_OPENCV
#ifndef WINSIZE
static constexpr int WINSIZE = 9;
#endif
#ifndef DEPTH
static constexpr int DEPTH = 16;
#endif
#ifndef PREFILTER_CAP
static constexpr int PREFILTER_CAP = 31;
#endif
#ifndef TEXTURE_THRESHOLD
static constexpr int TEXTURE_THRESHOLD = 10;
#endif
#ifndef UNIQUENESS_RATIO
static constexpr int UNIQUENESS_RATIO = 0;
#endif
#ifndef MIN_DISP
static constexpr int MIN_DISP = 0;
#endif
#endif

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: %s left right out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<uint8_t, 2> left, right;

#if STEREOBM_BUILD_OPENCV
    // Load a pair of 3-channel images and convert to grayscale with OpenCV
    cv::Mat left_color = cv::imread(argv[1], cv::IMREAD_COLOR);
    cv::Mat right_color = cv::imread(argv[2], cv::IMREAD_COLOR);
    if (left_color.empty() || right_color.empty()) {
        fprintf(stderr, "OpenCV could not read %s / %s\n", argv[1], argv[2]);
        return 1;
    }
    cv::Mat cv_left, cv_right;
    cv::cvtColor(left_color, cv_left, cv::COLOR_BGR2GRAY);
    cv::cvtColor(right_color, cv_right, cv::COLOR_BGR2GRAY);

    auto gray_to_buffer = [](const cv::Mat &g) {
        Halide::Runtime::Buffer<uint8_t, 2> buf(g.cols, g.rows);
        buf.for_each_element([&](int i, int j) {
            buf(i, j) = g.at<uchar>(j, i);
        });
        return buf;
    };
    left = gray_to_buffer(cv_left);
    right = gray_to_buffer(cv_right);
#else
    // Without OpenCV, load with Halide's image IO and manually convert to grayscale
    auto load_gray = [](const char *path) {
        Halide::Runtime::Buffer<uint8_t, 3> color = load_and_convert_image(path);
        Halide::Runtime::Buffer<uint8_t, 2> gray(color.width(), color.height());
        gray.for_each_element([&](int i, int j) {
            uint32_t r = color(i, j, 0), g = color(i, j, 1), b = color(i, j, 2);
            gray(i, j) = (uint8_t)((r * 4899 + g * 9617 + b * 1868 + 8192) >> 14);
        });
        return gray;
    };
    left = load_gray(argv[1]);
    right = load_gray(argv[2]);
#endif

    Halide::Runtime::Buffer<int16_t, 2> output(left.width(), left.height());

    double best = benchmark([&]() {
        stereobm(left, right, output);
        output.device_sync();
    });
    printf("Halide time: %gms\n", best * 1e3);

#if STEREOBM_BUILD_OPENCV
    cv::Ptr<cv::StereoBM> bm = cv::StereoBM::create(DEPTH, WINSIZE);
    bm->setPreFilterType(cv::StereoBM::PREFILTER_XSOBEL);
    bm->setPreFilterCap(PREFILTER_CAP);
    bm->setMinDisparity(MIN_DISP);
    bm->setTextureThreshold(TEXTURE_THRESHOLD);
    bm->setUniquenessRatio(UNIQUENESS_RATIO);

    cv::Mat cv_disp;
    double cv_best = benchmark([&]() {
        bm->compute(cv_left, cv_right, cv_disp);
    });
    printf("OpenCV time: %gms\n", cv_best * 1e3);

    const int32_t invalid = (MIN_DISP - 1) * 16;
    long long compared = 0;
    long long sum_abs = 0;
    int max_abs = 0;
    if (output.width() != cv_disp.cols || output.height() != cv_disp.rows) {
        fprintf(stderr, "Output size %d x %d doesn't match OpenCV's output size %d x %d\n",
                output.width(), output.height(), cv_disp.cols, cv_disp.rows);
        return 1;
    }
    output.for_each_element([&](int i, int j) {
        int32_t h = output(i, j);
        int32_t o = cv_disp.at<short>(j, i);
        if (h == invalid || o == invalid) return;
        int diff = std::abs(h - o);
        compared++;
        sum_abs += diff;
        max_abs = std::max(max_abs, diff);
    });

    if (compared > 0) {
        printf("compared %lld pixels\n", compared);
        printf("mean abs diff: %g px\n",
               (sum_abs / (double)compared) / 16.0);
        printf("max abs diff:  %g px\n", max_abs / 16.0);
    } else {
        printf("no overlapping valid pixels to compare\n");
    }
#endif

    // normalize output to range [0, 255]
    int32_t dmin = INT32_MAX, dmax = INT32_MIN;
    output.for_each_value([&](int16_t v) {
        dmin = std::min(dmin, (int32_t)v);
        dmax = std::max(dmax, (int32_t)v);
    });
    double scale = (dmax > dmin) ? 255.0 / (dmax - dmin) : 0.0;
    Halide::Runtime::Buffer<uint8_t, 2> vis(output.width(), output.height());
    vis.for_each_element([&](int i, int j) {
        int n = (output(i, j) - dmin) * scale + 0.5;
        vis(i, j) = std::max(0, std::min(255, n));
    });
    convert_and_save_image(vis, argv[3]);

    printf("Success!\n");
    return 0;
}
