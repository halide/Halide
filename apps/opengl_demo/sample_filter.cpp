#include "Halide.h"

class SampleFilter: public Halide::Generator<SampleFilter>
{
public:
    Input<Buffer<uint8_t>> input{"input", 3};
    Output<Buffer<uint8_t>> output{"output", 3};

    void generate() {
    	Var x, y, c;

    	output(x, y, c) = select(c == 3, input(x,y,c), cast<uint8_t>(255.0f-input(x, y, c)));

        input.dim(0).set_stride(4)
             .dim(2).set_stride(1).set_bounds(0, 4);

        output.dim(0).set_stride(4)
              .dim(2).set_stride(1);
        output.bound(c, 0, 4);

        if (get_target().has_feature(Target::OpenGL)) {
            output.glsl(x,y,c);
        }
    }
};

HALIDE_REGISTER_GENERATOR(SampleFilter, sample_filter)
