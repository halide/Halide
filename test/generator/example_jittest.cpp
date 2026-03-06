#include "Halide.h"

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

    {
        // Use the Callable interface to invoke a Generator.
        // We can call this on any Generator that is registered in the current process.
        Callable example = create_callable_from_generator(context, "example");

        Buffer<int32_t, 3> img(kSize, kSize, 3);
        int r = example(runtime_factor, img);
        assert(r == 0);

        verify(img, 1.f, runtime_factor, 3);
    }

    {
        // We can also make an explicitly-typed std::function if we prefer:
        auto example = create_callable_from_generator(context, "example").make_std_function<float, Buffer<int32_t, 3>>();

        Buffer<int32_t, 3> img(kSize, kSize, 3);
        int r = example(runtime_factor, img);
        assert(r == 0);

        verify(img, 1.f, runtime_factor, 3);
    }

    printf("Success!\n");
    return 0;
}
