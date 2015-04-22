#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// This visitor takes a string snapshot of all Pipeline IR nodes.
class SnapshotPipeline : public IRMutator {
    void visit(const Pipeline *op) {
        pipeline << op->produce;
    }

public:
    std::ostringstream pipeline;
};

Image<uint8_t> make_interleaved_image(uint8_t *host, int W, int H, int nChannels) {
    buffer_t buf = { 0 };
    buf.host = host;
    buf.extent[0] = W;
    buf.stride[0] = nChannels;
    buf.extent[1] = H;
    buf.stride[1] = buf.stride[0] * buf.extent[0];
    buf.extent[2] = nChannels;
    buf.stride[2] = 1;
    buf.elem_size = 1;
    return Image<uint8_t>(&buf);
}

Image<uint8_t> make_planar_image(uint8_t *host, int W, int H, int nChannels) {
    buffer_t buf = { 0 };
    buf.host = host;
    buf.extent[0] = W;
    buf.stride[0] = 1;
    buf.extent[1] = H;
    buf.stride[1] = buf.stride[0] * buf.extent[0];
    buf.extent[2] = nChannels;
    buf.stride[2] = buf.stride[1] * buf.extent[1];
    buf.elem_size = 1;
    return Image<uint8_t>(&buf);
}

std::string copy_interleaved(bool isVectorized = false, int nChannels = 4) {
    ImageParam input8(UInt(8), 3, "input");
    input8.set_stride(0, nChannels)
        .set_stride(1, Halide::Expr())
        .set_stride(2, 1)
        .set_bounds(2, 0, nChannels);  // expecting interleaved image
    uint8_t in_buf[128 * 128 * nChannels];
    uint8_t out_buf[128 * 128 * nChannels];
    Image<uint8_t> in = make_interleaved_image(in_buf, 128, 128, nChannels);
    Image<uint8_t> out = make_interleaved_image(out_buf, 128, 128, nChannels);
    // Image<uint8_t> in = make_planar_image(in_buf, 128, 128);
    // Image<uint8_t> out = make_planar_image(out_buf, 128, 128);
    input8.set(in);

    Var x, y, c;
    Func result("result");
    result(x, y, c) = input8(x, y, c);
    result.output_buffer()
        .set_stride(0, nChannels)
        .set_stride(1, Halide::Expr())
        .set_stride(2, 1)
        .set_bounds(2, 0, nChannels);  // expecting interleaved image

    result.bound(c, 0, nChannels);
    result.rs(x, y, c);
    if (isVectorized) result.vectorize(c);

    auto p = new SnapshotPipeline();
    result.add_custom_lowering_pass(p);
    result.realize(out);

    return p->pipeline.str();
}

std::string copy_interleaved_vectorized(int nChannels = 4) {
    return copy_interleaved(true, nChannels);
}

int main(int argc, char **argv) {
    std::string expected_vectorized_ir =
        R"|(let copy_to_device_result$2 = halide_copy_to_device(result.buffer, halide_rs_device_interface())
assert((copy_to_device_result$2 == 0), copy_to_device_result$2)
let copy_to_device_result = halide_copy_to_device(input.buffer, halide_rs_device_interface())
assert((copy_to_device_result == 0), copy_to_device_result)
parallel<RS> (result.s0.y.__block_id_y, 0, result.extent.1) {
  parallel<RS> (result.s0.x.__block_id_x, 0, result.extent.0) {
    allocate __shared[uint8 * 0]
    parallel<RS> (.__thread_id_x, 0, 1) {
      image_store(x4("result"), x4(result.buffer), x4((result.s0.x.__block_id_x + result.min.0)), x4((result.s0.y.__block_id_y + result.min.1)), ramp(0, 1, 4), image_load(x4("input"), x4(input.buffer), x4(((result.s0.x.__block_id_x + result.min.0) - input.min.0)), x4(input.extent.0), x4(((result.s0.y.__block_id_y + result.min.1) - input.min.1)), x4(input.extent.1), ramp(0, 1, 4), x4(4)))
    }
    free __shared
  }
}
set_dev_dirty(result.buffer, uint8(1))
)|";
    std::string pipeline_ir = copy_interleaved_vectorized(4);
    if (expected_vectorized_ir != pipeline_ir) {
        std::cout << "FAIL: Expected vectorized output:\n"
                  << expected_vectorized_ir
                  << "Actual output:\n" << pipeline_ir;
        return 1;
    }

    std::string expected_ir =
        R"|(let copy_to_device_result$5 = halide_copy_to_device(result$2.buffer, halide_rs_device_interface())
assert((copy_to_device_result$5 == 0), copy_to_device_result$5)
let copy_to_device_result$4 = halide_copy_to_device(input.buffer, halide_rs_device_interface())
assert((copy_to_device_result$4 == 0), copy_to_device_result$4)
parallel<RS> (result$2.s0.y$2.__block_id_y, 0, result$2.extent.1) {
  parallel<RS> (result$2.s0.x$2.__block_id_x, 0, result$2.extent.0) {
    allocate __shared[uint8 * 0]
    parallel<RS> (.__thread_id_x, 0, 1) {
      for<RS> (result$2.s0.c$2, 0, 4) {
        image_store("result$2", result$2.buffer, (result$2.s0.x$2.__block_id_x + result$2.min.0), (result$2.s0.y$2.__block_id_y + result$2.min.1), result$2.s0.c$2, image_load("input", input.buffer, ((result$2.s0.x$2.__block_id_x + result$2.min.0) - input.min.0), input.extent.0, ((result$2.s0.y$2.__block_id_y + result$2.min.1) - input.min.1), input.extent.1, result$2.s0.c$2, 4))
      }
    }
    free __shared
  }
}
set_dev_dirty(result$2.buffer, uint8(1))
)|";
    pipeline_ir = copy_interleaved(false, 4);
    if (expected_ir != pipeline_ir) {
        std::cout << "FAIL: Expected output:\n" << expected_ir
                  << "Actual output:\n" << pipeline_ir;
        return 2;
    }

    std::string expected_3_ir =
        R"|(let copy_to_device_result$8 = halide_copy_to_device(result$3.buffer, halide_rs_device_interface())
assert((copy_to_device_result$8 == 0), copy_to_device_result$8)
let copy_to_device_result$7 = halide_copy_to_device(input.buffer, halide_rs_device_interface())
assert((copy_to_device_result$7 == 0), copy_to_device_result$7)
parallel<RS> (result$3.s0.y$3.__block_id_y, 0, result$3.extent.1) {
  parallel<RS> (result$3.s0.x$3.__block_id_x, 0, result$3.extent.0) {
    allocate __shared[uint8 * 0]
    parallel<RS> (.__thread_id_x, 0, 1) {
      for<RS> (result$3.s0.c$3, 0, 3) {
        image_store("result$3", result$3.buffer, (result$3.s0.x$3.__block_id_x + result$3.min.0), (result$3.s0.y$3.__block_id_y + result$3.min.1), result$3.s0.c$3, image_load("input", input.buffer, ((result$3.s0.x$3.__block_id_x + result$3.min.0) - input.min.0), input.extent.0, ((result$3.s0.y$3.__block_id_y + result$3.min.1) - input.min.1), input.extent.1, result$3.s0.c$3, 3))
      }
    }
    free __shared
  }
}
set_dev_dirty(result$3.buffer, uint8(1))
)|";
    pipeline_ir = copy_interleaved(false, 3);
    if (expected_3_ir != pipeline_ir) {
        std::cout << "FAIL: Expected 3-channel output:\n" << expected_3_ir
                  << "Actual output:\n" << pipeline_ir;
        return 4;
    }

    std::string expected_vectorized_3_ir =
        R"|(let copy_to_device_result$11 = halide_copy_to_device(result$4.buffer, halide_rs_device_interface())
assert((copy_to_device_result$11 == 0), copy_to_device_result$11)
let copy_to_device_result$10 = halide_copy_to_device(input.buffer, halide_rs_device_interface())
assert((copy_to_device_result$10 == 0), copy_to_device_result$10)
parallel<RS> (result$4.s0.y$4.__block_id_y, 0, result$4.extent.1) {
  parallel<RS> (result$4.s0.x$4.__block_id_x, 0, result$4.extent.0) {
    allocate __shared[uint8 * 0]
    parallel<RS> (.__thread_id_x, 0, 1) {
      image_store(x3("result$4"), x3(result$4.buffer), x3((result$4.s0.x$4.__block_id_x + result$4.min.0)), x3((result$4.s0.y$4.__block_id_y + result$4.min.1)), ramp(0, 1, 3), image_load(x3("input"), x3(input.buffer), x3(((result$4.s0.x$4.__block_id_x + result$4.min.0) - input.min.0)), x3(input.extent.0), x3(((result$4.s0.y$4.__block_id_y + result$4.min.1) - input.min.1)), x3(input.extent.1), ramp(0, 1, 3), x3(3)))
    }
    free __shared
  }
}
set_dev_dirty(result$4.buffer, uint8(1))
)|";
    pipeline_ir = copy_interleaved(true, 3);
    if (expected_vectorized_3_ir != pipeline_ir) {
        std::cout << "FAIL: Expected vectorized x3 output:\n" << expected_vectorized_3_ir
                  << "Actual output:\n" << pipeline_ir;
        return 4;
    }

    std::cout << "Done!" << std::endl;
    return 0;
}