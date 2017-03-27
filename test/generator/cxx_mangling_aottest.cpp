#include <stdio.h>

#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include <assert.h>
#include <string.h>

#include "cxx_mangling.h"
#include "cxx_mangling_gpu.h"

using namespace Halide::Runtime;

int32_t extract_value_global(int32_t *arg) {
    return *arg;
}

namespace HalideTest {

int32_t extract_value_ns(const int32_t *arg) {
    return *arg;
}

}


namespace my_namespace {
class my_class {public: int foo;};
namespace my_subnamespace {
struct my_struct {int foo;};
}
}
union my_union {
    float a;
    int b;
};

int main(int argc, char **argv) {
    Buffer<uint8_t> input(100);

    for (int32_t i = 0; i < 100; i++) {
        input(i) = i;
    }

    Buffer<double> result(100);

    const halide_filter_metadata_t *m = HalideTest::cxx_mangling_metadata();
    assert(m != NULL);
    printf("Name is: %s\n", m->name);
    assert(strcmp(m->name, "cxx_mangling") == 0);

    int ptr_arg = 42;
    int *int_ptr = &ptr_arg;
    const int *const_int_ptr = &ptr_arg;
    void *void_ptr = nullptr;
    const void *const_void_ptr = nullptr;
    std::string *string_ptr = nullptr;
    const std::string *const_string_ptr = nullptr;

    // Don't bother calling this (we haven't linked in the CUDA support it needs),
    // just force a reference to ensure it is linked in.
    int (*f)(halide_buffer_t *,
             int8_t, uint8_t,
             int16_t, uint16_t,
             int32_t, uint32_t,
             int64_t, uint64_t,
             bool,
             float, double,
             int32_t *, int32_t const *,
             void *, void const *,
             void *, void const *,
             ::my_namespace::my_class const *, 
             struct ::my_namespace::my_subnamespace::my_struct const *, 
             my_union const *,
             halide_buffer_t *) = HalideTest::cxx_mangling_gpu;

    printf("HalideTest::cxx_mangling is at: %p\n", (void*) f);

    my_namespace::my_class mc;
    my_namespace::my_subnamespace::my_struct ms;
    my_union mu;

    int r = HalideTest::cxx_mangling(input, -1, 0xff, -1, 0xffff, -1, 0xffffffff,
                                     -1, 0xffffffffffffffffLL, true, 42.0, 4239.0f,
                                     int_ptr, const_int_ptr, void_ptr, const_void_ptr,
                                     string_ptr, const_string_ptr,
                                     &mc, &ms, &mu, result);
    if (r != 0) {
        fprintf(stderr, "Failure!\n");
        exit(1);
    }
    printf("Success!\n");
    return 0;
}
