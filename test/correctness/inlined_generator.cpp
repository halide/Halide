#include "Halide.h"

using namespace Halide;

void verify(const Buffer<int32_t> &img, float compiletime_factor, float runtime_factor, int runtime_offset) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y)) + runtime_offset;
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

class Example : public Generator<Example> {
public:
    using Generator<Example>::Generator;

    enum class SomeEnum { Foo, Bar };

    GeneratorParam<float> compiletime_factor{ "compiletime_factor", 1, 0, 100 };
    GeneratorParam<bool> vectorize{ "vectorize", true };

    Input<float> runtime_factor{ "runtime_factor", 1.0 };
    Input<int> runtime_offset{ "runtime_offset", 0 };

    Output<Func> output{ "output", Int(32), 3 };

    void generate() {
        Func f;
        f(x, y) = max(x, y);
        output(x, y, c) = cast(output.type(), f(x, y) * c * compiletime_factor * runtime_factor + runtime_offset);
    }

    void schedule() {
        output.bound(c, 0, 3).reorder(c, x, y).unroll(c);
        output.specialize(vectorize).vectorize(x, natural_vector_size(output.type()));
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

int main(int argc, char **argv) {
    GeneratorContext context(get_jit_target_from_environment());

    const int kSize = 32;
    const float kRuntimeFactor = 2.f;
    const int kRuntimeOffset = 32;
    {
        // If you have a Generator in a visible translation unit (i.e.
        // in the same source file, or visible via #include), you can
        // use it directly, even if it's not registered: just call
        // GeneratorContext.apply<GenType>() with values for all Inputs.
        // (Note that this uses the default values for all GeneratorParams.)
        auto gen = context.apply<Example>(kRuntimeFactor, kRuntimeOffset);  // gen's type is std::unique_ptr<Example>

        Buffer<int32_t> img = gen->realize(kSize, kSize, 3);
        verify(img, gen->compiletime_factor, kRuntimeFactor, kRuntimeOffset);
    }

    {
        // If you need to set GeneratorParams, it's a bit trickier:
        // you must first create the Generator, then set the GeneratorParam(s),
        // than call apply().
        auto gen = context.create<Example>();  // gen's type is std::unique_ptr<Example>

        // GeneratorParams must be set before calling apply()
        // (you'll assert-fail if you set them later).
        gen->compiletime_factor.set(2.5f);

        gen->apply(kRuntimeFactor, kRuntimeOffset);


        Buffer<int32_t> img = gen->realize(kSize, kSize, 3);
        verify(img, gen->compiletime_factor, kRuntimeFactor, kRuntimeOffset);
    }

    printf("Success!\n");
    return 0;
}

