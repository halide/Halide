#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "device_buffer_utils.h"

using namespace Halide::Runtime::Internal;

namespace {

halide_buffer_t make_buffer(halide_type_t type, uint8_t *host, int dimensions, halide_dimension_t *dim) {
    halide_buffer_t buf{};
    buf.host = host;
    buf.type = type;
    buf.dimensions = dimensions;
    buf.dim = dim;
    return buf;
}

}  // namespace

int main(int argc, char **argv) {
    void *user_context = (void *)1;
    const halide_type_t u8(halide_type_uint, 8);

    // A copy between buffers whose dimension counts disagree must be rejected
    // without reading past either buffer's dim array. make_buffer_copy walks
    // src->dimensions when computing the base offsets, so a src with more
    // dimensions than dst must not index dst->dim beyond its extent.
    {
        halide_dimension_t src_dim[4];
        halide_dimension_t dst_dim[2];
        for (auto &d : src_dim) {
            d = halide_dimension_t{0, 2, 1};
        }
        for (auto &d : dst_dim) {
            d = halide_dimension_t{0, 2, 1};
        }
        uint8_t src_host[16] = {0};
        uint8_t dst_host[4] = {0};
        halide_buffer_t src = make_buffer(u8, src_host, 4, src_dim);
        halide_buffer_t dst = make_buffer(u8, dst_host, 2, dst_dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 0);
    }

    // A well-formed same-shape copy still produces a real copy task.
    {
        halide_dimension_t src_dim[2] = {{0, 4, 1}, {0, 3, 4}};
        halide_dimension_t dst_dim[2] = {{0, 4, 1}, {0, 3, 4}};
        uint8_t host[12] = {0};
        halide_buffer_t src = make_buffer(u8, host, 2, src_dim);
        halide_buffer_t dst = make_buffer(u8, host, 2, dst_dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 12);
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            HALIDE_CHECK(user_context, c.extent[i] == 1);
        }
    }

    // Dense buffer with a leading extent-1 dimension (e.g. width-1 image)
    // must fold into a single contiguous chunk (#9563).
    {
        halide_dimension_t dim[2] = {{0, 1, 1}, {0, 64, 1}};
        uint8_t host[64] = {0};
        halide_buffer_t src = make_buffer(u8, host, 2, dim);
        halide_buffer_t dst = make_buffer(u8, host, 2, dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 64);
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            HALIDE_CHECK(user_context, c.extent[i] == 1);
            HALIDE_CHECK(user_context, c.src_stride_bytes[i] == 0);
            HALIDE_CHECK(user_context, c.dst_stride_bytes[i] == 0);
        }
    }

    // Dense buffer with an interior extent-1 dimension (e.g. single-row planar
    // {x: 64, y: 1, c: 3}) must fold into a single contiguous chunk (#9563).
    {
        halide_dimension_t dim[3] = {{0, 64, 1}, {0, 1, 64}, {0, 3, 64}};
        uint8_t host[192] = {0};
        halide_buffer_t src = make_buffer(u8, host, 3, dim);
        halide_buffer_t dst = make_buffer(u8, host, 3, dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 192);
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            HALIDE_CHECK(user_context, c.extent[i] == 1);
            HALIDE_CHECK(user_context, c.src_stride_bytes[i] == 0);
            HALIDE_CHECK(user_context, c.dst_stride_bytes[i] == 0);
        }
    }

    // Dense buffer with transposed strides and multiple extent-1 dimensions
    // (including arbitrary strides on the extent-1 dimensions) must sort and
    // fold into a single contiguous chunk.
    {
        halide_dimension_t dim[4] = {
            {0, 1, 999},
            {0, 5, 4},
            {0, 1, 777},
            {0, 4, 1},
        };
        uint8_t host[20] = {0};
        halide_buffer_t src = make_buffer(u8, host, 4, dim);
        halide_buffer_t dst = make_buffer(u8, host, 4, dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 20);
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            HALIDE_CHECK(user_context, c.extent[i] == 1);
        }
    }

    // Strided (non-dense) copy with a skipped extent-1 dimension must fold
    // the contiguous inner dimension and place the remaining outer dimension
    // at index 0 with no unused gap slots before it.
    {
        halide_dimension_t dim[3] = {
            {0, 1, 1},
            {0, 8, 1},
            {0, 4, 16},
        };
        uint8_t host[64] = {0};
        halide_buffer_t src = make_buffer(u8, host, 3, dim);
        halide_buffer_t dst = make_buffer(u8, host, 3, dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 8);
        HALIDE_CHECK(user_context, c.extent[0] == 4);
        HALIDE_CHECK(user_context, c.src_stride_bytes[0] == 16);
        HALIDE_CHECK(user_context, c.dst_stride_bytes[0] == 16);
        for (int i = 1; i < MAX_COPY_DIMS; i++) {
            HALIDE_CHECK(user_context, c.extent[i] == 1);
        }
    }

    // Overlapping crop copy where src has extent 1 in a dimension where dst
    // has extent > 1 should also skip the extent-1 copy dimension and fold.
    {
        halide_dimension_t src_dim[3] = {{0, 16, 1}, {0, 1, 999}, {0, 3, 16}};
        halide_dimension_t dst_dim[3] = {{0, 16, 1}, {0, 10, 48}, {0, 3, 16}};
        uint8_t src_host[48] = {0};
        uint8_t dst_host[480] = {0};
        halide_buffer_t src = make_buffer(u8, src_host, 3, src_dim);
        halide_buffer_t dst = make_buffer(u8, dst_host, 3, dst_dim);

        device_copy c = make_buffer_copy(&src, true, &dst, true);
        HALIDE_CHECK(user_context, c.chunk_size == 48);
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            HALIDE_CHECK(user_context, c.extent[i] == 1);
        }
    }

    print(user_context) << "Success!\n";
    return 0;
}
