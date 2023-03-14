#include "HalideRuntime.h"

#include <stdio.h>
#include <stdlib.h>

#include "error_codes.h"

void my_halide_error(void *user_context, const char *msg) {
    // Silently drop the error
    // printf("%s\n", msg);
}

void check(int result, int correct) {
    if (result != correct) {
        printf("The exit status was %d instead of %d\n", result, correct);
        exit(1);
    }
}

int main(int argc, char **argv) {

    halide_set_error_handler(&my_halide_error);

    halide_buffer_t in = {0}, out = {0};
    halide_dimension_t shape[] = {{0, 64, 1},
                                  {0, 123, 64}};

    in.host = (uint8_t *)malloc(64 * 123 * 4);
    in.type = halide_type_of<int>();
    in.dim = shape;
    in.dimensions = 2;

    out.host = (uint8_t *)malloc(64 * 123 * 4);
    out.type = halide_type_of<int>();
    out.dim = shape;
    out.dimensions = 2;

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
    halide_dimension_t smaller[] = {{0, 50, 1},
                                    {0, 123, 64}};
    in.dim = smaller;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_access_out_of_bounds;
    check(result, correct);
    in.dim = shape;

    // buffer extent negative, but in a way that doesn't trigger oob checks
    {
        halide_dimension_t bad_shape[] = {{0, 64, 1},
                                          {0, -123, 64}};
        halide_buffer_t i = in, o = out;
        i.dim = bad_shape;
        o.dim = bad_shape;

        result = error_codes(&i, 0, &o);
        correct = halide_error_code_buffer_extents_negative;
        check(result, correct);
    }

    // Input buffer larger than 2GB
    halide_dimension_t huge[] = {{0, 10000000, 1},
                                 {0, 10000000, 64}};
    in.dim = huge;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_buffer_extents_too_large;
    check(result, correct);
    in.dim = shape;

    // Input buffer requires addressing math that would overflow 32 bits.
    halide_dimension_t huge_stride[] = {{0, 64, 1},
                                        {0, 123, 0x7fffffff}};
    in.dim = huge_stride;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_buffer_allocation_too_large;
    check(result, correct);
    in.dim = shape;

    // strides and extents are 32-bit signed integers. It's
    // therefore impossible to make a halide_buffer_t that can address
    // more than 2^31 * 2^31 * dimensions elements, which is less
    // than 2^63, so there's no way a Halide pipeline can return
    // the above two error codes in 64-bit code.

    // stride[0] is constrained to be 1
    halide_dimension_t wrong_stride[] = {{0, 64, 2},
                                         {0, 123, 64}};
    in.dim = wrong_stride;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_constraint_violated;
    check(result, correct);
    in.dim = shape;

    // The second argument is supposed to be between 0 and 64.
    result = error_codes(&in, -23, &out);
    correct = halide_error_code_param_too_small;
    check(result, correct);

    shape[0].extent = 108;
    result = error_codes(&in, 108, &out);
    correct = halide_error_code_param_too_large;
    check(result, correct);
    shape[0].extent = 64;

    // You can't pass nullptr as a halide_buffer_t argument.
    result = error_codes(nullptr, 64, &out);
    correct = halide_error_code_buffer_argument_is_null;
    check(result, correct);

    // Violate the custom requirement that the height of the input is 123
    halide_dimension_t too_tall[] = {{0, 64, 1},
                                     {0, 200, 64}};
    in.dim = too_tall;
    result = error_codes(&in, 64, &out);
    correct = halide_error_code_requirement_failed;
    check(result, correct);
    in.dim = shape;

    printf("Success!\n");
    return 0;
}
