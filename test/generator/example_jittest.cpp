#include "Halide.h"

// Include the machine-generated .stub.h header file.
#include "example.stub.h"

using namespace Halide;

const int kSize = 32;

void verify(const Buffer<int32_t, 3> &img, float compiletime_factor, float runtime_factor, int channels) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y));
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

int main(int argc, char **argv) {
    GeneratorContext context(get_jit_target_from_environment());
    const float runtime_factor = 4.5f;

    // Demonstrate (and test) various ways to use a Stub to invoke a Generator with the JIT.
    {
        // The simplest way is to just use the Stub's static "generate" method.
        //
        // The Generator's Input<>s are specified via a struct that is initialized
        // via an {initializer-list}, in the order the Input<>s are declared in the Generator.
        Func f = example::generate(context, {runtime_factor});
        Buffer<int32_t, 3> img = f.realize({kSize, kSize, 3});
        verify(img, 1.f, runtime_factor, 3);
    }

    {
        // Of course, we can fill in the Inputs struct by name if we prefer.
        example::Inputs inputs;
        inputs.runtime_factor = runtime_factor;

        Func f = example::generate(context, inputs);
        Buffer<int32_t, 3> img = f.realize({kSize, kSize, 3});
        verify(img, 1.f, runtime_factor, 3);
    }

    {
        // We can also (optionally) specify non-default values for the Generator's GeneratorParam<> fields.
        // Note that we could use an {initializer-list} for this struct, but usually do not:
        // the example::GeneratorParams struct is initialized to the correct default values,
        // so we usually prefer to set just the fields we want to change.
        example::GeneratorParams gp;
        gp.compiletime_factor = 2.5f;

        Func f = example::generate(context, {runtime_factor}, gp);
        Buffer<int32_t, 3> img = f.realize({kSize, kSize, 3});
        verify(img, gp.compiletime_factor, runtime_factor, 3);
    }

    {
        // generate() actually returns an Outputs struct, which contains all of the Generator's
        // Output<> fields. If there is just a single Output<>,
        // you can assign a Func to it directly (as we did in previous examples).
        //
        // In this case, we'll save it to a temporary to make the typing explicit.
        example::Outputs result = example::generate(context, {runtime_factor});

        Buffer<int32_t, 3> img = result.realize({kSize, kSize, 3});
        verify(img, 1.f, runtime_factor, 3);
    }

    printf("Success!\n");
    return 0;
}
