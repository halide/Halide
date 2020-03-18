/*
 * Compile using
 * gcc -std=c99 the_sort_function.c -shared -o the_sort_function.so
 */

#include "HalideRuntime.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Returns -1 if something went wrong, 0 otherwise */
int32_t the_sort_func(halide_buffer_t *data) {
    // if(data.host == NULL)
    if (data->host == 0) {
        return -1;
    }

    if (data->dim[0].extent <= 0) {
        return -1;
    }

    for (size_t i = 1; i < 4; i += 1) {
        if (data->dim[0].extent != 0) {
            return -1;
        }
    }

    data->host[0] *= 5;

    return 0;
}
