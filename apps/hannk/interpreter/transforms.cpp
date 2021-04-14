#include "interpreter/transforms.h"

namespace hannk {

namespace {

bool is_input(const Op *op, TensorPtr t) {
    for (int i = 0; i < op->input_count(); ++i) {
        if (op->input(i) == t) {
            return true;
        }
    }
    return false;
}

bool is_output(const Op *op, TensorPtr t) {
    for (int i = 0; i < op->output_count(); ++i) {
        if (op->output(i) == t) {
            return true;
        }
    }
    return false;
}

bool is_used(const Op *op, TensorPtr t) {
    return is_input(op, t) || is_output(op, t);
}

template <typename T>
T *cast_op(Op *x) {
    class Caster : public OpVisitor {
    public:
        T *result = nullptr;

        void visit(T *op) {
            result = op;
        }
    };

    Caster caster;
    x->accept(&caster);
    if (caster.result == x) {
        return caster.result;
    } else {
        return nullptr;
    }
}

}  // namespace

void remove_dead_ops(Model *m) {
    // Find ops with outputs that are unused.
    // Go in reverse order so removing a dead op
    // enables earlier ops to be seen as dead.
    for (int i = (int)m->ops.size() - 1; i >= 0; --i) {
        Op *op = m->ops[i].get();
        bool dead = true;
        for (int j = 0; dead && j < op->input_count(); j++) {
            if (op->input(j)->is_output()) {
                dead = false;
                break;
            }
        }
        for (int j = 0; dead && j < op->output_count(); j++) {
            // An op isn't dead if its output is an output
            // of the graph.
            if (op->output(j)->is_output()) {
                dead = false;
                break;
            }

            for (int k = i + 1; k < (int)m->ops.size(); ++k) {
                Op *other_op = m->ops[k].get();
                if (is_input(other_op, op->output(j))) {
                    dead = false;
                    break;
                }
            }
        }

        if (dead) {
            m->ops.erase(m->ops.begin() + i);
        }
    }
}

namespace {

// We can alias two tensors if the input is not used after the output is written,
// and we meet a number of other requirements.
bool maybe_alias_tensors(TensorPtr input, TensorPtr output, std::vector<int> offset = {}) {
    if (input->rank() != output->rank()) {
        // TODO: We should be able to alias reshapes.
        return false;
    }

    if (input->type().bytes() != output->type().bytes()) {
        // We can't alias tensors with types of different size.
        return false;
    }

    // We can't alias an input that is an input or output.
    // TODO: We could, if we don't change the shape.
    if (input->is_input() || input->is_output()) {
        return false;
    }

    // We can't grow the bounds of the output tensor.
    // TODO: We could, if we allowed non-zero mins.
    Box input_bounds_with_offset = input->bounds();
    for (int i = 0; i < (int)offset.size(); ++i) {
        input_bounds_with_offset[i] += offset[i];
    }
    if (!is_subset_of(input_bounds_with_offset, output->bounds())) {
        return false;
    }

    input->set_alias_of(output, offset);
    return true;
}

// Try to alias outputs to inputs when it is safe.
class InPlace : public OpVisitor {
    using OpVisitor::visit;

    void maybe_alias_elementwise(ElementwiseOp *op) {
        for (int i = 0; i < op->input_count(); i++) {
            if (maybe_alias_tensors(op->input(i), op->output())) {
                // We can only alias one of the inputs to the output.
                return;
            }
        }
    }

    void visit(BinaryOp *op) {
        maybe_alias_elementwise(op);
    }

    void visit(UnaryOp *op) {
        maybe_alias_elementwise(op);
    }

    void visit(ConcatenationOp *op) {
        std::vector<int> offset(op->axis() + 1, 0);
        for (int i = 0; i < op->input_count(); i++) {
            maybe_alias_tensors(op->input(i), op->output(), offset);
            offset[op->axis()] += op->input(i)->extent(op->axis());
        }
    }

    void visit(PadOp *op) {
        if (!op->input(1) || !op->input(1)->is_constant()) {
            return;
        }
        assert(op->input(1)->is_allocated());

        auto padding = op->input(1)->buffer<const int32_t>();

        std::vector<int> offset(padding.extent(1));
        for (int d = 0; d < padding.extent(1); d++) {
            offset[d] = padding(0, d);
        }

        maybe_alias_tensors(op->input(), op->output(), offset);
    }

    void visit(ReshapeOp *op) {
        maybe_alias_tensors(op->input(), op->output());
    }
};

}  // namespace

void in_place(Model *m) {
    InPlace v;
    m->accept(&v);
}

namespace {

// Find ops that need padding and add an explicit pad op.
class PadForOps : public OpVisitor {
    void pad_for_op(Op *op, int input_idx, int output_idx) {
        TensorPtr input = op->input(input_idx);
        TensorPtr output = op->output(output_idx);
        BoundsMap deps = op->map_bounds(input_idx, output_idx);
        Box required = deps.evaluate(output->bounds());

        if (is_subset_of(required, input->bounds())) {
            return;
        }

        // Make a PadOp and a new tensor for the padded result.
        TensorPtr padded =
            std::make_shared<Tensor>(input->name() + "_padded", input->type(), required, input->quantization());
        op->set_input(padded);

        HalideBuffer<int32_t> padding_data(2, input->rank());
        // Center the crop, except for the channel dimension.
        // TODO: Is this always correct?
        padding_data(0, 0) = 0;
        padding_data(1, 0) = 0;
        for (int i = 1; i < input->rank(); i++) {
            padding_data(0, i) = (required[i].extent() - input->extent(i)) / 2;
            padding_data(1, i) = (required[i].extent() - input->extent(i) + 1) / 2;
        }
        TensorPtr padding = std::make_shared<Tensor>(input->name() + "_padding", padding_data);

        // Add the new tensor, op, and update the input.
        std::unique_ptr<Op> pad = ::hannk::make_unique<PadOp>(input, padding, padded);
        new_ops.emplace_back(std::move(pad));
    }

    void visit(Conv2DOp *op) {
        pad_for_op(op, 0, 0);

        // We also need to tile the filter.
        TensorPtr filter = op->filter();
        if (op->filter()->rank() == 4) {
            BoundsMap bounds = op->map_bounds(1, 0);
            Box tiled_shape = bounds.evaluate(op->output()->bounds());

            halide_type_t type = op->filter_type();
            QuantizationInfo quantization = filter->quantization();
            if (type.bits > filter->type().bits) {
                // We're widening the filter. Subtract the offset.
                std::fill(quantization.zero.begin(), quantization.zero.end(), 0);
            }
            TensorPtr tiled =
                std::make_shared<Tensor>(filter->name() + "_tiled", type, tiled_shape, quantization);
            op->set_input(1, tiled);

            std::unique_ptr<Op> tile = ::hannk::make_unique<TileConvFilterOp>(filter, tiled);
            new_ops.emplace_back(std::move(tile));
        }
    }

    void visit(DepthwiseConv2DOp *op) {
        if (op->depth_multiplier() != 1 && op->depth_multiplier() < op->output()->extent(0)) {
            TensorPtr input = op->input();
            BoundsMap bounds = op->map_bounds(0, 0);
            Box upsampled_shape = bounds.evaluate(op->output()->bounds());

            TensorPtr upsampled =
                std::make_shared<Tensor>(input->name() + "_upsampled", input->type(), upsampled_shape, input->quantization());
            op->set_input(0, upsampled);

            std::unique_ptr<Op> upsample = ::hannk::make_unique<UpsampleChannelsOp>(input, upsampled, op->depth_multiplier());
            op->set_depth_multiplier(1);
            new_ops.emplace_back(std::move(upsample));
        } else {
            pad_for_op(op, 0, 0);
        }
    }

    void visit(PoolOp *op) {
        if (op->op() == PoolOp::Average && op->padding() == Padding::Same) {
            // Pooling ops that normalize can't be padded :(.
            return;
        }

        pad_for_op(op, 0, 0);
    }

public:
    std::vector<std::unique_ptr<Op>> new_ops;
};

class FusePadOps : public OpVisitor {
    void visit(PadOp *op) {
        if (op->input()->producers().size() != 1 || op->input()->consumers().size() != 1) {
            return;
        }
        PadOp *prev_pad = cast_op<PadOp>(op->input()->producers().front());
        if (!prev_pad) {
            return;
        }

        op->set_input(prev_pad->input());

        auto prev_padding = prev_pad->input(1)->buffer<const int32_t>();
        auto padding = op->input(1)->buffer<int32_t>();

        for (int d = 0; d < std::min(prev_padding.dimensions(), padding.dimensions()); d++) {
            padding(0, d) += prev_padding(0, d);
            padding(1, d) += prev_padding(1, d);
        }
    }
};

}  // namespace

void pad_for_ops(Model *m) {
    PadForOps padder;
    m->accept(&padder);
    for (auto &i : padder.new_ops) {
        m->insert(std::move(i));
    }

    // Some networks use padding already for other reasons, so
    // we might have introduced two paddings in a row, which is
    // a waste.
    FusePadOps fuser;
    m->accept(&fuser);
}

namespace {

bool can_execute(const Op *op) {
    for (int i = 0; i < op->input_count(); i++) {
        if (!op->input(i)->is_constant()) {
            return false;
        }
        assert(op->input(i)->is_allocated());
    }
    return true;
}

}  // namespace

void fold_constants(Model *m) {
    std::vector<const Op *> to_remove;
    for (auto &i : m->ops) {
        if (can_execute(&*i)) {
            // Allocate all the outputs.
            for (int j = 0; j < i->output_count(); j++) {
                i->output(j)->allocate();
            }

            // Run the whole op.
            i->execute(i->output()->bounds());

            // Mark the outputs constant.
            for (int j = 0; j < i->output_count(); j++) {
                i->output(j)->set_constant();
            }

            to_remove.push_back(&*i);
        }
    }

    for (const Op *i : to_remove) {
        m->remove(i);
    }
}

}  // namespace hannk
