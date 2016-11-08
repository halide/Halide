#include "Halide.h"

namespace {

class Example : public Halide::Generator<Example> {
public:
    GeneratorParam<Halide::Type> type{ "type", UInt(8) };
    GeneratorParam<int> channels{ "channels", 4 };

    ImageParam input{ UInt(8), 3, "input" };

    Func build() override {
        input = ImageParam(type, input.dimensions(), input.name());

        Var x("x"), y("y"), c("c");

        Func output("output");
        output(x, y, c) = input(input.width() - x - 1, y, c);

        output
            .bound(c, 0, channels)
            .reorder(c, x, y)
            .unroll(c);

        if (get_target().has_feature(Target::OpenGL)) {
            input.set_bounds(2, 0, channels);
            output.glsl(x, y, c);
        } else {
            Expr input_planar = input.stride(0) == 1;
            Expr input_chunky = input.stride(2) == 1;
            Expr output_planar = output.output_buffer().stride(0) == 1;
            Expr output_chunky = output.output_buffer().stride(2) == 1;
            Expr stride_specializations[] = {
              input_planar && output_planar,
              input_planar,
              output_planar,
              input_chunky && output_chunky
            };
            for (Expr condition : stride_specializations) {
              output
                  .specialize(condition)
                  .vectorize(x, natural_vector_size<float>())
                  .parallel(y);
            }
        }

        // Remove the constraint that prevents interleaved buffers
        input.set_stride(0, Expr());
        output.output_buffer().set_stride(0, Expr());

        return output;
    }
};

Halide::RegisterGenerator<Example> register_example{"example"};

}  // namespace
