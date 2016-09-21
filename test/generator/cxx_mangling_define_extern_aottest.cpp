#include <stdio.h>

#include "HalideRuntime.h"
#include "HalideBuffer.h"

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
    Image<uint8_t> input(100);

    for (int32_t i = 0; i < 100; i++) {
        input(i) = i;
    }

    Image<double> result(100);

    const void *user_context = nullptr;
    int ptr_arg = 42;
    int *int_ptr = &ptr_arg;
    const int *const_int_ptr = &ptr_arg;
    void *void_ptr = nullptr;
    const void *const_void_ptr = nullptr;
    std::string *string_ptr = nullptr;
    const std::string *const_string_ptr = nullptr;
    assert(HalideTest::cxx_mangling_define_extern(user_context, input, int_ptr, const_int_ptr, 
        void_ptr, const_void_ptr, string_ptr, const_string_ptr, result) == 0);

    printf("Success!\n");
    return 0;
}
