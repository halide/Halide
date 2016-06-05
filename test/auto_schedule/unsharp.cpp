#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

  int H = 1920;
  int W = 1024;
  Image<float> in(H, W, 3);

  for (int y = 0; y < in.height(); y++) {
    for (int x = 0; x < in.width(); x++) {
      for (int c = 0; c < 3; c++) {
          in(x, y, c) = rand() & 0xfff;
      }
    }
  }

  // Define a 7x7 Gaussian Blur with a repeat-edge boundary condition.
  float sigma = 1.5f;

  Var x, y, c;
  Func kernel("kernel");
  kernel(x) = exp(-x*x/(2*sigma*sigma)) / (sqrtf(2*M_PI)*sigma);

  Func in_bounded = BoundaryConditions::repeat_edge(in);

  Func gray("gray");

  gray(x, y) = 0.299f * in_bounded(x, y, 0) + 0.587f * in_bounded(x, y, 1)
               + 0.114f * in_bounded(x, y, 2);

  Func blur_y("blur_y");
  blur_y(x, y) = (kernel(0) * gray(x, y) +
                  kernel(1) * (gray(x, y-1) +
                               gray(x, y+1)) +
                  kernel(2) * (gray(x, y-2) +
                               gray(x, y+2)) +
                  kernel(3) * (gray(x, y-3) +
                               gray(x, y+3)));

  Func blur_x("blur_x");
  blur_x(x, y) = (kernel(0) * blur_y(x, y) +
                  kernel(1) * (blur_y(x-1, y) +
                               blur_y(x+1, y)) +
                  kernel(2) * (blur_y(x-2, y) +
                               blur_y(x+2, y)) +
                  kernel(3) * (blur_y(x-3, y) +
                               blur_y(x+3, y)));

  Func sharpen("sharpen");
  sharpen(x, y) = 2 * gray(x, y) - blur_x(x, y);

  Func ratio("ratio");
  ratio(x, y) = sharpen(x, y) / gray(x, y);

  Func unsharp("unsharp");
  unsharp(x, y, c) = ratio(x, y) * in(x, y, c);

  unsharp.estimate(x, 0, in.width())
         .estimate(y, 0, in.height())
         .estimate(c, 0, in.channels());

  // Auto schedule the pipeline
  Target target = get_target_from_environment();
  Pipeline p(unsharp);

  p.auto_schedule(target);

  // Inspect the schedule
  unsharp.print_loop_nest();

  // Run the schedule
  Image<float> out = p.realize(in.width(), in.height(), in.channels());

  printf("Success!\n");
  return 0;
}
