#include <stdio.h>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <assert.h>
#include <string>

#include "cxx_mangling_define_extern.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    Buffer<uint8_t, 1> input(10);

    for (int32_t i = 0; i < 10; i++) {
        input(i) = i;
    }

    Buffer<double, 1> result_1(10), result_2(10), result_3(10);

    const void *user_context = nullptr;
    int ptr_arg = 42;
    int *int_ptr = &ptr_arg;
    const int *const_int_ptr = &ptr_arg;
    void *void_ptr = nullptr;
    const void *const_void_ptr = nullptr;
    std::string *string_ptr = nullptr;
    const std::string *const_string_ptr = nullptr;
    int r = HalideTest::cxx_mangling_define_extern(user_context, input, int_ptr, const_int_ptr,
                                                   void_ptr, const_void_ptr, string_ptr, const_string_ptr,
                                                   result_1, result_2, result_3);
    if (r != 0) {
        fprintf(stderr, "Failure!\n");
        exit(1);
    }

    for (int i = 0; i < 10; ++i) {
        if (result_1(i) != i + 12.0 || result_2(i) != i + 12.0 || result_3(i) != i + 12.0) {
            fprintf(stderr, "Failure!\n");
            exit(1);
        }
    }

    printf("Success!\n");
    return 0;
}
