#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {

  // TODO: Replace the parameters with the meaningful constant names
  Image<float> data(128, 128, 64, 4);

  int pad = 1; // padding required to handle boundaries

  Func f_in_bound;
  f_in_bound = BoundaryConditions::repeat_edge(data, 0, 128,
                                                     0, 128);
  Image<float> W(3, 3, 64, 64), b(64);

  Var x, y, z, n;

  Func f_conv("conv");
  RDom r(0, 3, 0, 3, 0, 64);

  f_conv(x, y, z, n) = b(z);

  f_conv(x, y, z, n) += W(r.x, r.y, r.z, z) *
                        f_in_bound(x + r.x - pad,
                                   y + r.y - pad,
                                   r.z, n);

  Func f_ReLU("ReLU");
  f_ReLU(x, y, z, n) = max(0, f_conv(x, y, z, n));

  // Specifying estimates
  f_ReLU.estimate(x, 0, 128).
         estimate(y, 0, 128).
         estimate(z, 0, 64).
         estimate(n, 0, 4);

  // Auto schedule the pipeline
  Target target = get_target_from_environment();
  Pipeline p(f_ReLU);

  p.auto_schedule(target);

  // Inspect the schedule
  f_ReLU.print_loop_nest();

  // Run the schedule
  Image<float> out = p.realize(128, 128, 64, 4);

}
