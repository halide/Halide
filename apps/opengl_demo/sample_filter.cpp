#include "Halide.h"

class SampleFilter: public Halide::Generator<SampleFilter>
{
    public:

    Halide::ImageParam input{Halide::UInt(8), 3, "input"};

    Halide::Func build()
    {
	Halide::Func filter;
	Halide::Var x, y, c;

	filter(x, y, c) = select(c == 3, input(x,y,c), Halide::cast<uint8_t>(255.0f-input(x, y, c)));

        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1).set_bounds(0, 4);

        filter.output_buffer().dim(0).set_stride(4).dim(2).set_stride(1);
        filter.bound(c, 0, 4);

        if (get_target().has_feature(Halide::Target::OpenGL)) {
            filter.glsl(x,y,c);
        }

        return filter;
    }
};

Halide::RegisterGenerator<SampleFilter> sample_filter_generator{"sample_filter"};
