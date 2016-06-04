#include "HalideRuntime.h"

#include <stdio.h>
#include <stdlib.h>

#include "error_codes.h"

void my_halide_error(void *user_context, const char *msg) {
    // Silently drop the error
    //printf("%s\n", msg);
}

void check(int result, int correct) {
    if (result != correct) {
        printf("The exit status was %d instead of %d\n", result, correct);
        exit(-1);
    }
}

int main(int argc, char **argv) {

    halide_set_error_handler(&my_halide_error);

    buffer_t in = {0}, out = {0};

    in.host = (uint8_t *)malloc(64*64*4);
    in.elem_size = 4;
    in.extent[0] = 64;
    in.stride[0] = 1;
    in.extent[1] = 64;
    in.stride[1] = 64;

    out.host = (uint8_t *)malloc(64*64*4);
    out.elem_size = 4;
    out.extent[0] = 64;
    out.stride[0] = 1;
    out.extent[1] = 64;
    out.stride[1] = 64;

    // First, a successful run.
    int result = error_codes(&in, 64, &out);
    int correct = halide_error_code_success;
    check(result, correct);

    // Passing 50 as the second arg violates the call to Func::bound
    // in the generator
    result = error_codes(&in, 50, &out);
    correct = halide_error_code_explicit_bounds_too_small;
    check(result, correct);

    // Would read out of bounds on the input
    in.extent[0] = 50;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_access_out_of_bounds;
    check(result, correct);
    in.extent[0] = 64;

    // Input buffer larger than 2GB
    in.extent[0] = 10000000;
    in.extent[1] = 10000000;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_buffer_extents_too_large;
    check(result, correct);
    in.extent[0] = 64;
    in.extent[1] = 64;

    // Input buffer requires addressing math that would overflow 32 bits.
    in.stride[1] = 0x7fffffff;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_buffer_allocation_too_large;
    check(result, correct);
    in.stride[1] = 64;

    // strides and extents are 32-bit signed integers. It's
    // therefore impossible to make a buffer_t that can address
    // more than 2^31 * 2^31 * dimensions elements, which is less
    // than 2^63, so there's no way a Halide pipeline can return
    // the above two error codes in 64-bit code.

    // stride[0] is constrained to be 1
    in.stride[0] = 2;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_constraint_violated;
    check(result, correct);
    in.stride[0] = 1;

    // The second argument is supposed to be between 0 and 64.
    result = error_codes(&in, -23, &out);
    correct = halide_error_code_param_too_small;
    check(result, correct);

    in.extent[0] = 108;
    out.extent[0] = 108;
    result = error_codes(&in, 108, &out);
    correct = halide_error_code_param_too_large;
    check(result, correct);
    in.extent[0] = 64;
    out.extent[0] = 64;

    // You can't pass nullptr as a buffer_t argument.
    result = error_codes(nullptr, 64, &out);
    correct = halide_error_code_buffer_argument_is_null;
    check(result, correct);

    printf("Success!\n");
    return 0;
}
