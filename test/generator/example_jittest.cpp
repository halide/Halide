#include "Halide.h"

// When using Generators to JIT, just include the Generator .cpp file.
// Yes, this is a little unusual, but it's recommended practice.
#include "example_generator.cpp"

using Halide::Image;

const int kSize = 32;

void verify(const Image<int32_t> &img, float compiletime_factor, float runtime_factor, int channels) {
    for (int i = 0; i < kSize; i++) {
        for (int j = 0; j < kSize; j++) {
            for (int c = 0; c < channels; c++) {
                if (img(i, j, c) !=
                    (int32_t)(compiletime_factor * runtime_factor * c * (i > j ? i : j))) {
                    printf("img[%d, %d, %d] = %d\n", i, j, c, img(i, j, c));
                    exit(-1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    {
        // Create a Generator and set its GeneratorParams by a map
        // of key-value pairs. Values are looked up by name, order doesn't matter.
        // GeneratorParams not set here retain their existing values. (Note that
        // all Generators have a "target" GeneratorParam, which is just
        // a Halide::Target set using the normal syntax.)
        Example gen;
        gen.set_generator_param_values({ { "compiletime_factor", "2.3" },
                                         { "channels", "3" },
                                         { "target", "host" },
                                         { "enummy", "foo" },
                                         { "flag", "false" } });
        Image<int32_t> img = gen.build().realize(kSize, kSize, 3, gen.get_target());
        verify(img, 2.3, 1, 3);
    }
    {
        // You can also set the GeneratorParams after creation by setting the
        // member values directly, of course.
        Example gen;
        gen.compiletime_factor.set(2.9);
        Image<int32_t> img = gen.build().realize(kSize, kSize, 3);
        verify(img, 2.9, 1, 3);

        // You can change the GeneratorParams between each call to build().
        gen.compiletime_factor.set(0.1);
        gen.channels.set(4);
        Image<int32_t> img2 = gen.build().realize(kSize, kSize, 4);
        verify(img2, 0.1, 1, 4);

        // Setting non-existent GeneratorParams will fail with a user_assert.
        // gen->set_generator_param_values({{"unknown_name", "0.1"}});

        // Setting GeneratorParams to values that can't be properly parsed
        // into the correct type will fail with a user_assert.
        // gen->set_generator_param_values({{"compiletime_factor", "this is not a number"}});
        // gen->set_generator_param_values({{"channels", "neither is this"}});
        // gen->set_generator_param_values({{"enummy", "not_in_the_enum_map"}});
        // gen->set_generator_param_values({{"flag", "maybe"}});
        // gen->set_generator_param_values({{"target", "6502-8"}});
    }
    {
        // If you're fine with the default values of all GeneratorParams,
        // you can just use a temporary:
        Image<int32_t> img = Example().build().realize(kSize, kSize, 3);
        verify(img, 1, 1, 3);
    }
    {
        // Want to set both GeneratorParams and FilterParams
        // (aka Runtime Params aka plain-old-params)? The currently-recommended
        // approach is to set all before calling build() or realize().
        // (Better approaches should be possible in the future.)
        Example gen;
        gen.compiletime_factor.set(1.234f);
        gen.runtime_factor.set(3.456f);
        Image<int32_t> img = gen.build().realize(kSize, kSize, 3);
        verify(img, 1.234f, 3.456f, 3);
    }

    printf("Success!\n");
    return 0;
}
