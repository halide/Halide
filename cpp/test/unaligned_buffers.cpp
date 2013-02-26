#include <stdio.h>
#include <Halide.h>

using namespace Halide;

// Check that a pipeline bails out gracefully if the input or output
// buffers are unaligned

bool error_occurred = false;
void my_error_handler(char *msg) {
    error_occurred = true;
}

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    Image<int> im(100);
    const buffer_t *aligned_buffer = (const buffer_t *)im;
    buffer_t misaligned_buffer = *aligned_buffer;
    misaligned_buffer.host++;
    Image<int> bad_im(&misaligned_buffer);    

    f(x) = bad_im(x*2);
    f.set_error_handler(my_error_handler);
    Image<int> out = f.realize(20);
    assert(error_occurred);

    error_occurred = false;
    
    g(x) = x;
    g.set_error_handler(my_error_handler);
    g.realize(bad_im);
    assert(error_occurred);

    printf("Success!\n");
    return 0;
}
