#include <Halide.h>
#include <string.h>
#include <stdio.h>

using namespace Halide;

bool error_occurred;
void halide_error(char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    Var x, y;
    Func f;
    f(x, y) = x+y;

    // Dig out the raw function pointer so we can use it as if we were
    // compiling statically
    void (*function)(buffer_t *) = (void (*)(buffer_t *))(f.compile_jit());

    buffer_t out;
    memset(&out, 0, sizeof(out));
    out.host = (uint8_t *)malloc(10*10);
    out.elem_size = 1; // should be 4!
    out.extent[0] = 10;
    out.extent[1] = 10;
    out.stride[0] = 1;
    out.stride[1] = 10;

    f.set_error_handler(&halide_error);
    error_occurred = false;
    function(&out);

    if (error_occurred) {
        printf("Success!\n");
        return 0;
    } else {
        printf("There should have been a runtime error\n");
        return -1;
    }
}
