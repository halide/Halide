#include <assert.h>
#include <stdio.h>
#include <Halide.h>
using namespace Halide;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int call_count = 0;

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using sinf.
extern "C" DLLEXPORT int count_calls(buffer_t *out) {
    if (out->host) {
        call_count++;
        for (int32_t i = 0; i < out->extent[0]; i++) {
            for (int32_t j = 0; j < out->extent[1]; j++) {
	        out->host[i * out->stride[0] + j * out->stride[1]] = 42;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func count_calls;
    count_calls.define_extern("count_calls",
                              std::vector<ExternFuncArgument>(),
                              UInt(8), 2);

    Func f;
    Var x, y;
    f(x, y) = count_calls(x, y) + count_calls(x, y);
    count_calls.compute_cached();

    Image<uint8_t> out1 = f.realize(256, 256);
    Image<uint8_t> out2 = f.realize(256, 256);

    for (int32_t i = 0; i < 256; i++) {
        for (int32_t j = 0; j < 256; j++) {
            assert(out1(i, j) == (42 + 42));
            assert(out2(i, j) == (42 + 42));
        }
    }
    assert(call_count == 1);

    printf("Success!\n");
    return 0;
}
