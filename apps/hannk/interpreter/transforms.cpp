#include "interpreter/transforms.h"

namespace hannk {

namespace {

template<typename T>
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

void remove_dead_ops(OpGroup *root) {
    // Find ops with outputs that are unused.
    // Go in reverse order so removing a dead op
    // enables earlier ops to be seen as dead.
    for (int i = root->op_count() - 1; i >= 0; --i) {
        Op *op = root->op(i);
        if (OpGroup *group = cast_op<OpGroup>(op)) {
            remove_dead_ops(group);
        }
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

            if (!op->output(j)->consumers().empty()) {
                dead = false;
                break;
            }
        }

        if (dead) {
            root->remove(op);
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
        for (int j = 0; j < op->output_count(); j++) {
            for (int i = 0; i < op->input_count(); i++) {
                if (maybe_alias_tensors(op->input(i), op->output(j))) {
                    // We can only alias one of the input to each output.
                    break;
                }
            }
        }
    }

    void visit(BinaryOp *op) {
        maybe_alias_elementwise(op);
    }

    void visit(UnaryOp *op) {
        maybe_alias_elementwise(op);
    }

    void visit(ElementwiseProgramOp *op) {
        maybe_alias_elementwise(op);
    }

    void visit(ConcatenationOp *op) {
        std::vector<int> offset(op->axis() + 1, 0);
        for (int i = 0; i < op->input_count(); i++) {
            maybe_alias_tensors(op->input(i), op->output(), offset);
            offset[op->axis()] += op->input(i)->extent(op->axis());
        }
    }

    void visit(SplitOp *op) {
        std::vector<int> offset(op->axis() + 1, 0);
        for (int i = 0; i < op->output_count(); i++) {
            maybe_alias_tensors(op->output(i), op->input(), offset);
            offset[op->axis()] += op->output(i)->extent(op->axis());
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

    void visit(OpGroup *op) {
        for (int i = 0; i < op->op_count(); i++) {
            op->op(i)->accept(this);
        }
    }
};

}  // namespace

void in_place(Op *op) {
    InPlace v;
    op->accept(&v);
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
            // Maybe more than one op uses this same filter...?
            filter->replace_all_consumers_with(tiled);

            std::unique_ptr<Op> tile = ::hannk::make_unique<TileConvFilterOp>(filter, tiled);
            new_ops.emplace_back(std::move(tile));
        }
    }

    void visit(DepthwiseConv2DOp *op) {
        pad_for_op(op, 0, 0);
    }

    void visit(PoolOp *op) {
        if (op->op() == PoolOp::Average && op->padding() == Padding::Same) {
            // Pooling ops that normalize can't be padded :(.
            return;
        }

        pad_for_op(op, 0, 0);
    }

    void visit(OpGroup *op) {
        for (int i = 0; i < op->op_count(); i++) {
            op->op(i)->accept(this);
        }
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

    void visit(OpGroup *op) {
        for (int i = 0; i < op->op_count(); i++) {
            op->op(i)->accept(this);
        }
    }
};

}  // namespace

void pad_for_ops(OpGroup *op) {
    PadForOps padder;
    op->accept(&padder);
    for (auto &i : padder.new_ops) {
        op->add(std::move(i));
    }

    // Some networks use padding already for other reasons, so
    // we might have introduced two paddings in a row, which is
    // a waste.
    FusePadOps fuser;
    op->accept(&fuser);
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

void fold_constants(OpGroup *root) {
    std::vector<const Op *> to_remove;
    for (int i = 0; i < root->op_count(); i++) {
        Op *op = root->op(i);
        if (OpGroup *group = cast_op<OpGroup>(op)) {
            fold_constants(group);
        }
        if (can_execute(op)) {
            // Allocate all the outputs.
            for (int j = 0; j < op->output_count(); j++) {
                op->output(j)->allocate();
            }

            // Run the whole op.
            op->execute();

            // Mark the outputs constant.
            for (int j = 0; j < op->output_count(); j++) {
                op->output(j)->set_constant();
            }

            to_remove.push_back(op);
        }
    }

    for (const Op *i : to_remove) {
        root->remove(i);
    }
}

}  // namespace hannk
