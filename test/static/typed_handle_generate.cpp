#include "typed_handle_common.h"

#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

HalideExtern_1(uint8_t, typed_handle_get, TypedHandle *);

int main(int argc, char **argv) {
    Param<TypedHandle *> handle;
    Func output;
    output() = typed_handle_get(handle);

    output.compile_to_file("typed_handle", handle);
    return 0;
}
