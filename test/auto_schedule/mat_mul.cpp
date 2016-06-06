#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
  int size = 1024;
  Image<float> A(size, size);
  Image<float> B(size, size);
  Image<float> C(size, size);

  for (int y = 0; y < A.height(); y++) {
    for (int x = 0; x < A.width(); x++) {
      A(x, y) = rand() & 0xfff;
    }
  }

  for (int y = 0; y < B.height(); y++) {
    for (int x = 0; x < B.width(); x++) {
      B(x, y) = rand() & 0xfff;
    }
  }

  Var x, y;

  Func prod("prod");
  RDom r(0, size);

  prod(x, y) = 0.0f;
  prod(x, y) += A(x, r.x) * B(r.x, y);

  Func out;
  out(x, y) = prod(x, y);

  // Specifying estimates
  out.estimate(x, 0, size).estimate(y, 0, size);

  // Auto schedule the pipeline
  Target target = get_target_from_environment();
  Pipeline p(out);

  p.auto_schedule(target);

  // Inspect the schedule
  out.print_loop_nest();

  // Run the schedule
  Image<float> output = p.realize(size, size);
  printf("Success!\n");
  return 0;

}
