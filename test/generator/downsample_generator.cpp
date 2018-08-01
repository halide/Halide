#include "Halide.h"

using namespace Halide::ConciseCasts;
using namespace Halide::BoundaryConditions;

class Downsample : public Halide::Generator<Downsample> {
 public:
  Input<Buffer<uint8_t>> input_{"input", 2};
  Output<Buffer<uint8_t>> output_{"output", 2};

  void generate() {
    Var x, y;
    Func downx, downy, convx, convy, im16;
    im16(x, y) = u16(repeat_edge(input_)(x, y));
    convx(x, y) = im16(x - 2, y) +
                     4 * (im16(x - 1, y) + im16(x + 1, y)) +
                     6 * im16(x, y) +
                     im16(x + 2, y);
    convy(x, y) = convx(x, y - 2) +
                     4 * (convx(x, y - 1) + convx(x, y + 1)) +
                     6 * convx(x, y) +
                     convx(x, y + 2);
    downx(x, y) = convy(2 * x, y);
    downy(x, y) = downx(x, 2 * y);
    output_(x, y) = u8(downy(x, y) >> 8);

    auto target = get_target();
    if (target.has_feature(Target::HVX_128) || target.has_feature(Target::HVX_64)) {
      output_.hexagon();
    } else {
      // nothing
    }
  }
};

HALIDE_REGISTER_GENERATOR(Downsample, downsample);
