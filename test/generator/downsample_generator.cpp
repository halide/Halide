#include "Halide.h"

using namespace Halide::ConciseCasts;
using namespace Halide::BoundaryConditions;

class Downsample : public Halide::Generator<Downsample> {
 public:
  Input<Buffer<uint8_t>> input_{"input", 2};
  Output<Buffer<uint16_t>> output_{"output", 2};

  void generate() {
    Var x, y;
    Func convx, convy, im16;
    im16(x, y) = u16(repeat_edge(input_)(x, y));
    convx(x, y) = im16(x - 2, y) +
                     4 * im16(x - 1, y) +
                     6 * im16(x, y) +
                     4 * im16(x + 1, y) +
                     im16(x + 2, y);
    convy(x) = convx(x, -2) +
                     4 * convx(x, -1) +
                     6 * convx(x, 0) +
                     4 * convx(x, 1) +
                     convx(x, 2);
    output_(x, y) = convy(2 * x);

    // don't set any of these
    //input_.dim(0).set_min(0);
    //input_.dim(0).set_extent(4);
    //input_.dim(1).set_min(0);
    //input_.dim(1).set_extent(1);

    // don't bound the min of output.x
    output_.bound_extent(x, 4);
    output_.bound(y, 0, 1);

    auto target = get_target();
    if (target.has_feature(Target::HVX_128)) {
      output_.hexagon();
    } else {
      // nothing
    }
  }
};

HALIDE_REGISTER_GENERATOR(Downsample, downsample);
