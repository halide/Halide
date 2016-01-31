#include "Halide.h"

using namespace Halide;

#include <iostream>
#include <limits>

#include "benchmark.h"
#include "halide_image_io.h"

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>

int main(int argc, char **argv) {

    std::cout << "unsharp\t";
    Image<float> in = Tools::load_image("input.png");

    // Define a 7x7 Gaussian Blur with a repeat-edge boundary condition.
    float sigma = 1.5f;

    Var x, y, c;
    Func kernel;
    kernel(x) = exp(-x*x/(2*sigma*sigma)) / (sqrtf(2*M_PI)*sigma);

    Func in_bounded = BoundaryConditions::repeat_edge(in);

    Func gray;
    gray(x, y) = max(in_bounded(x, y, 0),
                     max(in_bounded(x, y, 1),
                         in_bounded(x, y, 2)));

    Func blur_y;
    blur_y(x, y) = (kernel(0) * gray(x, y) +
                    kernel(1) * (gray(x, y-1) +
                                 gray(x, y+1)) +
                    kernel(2) * (gray(x, y-2) +
                                 gray(x, y+2)) +
                    kernel(3) * (gray(x, y-3) +
                                 gray(x, y+3)));

    Func blur_x;
    blur_x(x, y) = (kernel(0) * blur_y(x, y) +
                    kernel(1) * (blur_y(x-1, y) +
                                 blur_y(x+1, y)) +
                    kernel(2) * (blur_y(x-2, y) +
                                 blur_y(x+2, y)) +
                    kernel(3) * (blur_y(x-3, y) +
                                 blur_y(x+3, y)));

    Func sharpen;
    sharpen(x, y) = 2 * gray(x, y) - blur_x(x, y);

    Func ratio;
    ratio(x, y) = sharpen(x, y) / gray(x, y);

    Func result;
    result(x, y, c) = ratio(x, y) * in(x, y, c);

    // Schedule it.
    blur_y.compute_at(result, y).vectorize(x, 8);
    ratio.compute_at(result, y).vectorize(x, 8);
    result.vectorize(x, 8).parallel(y).reorder(x, c, y);

    // Benchmark the pipeline.
    Image<float> output(in.width(),
                        in.height(),
                        in.channels());

    std::cout << benchmark(10, 1, []() { result.realize(output); }) << "\t";

    Tools::save_image(output, "output.png");

    // Time OpenCV doing the same thing.
    {
        cv::Mat input_image = cv::imread("input.png");
        input_image.convertTo(input_image, CV_32FC3);
        cv::Mat output_image;

        double t = benchmark(10, 1, [&] () {
            cv::Mat channels[3];
            cv::split(input_image, channels);
            cv::Mat gray = cv::max(channels[0], cv::max(channels[1], channels[2]));

            cv::Mat blurry(gray.size(), CV_32FC1);
            GaussianBlur(gray, blurry, cv::Size(7, 7),
                         1.5f, 1.5f, cv::BORDER_REPLICATE);

            cv::Mat sharp = 2*gray - blurry;

            cv::Mat out_channels[3];
            cv::Mat ratio = sharp/gray;
            for (int c = 0; c < 3; c++) {
                out_channels[c] = channels[c].mul(ratio);
            }
            cv::merge(out_channels, 3, output_image);

            double t2 = current_time();
            best = std::min(best, t2 - t1);
        });
        std::cout << t << std::endl;

        output_image.convertTo(output_image, CV_8UC3);
        cv::imwrite("opencv_output.png", output_image);
    }

    return 0;
}


using namespace Halide::Tools;

using std::vector;

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n\t./interpolate in.png out.png\n" << std::endl;
        return 1;
    }

    ImageParam input(Float(32), 3);

    // Input must have four color channels - rgba
    input.set_bounds(2, 0, 4);

    const int levels = 10;

    Func downsampled[levels];
    Func downx[levels];
    Func interpolated[levels];
    Func upsampled[levels];
    Func upsampledx[levels];
    Var x("x"), y("y"), c("c");

    Func clamped = BoundaryConditions::repeat_edge(input);

    // This triggers a bug in llvm 3.3 (3.2 and trunk are fine), so we
    // rewrite it in a way that doesn't trigger the bug. The rewritten
    // form assumes the input alpha is zero or one.
    // downsampled[0](x, y, c) = select(c < 3, clamped(x, y, c) * clamped(x, y, 3), clamped(x, y, 3));
    downsampled[0](x, y, c) = clamped(x, y, c) * clamped(x, y, 3);

    for (int l = 1; l < levels; ++l) {
        Func prev = downsampled[l-1];

        if (l == 4) {
            // Also add a boundary condition at a middle pyramid level
            // to prevent the footprint of the downsamplings to extend
            // too far off the base image. Otherwise we look 512
            // pixels off each edge.
            Expr w = input.width()/(1 << l);
            Expr h = input.height()/(1 << l);
            prev = lambda(x, y, c, prev(clamp(x, 0, w), clamp(y, 0, h), c));
        }

        downx[l](x, y, c) = (prev(x*2-1, y, c) +
                             2.0f * prev(x*2, y, c) +
                             prev(x*2+1, y, c)) * 0.25f;
        downsampled[l](x, y, c) = (downx[l](x, y*2-1, c) +
                                   2.0f * downx[l](x, y*2, c) +
                                   downx[l](x, y*2+1, c)) * 0.25f;
    }
    interpolated[levels-1](x, y, c) = downsampled[levels-1](x, y, c);
    for (int l = levels-2; l >= 0; --l) {
        upsampledx[l](x, y, c) = (interpolated[l+1](x/2, y, c) +
                                  interpolated[l+1]((x+1)/2, y, c)) / 2.0f;
        upsampled[l](x, y, c) =  (upsampledx[l](x, y/2, c) +
                                  upsampledx[l](x, (y+1)/2, c)) / 2.0f;
        interpolated[l](x, y, c) = downsampled[l](x, y, c) + (1.0f - downsampled[l](x, y, 3)) * upsampled[l](x, y, c);
    }

    Func normalize("normalize");
    normalize(x, y, c) = interpolated[0](x, y, c) / interpolated[0](x, y, 3);

    Func final("final");
    final(x, y, c) = normalize(x, y, c);

    std::cout << "Finished function setup." << std::endl;

    int sched;
    Target target = get_target_from_environment();
    if (target.has_gpu_feature()) {
        sched = 4;
    } else {
        sched = 2;
    }

    switch (sched) {
    case 0:
    {
        std::cout << "Flat schedule." << std::endl;
        for (int l = 0; l < levels; ++l) {
            downsampled[l].compute_root();
            interpolated[l].compute_root();
        }
        final.compute_root();
        break;
    }
    case 1:
    {
        std::cout << "Flat schedule with vectorization." << std::endl;
        for (int l = 0; l < levels; ++l) {
            downsampled[l].compute_root().vectorize(x,4);
            interpolated[l].compute_root().vectorize(x,4);
        }
        final.compute_root();
        break;
    }
    case 2:
    {
        Var xi, yi;
        std::cout << "Flat schedule with parallelization + vectorization." << std::endl;
        for (int l = 1; l < levels-1; ++l) {
            downsampled[l]
                .compute_root()
                .parallel(y, 8)
                .vectorize(x, 4);
            interpolated[l]
                .compute_root()
                .parallel(y, 8)
                .unroll(x, 2)
                .unroll(y, 2)
                .vectorize(x, 8);
        }
        final
            .reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c)
            .tile(x, y, xi, yi, 2, 2)
            .unroll(xi)
            .unroll(yi)
            .parallel(y, 8)
            .vectorize(x, 8)
            .bound(x, 0, input.width())
            .bound(y, 0, input.height());
        break;
    }
    case 3:
    {
        std::cout << "Flat schedule with vectorization sometimes." << std::endl;
        for (int l = 0; l < levels; ++l) {
            if (l + 4 < levels) {
                Var yo,yi;
                downsampled[l].compute_root().vectorize(x,4);
                interpolated[l].compute_root().vectorize(x,4);
            } else {
                downsampled[l].compute_root();
                interpolated[l].compute_root();
            }
        }
        final.compute_root();
        break;
    }
    case 4:
    {
        std::cout << "GPU schedule." << std::endl;

        // Some gpus don't have enough memory to process the entire
        // image, so we process the image in tiles.
        Var yo, yi, xo, xi;
        final.reorder(c, x, y).bound(c, 0, 3).vectorize(x, 4);
        final.tile(x, y, xo, yo, xi, yi, input.width()/8, input.height()/8);
        normalize.compute_at(final, xo).reorder(c, x, y).gpu_tile(x, y, 16, 16, DeviceAPI::Default_GPU).unroll(c);

        // Start from level 1 to save memory - level zero will be computed on demand
        for (int l = 1; l < levels; ++l) {
            int tile_size = 32 >> l;
            if (tile_size < 1) tile_size = 1;
            if (tile_size > 8) tile_size = 8;
            downsampled[l].compute_root();
            if (false) {
                // Outer loop on CPU for the larger ones.
                downsampled[l].tile(x, y, xo, yo, x, y, 256, 256);
            }
            downsampled[l].gpu_tile(x, y, c, tile_size, tile_size, 4, DeviceAPI::Default_GPU);
            interpolated[l].compute_at(final, xo).gpu_tile(x, y, c, tile_size, tile_size, 4, DeviceAPI::Default_GPU);
        }
        break;
    }
    default:
        assert(0 && "No schedule with this number.");
    }

    // JIT compile the pipeline eagerly, so we don't interfere with timing
    final.compile_jit(target);

    Image<float> in_png = load_image(argv[1]);
    Image<float> out(in_png.width(), in_png.height(), 3);
    assert(in_png.channels() == 4);
    input.set(in_png);

    std::cout << "Running... " << std::endl;
    double best = benchmark(20, 1, [&]() { final.realize(out); });
    std::cout << " took " << best * 1e3 << " msec." << std::endl;

    vector<Argument> args;
    args.push_back(input);
    final.compile_to_assembly("test.s", args, target);

    save_image(out, argv[2]);

}
