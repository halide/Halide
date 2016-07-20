#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

// We don't actually need Halide.h for this test
#include "HalideRuntime.h"

int main(int argc, char **argv) {
    srand(0);

    for (int trial = 0; trial < 10; trial++) {
        // Make a dense buffer
        halide_buffer_t buf = {0};
        halide_dimension_t shape[3];
        buf.dimensions = 3;
        buf.dim = shape;
        buf.type = halide_type_of<float>();

        size_t total_bytes = buf.type.bytes();
        for (int i = 0; i < 3; i++) {
            buf.dim[i].min = rand() & 15;
            buf.dim[i].extent = (rand() & 15) + 8;
            if (i == 0) {
                buf.dim[i].stride = 1;
            } else {
                buf.dim[i].stride = buf.dim[i-1].extent * buf.dim[i-1].stride;
            }
            total_bytes *= buf.dim[i].extent;
        }

        std::vector<uint8_t> data(total_bytes);
        buf.host = data.data();

        int count = 0;
        uint8_t *begin, *end;
        buf.for_every_contiguous_block([&](uint8_t *b, uint8_t *e) {count++; begin = b; end = e;});

        if (begin != buf.host) {
            printf("Incorrect begin: %p != %p\n", begin, buf.host);
            return -1;
        }
        if (end != buf.host + total_bytes) {
            printf("Incorrect end: %p != %p\n", end, buf.host + total_bytes);
            return -1;
        }
        if (count != 1) {
            printf("Incorrect count: %d != 1\n", count);
            return -1;
        }

        count = 0;
        buf.for_every_element<float>([&](float &x) {count++; x = 5.0f;});

        int correct_count = total_bytes / buf.type.bytes();
        if (count != correct_count) {
            printf("Incorrect count: %d != %d\n", count, correct_count);
            return -1;
        }

        for (size_t i = 0; i < total_bytes / sizeof(float); i++) {
            float val = ((float *)buf.host)[i];
            if (val != 5.0f) {
                printf("buf.host[%d] = %f instead of 5.0f\n",
                       (int)i, val);
                return -1;
            }
        }
    }

    for (int trial = 0; trial < 10; trial++) {
        // Make a sparse buffer
        halide_buffer_t buf = {0};
        halide_dimension_t shape[3];
        buf.dimensions = 3;
        buf.dim = shape;
        buf.type = halide_type_of<float>();

        size_t total_bytes = buf.type.bytes();
        for (int i = 0; i < 3; i++) {
            buf.dim[i].min = rand() & 15;
            buf.dim[i].extent = (rand() & 15) + 8;
            if (i == 0) {
                buf.dim[i].stride = 1;
            } else {
                buf.dim[i].stride = buf.dim[i-1].extent * std::abs(buf.dim[i-1].stride) + 17;
            }
            total_bytes *= buf.dim[i].extent;
        }

        // Fiddle with the signs on the strides, to cover flipped cases
        for (int i = 0; i < 3; i++) {
            if (rand() & 1) {
                buf.dim[i].stride = -buf.dim[i].stride;
            }
        }

        // Randomly permute the ordering of the dimensions. It shouldn't matter.
        int inner_dim = 0;
        switch (rand() % 6) {
        case 0: // 0 1 2
            break;
        case 1: // 0 2 1
            std::swap(buf.dim[1], buf.dim[2]);
            break;
        case 2: // 1 0 2
            std::swap(buf.dim[0], buf.dim[1]);
            inner_dim = 1;
            break;
        case 3: // 1 2 0
            std::swap(buf.dim[0], buf.dim[2]);
            std::swap(buf.dim[0], buf.dim[1]);
            inner_dim = 2;
            break;
        case 4: // 2 0 1
            std::swap(buf.dim[0], buf.dim[1]);
            std::swap(buf.dim[0], buf.dim[2]);
            inner_dim = 1;
            break;
        case 5: // 2 1 0
            std::swap(buf.dim[0], buf.dim[2]);
            inner_dim = 2;
            break;
        }

        std::vector<uint8_t> data(buf.size_in_bytes(), 0.0f);
        buf.host = data.data();
        // That's the right amount of data, but because we have
        // negative strides at play the address of the smallest
        // element is not the same as the host pointer.
        buf.host += buf.host - buf.begin();

        if (buf.begin() != data.data()) {
            printf("%p %p %p\n", buf.host, buf.begin(), data.data());
            return -1;
        }

        int count = 0;
        int span = 0;
        buf.for_every_contiguous_block([&](uint8_t *b, uint8_t *e) {span = (int)(e-b); count += span;});

        int correct_span = buf.dim[inner_dim].extent * buf.type.bytes();
        int correct_count = total_bytes;
        if (count != correct_count) {
            printf("Incorrect count: %d != %d\n", count, correct_count);
            return -1;
        }

        if (span != correct_span) {
            printf("Incorrect span: %d vs %d\n", span, correct_span);
            return -1;
        }

        count = 0;
        buf.for_every_element<float>([&](float &x) {count++; x = 5.0f;});

        correct_count = total_bytes / buf.type.bytes();
        if (count != correct_count) {
            printf("Incorrect count: %d != %d\n", count, correct_count);
            return -1;
        }

        int fives = 0;
        for (float *p = (float *)buf.begin(); p != (float *)buf.end(); p++) {
            if (*p == 5.0f) {
                fives++;
            }
        }

        if (fives != correct_count) {
            printf("Wrong number of fives: %d != %d\n", fives, correct_count);
            return -1;
        }

    }

    {
        // Try a case where the scanlines actually overlap in memory with an offset: a Toeplitz matrix
        halide_buffer_t buf = {0};
        halide_dimension_t shape[2];
        buf.dim = shape;
        buf.dimensions = 2;
        buf.type = halide_type_of<float>();
        std::vector<float> kernel(63); // Represents a symmetric kernel
        for (int i = 0; i < (int)kernel.size(); i++) {
            kernel[i] = (32 - std::abs(32 - i)) / 31.0f;
        }

        // The buffer will be that kernel as a 32x32 matrix
        buf.host = (uint8_t *)(&(kernel[32]));
        buf.dim[0].extent = 32;
        buf.dim[0].stride = 1;
        buf.dim[1].extent = 32;
        buf.dim[1].stride = -1;

        // Check we set up the strides correctly:
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                float *coeff = (float *)buf.address_of(i, j);
                float *correct_coeff = &kernel[i-j+32];
                if (coeff != correct_coeff)  {
                    printf("Toeplitz matrix set up wrong: %p vs %p at %d, %d\n",
                           coeff, correct_coeff, i, j);
                }
            }
        }

        uint8_t *begin, *end;
        int count = 0;
        buf.for_every_contiguous_block([&](uint8_t *b, uint8_t *e) {count++; begin = b; end = e;});
        if (begin != buf.begin()) {
            printf("Incorrect begin: %p != %p\n", begin, buf.begin());
            return -1;
        }
        if (end != buf.end()) {
            printf("Incorrect end: %p != %p\n", end, buf.end());
            return -1;
        }
        if (count != 1) {
            printf("Incorrect count: %d != 1\n", count);
            return -1;
        }

        count = 0;
        buf.for_every_element<float>([&](float &x) {count++;});
        if (count != 63) {
            // Note that this hits every memory location once, not 32*32.
            printf("Incorrect count: %d\n", count);
            return -1;
        }
    }

    {
        // A different case where the scanlines overlap - setting a stride to zero
        // TODO
    }

    printf("Success!\n");
    return 0;

}
