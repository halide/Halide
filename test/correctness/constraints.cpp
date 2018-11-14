#include "Halide.h"
#include <stdio.h>

#include "test/common/halide_test_dirs.h"

using namespace Halide;

bool error_occurred = false;
void my_error_handler(void *user_context, const char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    Var x, y;
    Buffer<int> image_128x73(128, 73);
    Buffer<int> image_144x72(144, 72);

    // Test setting input constraints via ImageParam.dim().set_xxx()
    ImageParam param1(Int(32), 2);
    param1.dim(0).set_bounds(0, 128);

    Func f1;
    f1(x, y) = param1(x, y)*2;
    f1.set_error_handler(my_error_handler);

    // This should be fine
    param1.set(image_128x73);
    error_occurred = false;
    f1.realize(20, 20);
    if (error_occurred) {
        printf("Error incorrectly raised when constraining input buffer %d\n", __LINE__);
        return -1;
    }

    // This should be an error, because dimension 0 of image 2 is not from 0 to 128 like we promised
    param1.set(image_144x72);
    error_occurred = false;
    f1.realize(20, 20);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining input buffer %d\n", __LINE__);
        return -1;
    }

    // Test setting input constraints via scheduling language
    ImageParam param2(Int(32), 2);
    param2.bound(Halide::_0, 0, 128);

    Func f2;
    f2(x, y) = param2(x, y)*2;
    f2.set_error_handler(my_error_handler);

    // This should be fine
    param2.set(image_128x73);
    error_occurred = false;
    f2.realize(20, 20);
    if (error_occurred) {
        printf("Error incorrectly raised when constraining input buffer %d\n", __LINE__);
        return -1;
    }

    // This should be an error, because dimension 0 of image 2 is not from 0 to 128 like we promised
    param2.set(image_144x72);
    error_occurred = false;
    f2.realize(20, 20);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining input buffer %d\n", __LINE__);
        return -1;
    }

    // Test setting constraints via output_buffer().dim().set_xxx()
    Func h1;
    h1(x, y) = x*y;
    h1.set_error_handler(my_error_handler);
    h1.output_buffer().dim(0).set_bounds(0, ((h1.output_buffer().dim(0).extent())/64)*64);
    error_occurred = false;
    h1.realize(image_128x73);
    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }
    error_occurred = false;
    h1.realize(image_144x72);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }

    // Test setting constraints via scheduling language
    Func h2;
    h2(x, y) = x*y;
    h2.set_error_handler(my_error_handler);
    h2.align_bounds(x, 64);
    error_occurred = false;
    h2.realize(image_128x73);
    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }
    error_occurred = false;
    h2.realize(image_144x72);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }

    // Test setting constraints via output_buffer() *and* scheduling language()
    // (in this case, explict values must match inferred values)
    Func h3;
    h3(x, y) = x*y;
    h3.set_error_handler(my_error_handler);
    // TODO: no way to do align_bounds() on just extent, but constrain min to (say) zero,
    // hence this complex expression to match what align_bounds() does
    Expr aligned_min = h3.output_buffer().dim(0).min() / 64 * 64;
    Expr aligned_max = (h3.output_buffer().dim(0).min() + h3.output_buffer().dim(0).extent()) / 64 * 64;
    h3.output_buffer().dim(0).set_bounds(aligned_min, aligned_max - aligned_min);
    h3.align_bounds(x, 64);
    error_occurred = false;
    h3.realize(image_128x73);
    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }
    error_occurred = false;
    h3.realize(image_144x72);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }

    std::string assembly_file = Internal::get_test_tmp_dir() + "h.s";
    Internal::ensure_no_file_exists(assembly_file);

    // Also check it compiles ok without an inferred argument list
    error_occurred = false;
    h1.compile_to_assembly(assembly_file, {image_128x73}, "h");
    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer %d\n", __LINE__);
        return -1;
    }

    Internal::assert_file_exists(assembly_file);

    printf("Success!\n");
    return 0;
}
