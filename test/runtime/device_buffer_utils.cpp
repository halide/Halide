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
        HALIDE_CHECK(user_context, c.chunk_size != 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
