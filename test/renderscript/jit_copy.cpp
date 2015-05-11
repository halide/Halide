#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class ValidateInterleavedPipeline: public IRMutator {
//
// This is roughly the structure that we are trying to validate in this custom pass:
//
// parallel<Renderscript> (result$2.s0.y$2.__block_id_y, 0, result$2.extent.1) {
//   parallel<Renderscript> (result$2.s0.x$2.__block_id_x, 0, result$2.extent.0) {
//     ...
//     parallel<Renderscript> (.__thread_id_x, 0, 1) {
//       for<Renderscript> (result$2.s0.c$2, 0, 4) {
//         image_store("result$2",
//                     result$2.buffer,
//                     (result$2.s0.x$2.__block_id_x + result$2.min.0),
//                     (result$2.s0.y$2.__block_id_y + result$2.min.1),
//                     result$2.s0.c$2,
//                     image_load("input",
//                                input.buffer,
//                                ((result$2.s0.x$2.__block_id_x + result$2.min.0) - input.min.0),
//                                input.extent.0,
//                                ((result$2.s0.y$2.__block_id_y + result$2.min.1) - input.min.1),
//                                input.extent.1,
//                                result$2.s0.c$2,
//                                4))
//       }
//     }
//     ...
//   }
// }
//
    virtual void visit(const Call *call) {
        if (in_pipeline && call->call_type == Call::CallType::Intrinsic && call->name == Call::image_store) {
            assert(for_nest_level == 4);

            std::map<std::string, Expr> matches;

            assert(expr_match(
                StringImm::make("result"),
                call->args[0],
                matches));
            assert(expr_match(
                Variable::make(Handle(1), "result.buffer"),
                call->args[1],
                matches));
            assert(expr_match(
                Variable::make(Int(32), "result.s0.x.__block_id_x") + Variable::make(Int(32), "result.min.0"),
                call->args[2],
                matches));
            assert(expr_match(
                Variable::make(Int(32), "result.s0.y.__block_id_y") + Variable::make(Int(32), "result.min.1"),
                call->args[3],
                matches));
            assert(expr_match(
                Variable::make(Int(32), "result.s0.c"),
                call->args[4],
                matches));
            assert(expr_match(
                Call::make(
                    UInt(8),
                    Call::image_load,
                    {
                        StringImm::make("input"),
                        Variable::make(Handle(1), "input.buffer"),
                        (Variable::make(Int(32), "result.s0.x.__block_id_x") +
                            Variable::make(Int(32), "result.min.0")) -
                            Variable::make(Int(32), "input.min.0"),
                        Variable::make(Int(32), "input.extent.0"),
                        (Variable::make(Int(32), "result.s0.y.__block_id_y") +
                            Variable::make(Int(32), "result.min.1")) -
                            Variable::make(Int(32), "input.min.1"),
                        Variable::make(Int(32), "input.extent.1"),
                        Variable::make(Int(32), "result.s0.c"),
                        IntImm::make(channels)
                    },
                    Call::CallType::Intrinsic),
                call->args[5],
                matches));
        }
        IRMutator::visit(call);
    }

    void visit(const For *op) {
        if (in_pipeline) {
            assert(for_nest_level >= 0); // For-loop should show up in Pipeline.
            for_nest_level++;
            if (for_nest_level <= 3) {
                assert(op->for_type == ForType::Parallel);
            }
            assert(op->device_api == DeviceAPI::Renderscript);
        }
        IRMutator::visit(op);
    }

    void visit(const Pipeline* op) {
        assert(!in_pipeline); // There should be only one pipeline in the test.
        for_nest_level = 0;
        in_pipeline = true;

        assert(op->produce.defined());
        assert(!op->update.defined());
        assert(op->consume.defined());

        IRMutator::visit(op);
        stmt = Stmt();
    }
protected:
    int for_nest_level = -1;
    bool in_pipeline = false;
    int channels;

public:
    ValidateInterleavedPipeline(int _channels) : channels(_channels) {}
};

class ValidateInterleavedVectorizedPipeline: public ValidateInterleavedPipeline {
    virtual void visit(const Call *call) {
        if (in_pipeline && call->call_type == Call::CallType::Intrinsic && call->name == Call::image_store) {
            assert(for_nest_level == 3); // Should be three nested for-loops before we get to the first call.

            std::map<std::string, Expr> matches;

            assert(expr_match(
                Broadcast::make(StringImm::make("result"), channels),
                call->args[0],
                matches));
            assert(expr_match(
                Broadcast::make(Variable::make(Handle(1), "result.buffer"), channels),
                call->args[1],
                matches));
            assert(expr_match(
                Broadcast::make(Variable::make(Int(32), "result.s0.x.__block_id_x") + Variable::make(Int(32), "result.min.0"), channels),
                call->args[2],
                matches));
            assert(expr_match(
                Broadcast::make(Variable::make(Int(32), "result.s0.y.__block_id_y") + Variable::make(Int(32), "result.min.1"), channels),
                call->args[3],
                matches));
            assert(expr_match(
                Ramp::make(0, 1, channels), call->args[4], matches));
            assert(expr_match(
                Call::make(
                    UInt(8, channels),
                    Call::image_load,
                    {
                        Broadcast::make(StringImm::make("input"), channels),
                        Broadcast::make(Variable::make(Handle(1), "input.buffer"), channels),
                        Broadcast::make(
                            Add::make(
                                Variable::make(Int(32), "result.s0.x.__block_id_x"),
                                Variable::make(Int(32), "result.min.0")) -
                            Variable::make(Int(32), "input.min.0"),
                            channels),
                        Broadcast::make(Variable::make(Int(32), "input.extent.0"), channels),
                        Broadcast::make(
                            Add::make(
                                Variable::make(Int(32), "result.s0.y.__block_id_y"),
                                Variable::make(Int(32), "result.min.1")) -
                            Variable::make(Int(32), "input.min.1"),
                            channels),
                        Broadcast::make(Variable::make(Int(32), "input.extent.1"), channels),
                        Ramp::make(0, 1, channels),
                        Broadcast::make(IntImm::make(channels), channels)
                    },
                    Call::CallType::Intrinsic),
                call->args[5],
                matches));
        }
        IRMutator::visit(call);
    }
public:
    ValidateInterleavedVectorizedPipeline(int _channels) : ValidateInterleavedPipeline(_channels) {}
};

Image<uint8_t> make_interleaved_image(uint8_t *host, int W, int H, int channels) {
    buffer_t buf = { 0 };
    buf.host = host;
    buf.extent[0] = W;
    buf.stride[0] = channels;
    buf.extent[1] = H;
    buf.stride[1] = buf.stride[0] * buf.extent[0];
    buf.extent[2] = channels;
    buf.stride[2] = 1;
    buf.elem_size = 1;
    return Image<uint8_t>(&buf);
}

void copy_interleaved(bool vectorized = false, int channels = 4) {
    ImageParam input8(UInt(8), 3, "input");
    input8.set_stride(0, channels)
        .set_stride(1, Halide::Expr())
        .set_stride(2, 1)
        .set_bounds(2, 0, channels);  // expecting interleaved image
    uint8_t in_buf[128 * 128 * channels];
    uint8_t out_buf[128 * 128 * channels];
    Image<uint8_t> in = make_interleaved_image(in_buf, 128, 128, channels);
    Image<uint8_t> out = make_interleaved_image(out_buf, 128, 128, channels);
    input8.set(in);

    Var x, y, c;
    Func result("result");
    result(x, y, c) = input8(x, y, c);
    result.output_buffer()
        .set_stride(0, channels)
        .set_stride(1, Halide::Expr())
        .set_stride(2, 1)
        .set_bounds(2, 0, channels);  // expecting interleaved image

    result.bound(c, 0, channels);
    result.shader(x, y, c, DeviceAPI::Renderscript);
    if (vectorized) result.vectorize(c);

    result.add_custom_lowering_pass(
        vectorized?
            new ValidateInterleavedVectorizedPipeline(channels):
            new ValidateInterleavedPipeline(channels));
    result.realize(out);
}

int main(int argc, char **argv) {
    copy_interleaved(true, 4);
    copy_interleaved(false, 4);
    copy_interleaved(true, 3);
    copy_interleaved(false, 3);

    std::cout << "Done!" << std::endl;
    return 0;
}