#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "harris.h"

#include "benchmark.h"
#include "halide_image.h"
#include "halide_image_io.h"

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace Halide::Tools;

int main(int argc, char **argv) {

    Image<float> input = load_image(argv[1]);
    Image<float> output(input.width()-6, input.height()-6);

    harris(input, output);

    // Timing code. Timing doesn't include copying the input data to
    // the gpu or copying the output back.
    double halide_t = benchmark(1, 10, [&]() {
        harris(input, output);
    });

    save_image(output, "output.png");

    double opencv_t;
    // Time OpenCV doing the same thing.
    {
        cv::Mat input_image = cv::imread(argv[1]);
        cv::Mat output_image;

        opencv_t = benchmark(10, 1, [&] () {
            cv::Mat gray;
            cv::cvtColor(input_image, gray, CV_BGR2GRAY);
            cv::cornerHarris(gray, output_image, 2, 3, 0.04);
        });

        cv::Mat normalized;
        cv::normalize( output_image, normalized, 0, 255, cv::NORM_MINMAX, CV_32FC1, cv::Mat() );
        normalized.convertTo(normalized, CV_8UC3);
        cv::imwrite("opencv_output.png", normalized);
    }
    printf("harris\t%f\t%f\n", halide_t*1e3, opencv_t*1e3);

    return 0;
}
