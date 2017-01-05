#include "Halide.h"

// Include the machine-generated .stub.h header file.
#include "example.stub.h"

using Halide::Buffer;

const int kSize = 32;

void verify(const Buffer<int32_t> &img, float compiletime_factor, float runtime_factor, int channels) {
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
    Halide::JITGeneratorContext context(Halide::get_target_from_environment());

    {
        // Create a Generator and set its Inputs and GeneratorParams.
        // We could just use initializer-list syntax, but we'll explicitly
        // set the fields by name for clarity.
        example::Inputs inputs;
        inputs.runtime_factor = 1.f;

        // The fields of the GeneratorParams struct are initialized to the
        // default values specified in the Generator, so we can just omit
        // any we don't want to change
        example::GeneratorParams gp;
        gp.compiletime_factor = 2.5f;
        gp.enummy = Enum_enummy::foo;
        // gp.channels = 3;  -- this is the default; no need to set

        auto gen = example(context, inputs, gp);

        // We must call schedule() before calling realize()
        gen.schedule();

        Halide::Buffer<int32_t> img = gen.realize(kSize, kSize, 3);
        verify(img, 2.5f, 1, 3);
    }

    {
        // Here, we'll use an initializer list for inputs, and omit
        // the GeneratorParams entirely to use their default values.
        auto gen = example(context, /* inputs: */ { 1.f });

        // We'll set "vectorize=false" in the ScheduleParams, just to
        // show that we can:
        example::ScheduleParams sp;
        sp.vectorize = false;
        gen.schedule(sp);

        Halide::Buffer<int32_t> img = gen.realize(kSize, kSize, 3);
        verify(img, 1, 1, 3);
    }

    printf("Success!\n");
    return 0;
}
