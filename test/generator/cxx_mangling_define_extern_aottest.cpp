#include <stdio.h>

#include "HalideRuntime.h"
#include <assert.h>
#include <string.h>

#include "cxx_mangling_define_extern.h"

using namespace Halide;

int32_t extract_value_global(int32_t *arg) {
    return *arg;
}

namespace HalideTest {

int32_t extract_value_ns(const int32_t *arg) {
    return *arg;
}

}

int main(int argc, char **argv) {
    Buffer<uint8_t> input(100);

    for (int32_t i = 0; i < 100; i++) {
        input(i) = i;
    }

    Buffer<double> result(100);

    const void *user_context = nullptr;
    int ptr_arg = 42;
    assert(HalideTest::cxx_mangling_define_extern(input, &ptr_arg, &ptr_arg, result) == 0);

    printf("Success!\n");
    return 0;
}
