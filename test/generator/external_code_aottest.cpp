#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

#include "external_code.h"

using namespace std;
using namespace Halide::Runtime;

int main() {
    Buffer<int32_t, 2> buf(10, 10);

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            buf(i, j) = i * 65536 + j * 256;
        }
    }

    Buffer<float, 2> out(10, 10);
    int ret_code = external_code(buf.raw_buffer(), out.raw_buffer());

    assert(ret_code == 0);

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            assert(out(i, j) == i * 65536 + j * 256 + 42);
        }
    }
    printf("Success!\n");
    return 0;
}
