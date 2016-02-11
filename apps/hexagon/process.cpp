#include <stdio.h>
#include <memory.h>

#include "scale.h"

int main(int argc, char **argv) {
    const int W = 64;
    const int H = 64;

    uint8_t in_host[W * H * 3];
    uint8_t out_host[W * H * 3];
    memset(in_host, 0, W * H * 3);

    buffer_t in = { 0 };
    in.host = in_host;
    in.elem_size = 1;
    in.extent[0] = W;
    in.extent[1] = H;
    in.extent[2] = 3;
    in.stride[0] = 1;
    in.stride[1] = W;
    in.stride[2] = W * H;

    buffer_t out = in;
    out.host = out_host;

    printf("Running pipeline...");
    int result = scale(2, &in, &out);
    printf("done: %d\n", result);

    return result;
}
