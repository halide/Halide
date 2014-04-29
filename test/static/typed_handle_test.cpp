#include "typed_handle_common.h"
#include <typed_handle.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char **argv) {
    uint8_t out = 0;
    buffer_t result;
    memset(&result, 0, sizeof(result));
    result.host = &out;
    result.extent[0] = 1;
    result.elem_size = sizeof(out);

    TypedHandle test_handle(42);
    typed_handle(&test_handle, &result);

    assert(out == 42);

    printf("Success!\n");
    return 0;
}
