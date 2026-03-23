#include "interpreter/transforms.h"
#include "util/small_vector.h"

#include <unordered_set>

namespace hannk {

namespace {

// Does *not* transfer ownership!
// If not castable, returns nullptr but original op is still valid.
template<typename T>
const T *cast_op(const Op *x) {
    class Caster : public OpVisitor {
        using OpVisitor::visit;

    public:
        const T *result = nullptr;

        void visit(const T *op) override {
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

class RemoveDeadOps : public OpMutator {
    using OpMutator::visit;

    std::unordered_set<Tensor *> root_outputs_;
    int removed_ = 0;

    bool is_root_output(const TensorPtr &t) const {
        return root_outputs_.count(t.get()) > 0;
    };

    bool is_dead(const Op *op) const {
        for (int j = 0; j < op->input_count(); j++) {
            const auto &input = op->input(j);
            if (is_root_output(input)) {
                // TODO: is it ever actually possible to have an Op's input be a root output?
                // (This is what the previous, OpVisitor-based code did)
                return false;
            }
        }
        for (int j = 0; j < op->output_count(); j++) {
            const auto &output = op->output(j);
            // An op isn't dead if its output is an output
            // of the graph.
            if (is_root_output(output)) {
                return false;
            }

            if (!output->consumers().empty()) {
                return false;
            }
        }
        return true;
    }

public:
    // Go in reverse order so removing a dead op enables earlier ops to be seen as dead.
    explicit RemoveDeadOps(const Op *root) {
        // Build a set so that we don't have to worry about the root op mutating
        for (int i = 0; i < root->output_count(); i++) {
            root_outputs_.insert(root->output(i).get());
        }
    }

    int removed() const {
        return removed_;
    }

protected:
    OpPtr visit_leaf(OpPtr op) override {
        if (is_dead(op.get())) {
            removed_++;
            return nullptr;
        } else {
            return op;
        }
    }

    OpPtr visit(std::unique_ptr<OpGroup> op) override {
        // Don't call super; roll our own code to go in reverse order.
        // TODO: is this even the right thing to do? May be better to
        // to a graph traverse andkeep removing ops until none get removed.

        std::vector<TensorPtr> inputs = op->inputs();
        std::vector<TensorPtr> outputs = op->outputs();

        const int old_op_count = op->op_count();

        std::vector<OpPtr> ops_new;
        ops_new.reserve(old_op_count);
        for (int i = 0; i < old_op_count; i++) {
            const int idx = (old_op_count - i - 1);
            OpPtr sub_op_old = op->take_op(idx);
            assert(sub_op_old != nullptr);
            OpPtr sub_op_new = mutate(std::move(sub_op_old));
            if (sub_op_new != nullptr) {
                ops_new.push_back(std::move(sub_op_new));
            }
        }
        std::reverse(ops_new.begin(), ops_new.end());
        auto new_op_group = make_op<OpGroup>(inputs, outputs, std::move(ops_new));

        // If the OpGroup is empty after mutation, remove it as well.
        if (new_op_group && new_op_group->op_count() == 0) {
            removed_++;
            return nullptr;
        }

        return new_op_group;
    }

private:
};

}  // namespace

OpPtr remove_dead_ops(OpPtr op) {
    return RemoveDeadOps(op.get()).mutate(std::move(op));
}

namespace {

class InPlaceReshape : public OpMutator {
    using OpMutator::visit;

    OpPtr visit(std::unique_ptr<ReshapeOp> op) override {
        TensorPtr input = op->input();
        TensorPtr output = op->output();

        HCHECK(input->type().bytes() == output->type().bytes());
        HCHECK(input->is_dense() == output->is_dense());
        HCHECK(input->number_of_elements() == output->number_of_elements());

        if (input->can_alias(output, AliasType::Reshaped)) {
            Tensor::make_reshape_alias(input, output);
        } else if (output->can_alias(input, AliasType::Reshaped)) {
            Tensor::make_reshape_alias(output, input);
        }

        return op;
    }
};

// Try to alias outputs to inputs when it is safe.
class InPlace : public OpMutator {
    using OpMutator::visit;

    // We can alias two tensors if the input is not used after the output is written,
    // and we meet a number of other requirements.
    bool maybe_alias_tensors(TensorPtr input, TensorPtr output, TensorOffset offset = {}) const {
        // We can't alias an input that is an input or output of the root graph.
        // TODO: We could, if we don't change the shape.
        if (is_root_input_or_output(input)) {
            return false;
        }

        // If the input is used anywhere else, we should not alias it.
        // TODO: This is conservative, we could alias it if it is the *last* use.
        if (input->consumers().size() != 1) {
            return false;
        }

        // We can't grow the bounds of the tensor we alias with.
        // TODO: We could, if we allowed non-zero mins. We also
        // could allow the max to grow, just not the min.
        Box input_bounds_with_offset = input->bounds();
        Box output_bounds_with_negative_offset = output->bounds();
        for (int i = 0; i < (int)offset.size(); ++i) {
            input_bounds_with_offset[i] += offset[i];
            output_bounds_with_negative_offset[i] -= offset[i];
        }

        if (is_subset_of(input_bounds_with_offset, output->bounds()) && input->can_alias(output, AliasType::Offset)) {
            Tensor::make_offset_alias(input, output, offset);
            return true;
        } else if (is_subset_of(output_bounds_with_negative_offset, input->bounds()) && output->can_alias(input, AliasType::Offset)) {
            for (int &i : offset) {
                i = -i;
            }
            Tensor::make_offset_alias(output, input, offset);
            return true;
        }

        return false;
    }

    void maybe_alias_elementwise(const ElementwiseOp *op) const {
        for (int j = 0; j < op->output_count(); j++) {
            for (int i = 0; i < op->input_count(); i++) {
                if (maybe_alias_tensors(op->input(i), op->output(j))) {
                    // We can only alias one of the input to each output.
                    break;
                }
            }
        }
    }

    OpPtr visit(std::unique_ptr<BinaryOp> op) override {
        maybe_alias_elementwise(op.get());
        return op;
    }

    OpPtr visit(std::unique_ptr<UnaryOp> op) override {
        maybe_alias_elementwise(op.get());
        return op;
    }

    OpPtr visit(std::unique_ptr<ElementwiseProgramOp> op) override {
        maybe_alias_elementwise(op.get());
        return op;
    }

    OpPtr visit(std::unique_ptr<ConcatenationOp> op) override {
        bool is_no_op = true;
        TensorOffset offset(op->axis() + 1);
        for (int i = 0; i < op->input_count(); i++) {
            is_no_op = is_no_op && maybe_alias_tensors(op->input(i), op->output(), offset);
            is_no_op = is_no_op && op->input(i)->quantization() == op->output()->quantization();
            offset[op->axis()] += op->input(i)->extent(op->axis());
        }
        if (is_no_op) {
            // TODO: Try actually deleting the op?
            op->set_no_op();
        }
        return op;
    }

    OpPtr visit(std::unique_ptr<SplitOp> op) override {
        bool is_no_op = true;
        TensorOffset offset(op->axis() + 1);
        for (int i = 0; i < op->output_count(); i++) {
            is_no_op = is_no_op && maybe_alias_tensors(op->input(), op->output(i), offset);
            is_no_op = is_no_op && op->output(i)->quantization() == op->input()->quantization();
            offset[op->axis()] -= op->output(i)->extent(op->axis());
        }
        if (is_no_op) {
            // TODO: Try actually deleting the op?
            op->set_no_op();
        }
        return op;
    }

    OpPtr visit(std::unique_ptr<PadOp> op) override {
        if (!op->input(1) || !op->input(1)->is_constant()) {
            return op;
        }
        assert(op->input(1)->is_allocated());

        auto padding = op->input(1)->buffer<const int32_t>();

        TensorOffset offset(padding.extent(1));
        for (int d = 0; d < padding.extent(1); d++) {
            offset[d] = padding(0, d);
        }

        maybe_alias_tensors(op->input(), op->output(), offset);
        return op;
    }

    std::unordered_set<Tensor *> inputs_and_outputs_;

    bool is_root_input_or_output(const TensorPtr &t) const {
        return inputs_and_outputs_.count(t.get()) > 0;
    };

public:
    explicit InPlace(const Op *root) {
        // Build a set so that we don't have to worry about the root op mutating
        for (int i = 0; i < root->input_count(); i++) {
            inputs_and_outputs_.insert(root->input(i).get());
        }
        for (int i = 0; i < root->output_count(); i++) {
            inputs_and_outputs_.insert(root->output(i).get());
        }
    }
};

}  // namespace

OpPtr in_place(OpPtr op) {
    // Always check for ReshapeOp before anything else; we want
    // to try our best to alias those tensors, and the luck of the
    // draw could put other aliases in place first that could thwart this.
    InPlaceReshape handle_reshapes;
    op = handle_reshapes.mutate(std::move(op));

    InPlace handle_in_place(op.get());
    op = handle_in_place.mutate(std::move(op));

    return op;
}

namespace {

void replace_consumers(const TensorPtr &from, const TensorPtr &to) {
    // We need to make a copy of the list of consumers so it doesn't get invalidated
    // by set_input below.
    auto consumers = from->consumers();
    for (Op *i : consumers) {
        for (int j = 0; j < i->input_count(); j++) {
            if (i->input(j).get() == from.get()) {
                i->set_input(j, to);
            }
        }
    }
}

// Find ops that need padding and add an explicit pad op.
class PadForOps : public OpMutator {
    using OpMutator::visit;

    std::unique_ptr<PadOp> get_padding_for_op(const Op *op, int input_idx = 0, int output_idx = 0) {
        TensorPtr input = op->input(input_idx);
        TensorPtr output = op->output(output_idx);
        BoundsMap deps = op->map_bounds(input_idx, output_idx);
        Box required = deps.evaluate(output->bounds());

        if (is_subset_of(required, input->bounds())) {
            return nullptr;
        }

        // Make a PadOp and a new tensor for the padded result.
        TensorPtr padded = std::make_shared<Tensor>(input->name() + ".padded",
                                                    input->type(), required, input->quantization());

        HalideBuffer<int32_t, 2> padding_data(2, input->rank());
        // Center the crop, except for the channel dimension.
        // TODO: Is this always correct?
        const int r = input->rank();
        padding_data(0, r - 1) = 0;
        padding_data(1, r - 1) = 0;
        for (int i = 1; i < input->rank(); i++) {
            padding_data(0, r - i - 1) = (required[i].extent() - input->extent(i)) / 2;
            padding_data(1, r - i - 1) = (required[i].extent() - input->extent(i) + 1) / 2;
        }
        TensorPtr padding = std::make_shared<Tensor>(input->name() + "_padding", padding_data);
        padding->set_constant();

        // Add the new tensor, op, and update the input.
        return make_prepared_op<PadOp>(input, padding, padded);
    }

    OpPtr visit(std::unique_ptr<ConvOp> op) override {
        OpPtr padding = get_padding_for_op(op.get());
        OpPtr tile = nullptr;

        // We also need to tile the filter.
        TensorPtr filter = op->filter();
        if (op->filter()->rank() == op->input()->rank()) {
            // This op has not yet had its filter tiled, do it now.
            BoundsMap bounds = op->map_bounds(1, 0);
            Box tiled_shape = bounds.evaluate(op->output()->bounds());

            halide_type_t type = op->filter_type();
            QuantizationInfo quantization = filter->quantization();
            if (type.bits > filter->type().bits) {
                // We're widening the filter. Subtract the offset.
                std::fill(quantization.zero.begin(), quantization.zero.end(), 0);
            }
            TensorPtr tiled = std::make_shared<Tensor>(filter->name() + ".tiled",
                                                       type, tiled_shape, quantization);
            // Maybe more than one op uses this same filter...?
            replace_consumers(filter, tiled);

            tile = make_prepared_op<TileConvFilterOp>(filter, tiled);
        }

        if (padding || tile) {
            TensorPtr conv_input = op->input();
            TensorPtr conv_filter = op->filter();

            std::vector<OpPtr> new_ops;
            if (padding) {
                conv_input = padding->output();
                new_ops.push_back(std::move(padding));
            }

            if (tile) {
                conv_filter = tile->output();
                new_ops.push_back(std::move(tile));
            }

            auto inputs = op->inputs();
            auto outputs = op->outputs();
            op = make_prepared_op<ConvOp>(conv_input, conv_filter, op->bias(), op->output(),
                                          op->stride(), op->dilation(), op->padding(), op->activation());
            new_ops.push_back(std::move(op));

            return make_prepared_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(new_ops));
        } else {
            return op;
        }
    }

    OpPtr visit(std::unique_ptr<DepthwiseConv2DOp> op) override {
        OpPtr upsample_op = nullptr;
        if (op->depth_multiplier() != 1 && op->depth_multiplier() < op->output()->extent(0)) {
            // Make an UpsampleChannels op and a new tensor for the upsampled result.
            TensorPtr input = op->input();
            TensorPtr output = op->output();
            Box upsampled_shape = input->bounds();
            upsampled_shape[0].min *= op->depth_multiplier();
            upsampled_shape[0].max = (upsampled_shape[0].max + 1) * op->depth_multiplier() - 1;
            TensorPtr upsampled = std::make_shared<Tensor>(input->name() + ".upsampled",
                                                           input->type(), upsampled_shape, input->quantization());

            // Add the new tensor, op, and update the input.
            upsample_op = make_prepared_op<UpsampleChannelsOp>(input, op->depth_multiplier(), upsampled);

            op = make_prepared_op<DepthwiseConv2DOp>(upsampled, op->filter(), op->bias(), op->output(),
                                                     /*depth_multiplier*/ 1, op->stride(), op->dilation(),
                                                     op->padding(), op->activation());
        }

        // TODO: It might be worth enabling UpsampleChannels to handle padding, and fusing the padding
        // in the case we need to upsample the channels.
        OpPtr padding_op = get_padding_for_op(op.get());

        if (padding_op || upsample_op) {
            std::vector<OpPtr> new_ops;
            if (upsample_op) {
                new_ops.push_back(std::move(upsample_op));
            }

            if (padding_op) {
                TensorPtr padding_output = padding_op->output();
                new_ops.push_back(std::move(padding_op));
                op = make_prepared_op<DepthwiseConv2DOp>(padding_output, op->filter(), op->bias(), op->output(),
                                                         op->depth_multiplier(), op->stride(), op->dilation(),
                                                         op->padding(), op->activation());
            }

            auto inputs = op->inputs();
            auto outputs = op->outputs();
            new_ops.push_back(std::move(op));

            return make_prepared_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(new_ops));
        } else {
            return op;
        }
    }

    template<class T, class... Args>
    std::unique_ptr<T> make_prepared_op(Args &&...args) {
        auto op = std::make_unique<T>(std::forward<Args>(args)...);
        if (!op->prepare()) {
            HLOG(ERROR) << "pad_for_ops: new_op " << op->name() << " failed prepare()";
            prepare_failed = true;
        }
        return op;
    }

public:
    bool prepare_failed = false;
};

}  // namespace

OpPtr pad_for_ops(OpPtr op) {
    PadForOps padder;
    op = padder.mutate(std::move(op));
    if (padder.prepare_failed) {
        return nullptr;
    }
    return op;
}

namespace {

class FusePadOps : public OpMutator {
    using OpMutator::visit;

    OpPtr visit(std::unique_ptr<PadOp> op) override {
        TensorPtr input = op->input();
        if (input->producers().size() != 1 || input->consumers().size() != 1) {
            return op;
        }
        const PadOp *prev_pad = cast_op<PadOp>(input->producers().front());
        if (!prev_pad) {
            return op;
        }

        // Combine prev's padding into our padding. (We'll rely on remove_dead_ops
        // to get rid of the prev padding later on.)

        auto prev_padding = prev_pad->padding()->buffer<const int32_t>();
        auto cur_padding = op->padding()->buffer<int32_t>();
        for (int d = 0; d < std::min(prev_padding.dimensions(), cur_padding.dimensions()); d++) {
            cur_padding(0, d) += prev_padding(0, d);
            cur_padding(1, d) += prev_padding(1, d);
        }

        return make_prepared_op<PadOp>(prev_pad->input(), op->padding(), op->output());
    }

    template<class T, class... Args>
    std::unique_ptr<T> make_prepared_op(Args &&...args) {
        auto op = std::make_unique<T>(std::forward<Args>(args)...);
        if (!op->prepare()) {
            HLOG(ERROR) << "fuse_pad_ops: new_op " << op->name() << " failed prepare()";
            prepare_failed = true;
        }
        return op;
    }

public:
    bool prepare_failed = false;
};

}  // namespace

OpPtr fuse_pad_ops(OpPtr op) {
    // Some networks use padding already for other reasons, so
    // we might have introduced two paddings in a row, which is
    // a waste. (This should be run after flatten_groups().)
    FusePadOps fuser;
    op = fuser.mutate(std::move(op));
    if (fuser.prepare_failed) {
        return nullptr;
    }
    return op;
}

namespace {

bool can_execute_with_all_constant_inputs(const Op *op) {
    for (int i = 0; i < op->input_count(); i++) {
        if (!op->input(i)->is_constant()) {
            return false;
        }
        assert(op->input(i)->is_allocated());
    }
    return true;
}

class ConstantFolder : public OpMutator {
    using OpMutator::visit;

    OpPtr visit_leaf(OpPtr op) override {
        if (can_execute_with_all_constant_inputs(op.get())) {
            // Allocate all the outputs.
            // Since we aren't ready for arena allocation,
            // we'll just do these as one-off heap allocs.
            for (int j = 0; j < op->output_count(); j++) {
                // Note that an output could be 'allocated' here if it
                // is the result of a ReshapeOp that aliases constant data.
                if (!op->output(j)->is_allocated()) {
                    op->output(j)->allocate_from_heap();
                }
            }

            // Run the whole op.
            op->execute();

            // Mark the outputs constant.
            for (int j = 0; j < op->output_count(); j++) {
                op->output(j)->set_constant();
            }

            return nullptr;
        } else {
            return op;
        }
    }
};

}  // namespace

OpPtr fold_constants(OpPtr op) {
    ConstantFolder folder;
    return folder.mutate(std::move(op));
}

namespace {

class GroupFlattener : public OpMutator {
    using OpMutator::visit;

    OpPtr visit_leaf(OpPtr op) override {
        flattened.push_back(std::move(op));
        return nullptr;
    }

public:
    std::vector<OpPtr> flattened;
};

}  // namespace

OpPtr flatten_groups(OpPtr op) {
    std::vector<TensorPtr> inputs = op->inputs();
    std::vector<TensorPtr> outputs = op->outputs();

    GroupFlattener flattener;
    (void)flattener.mutate(std::move(op));
    return make_op<OpGroup>(inputs, outputs, std::move(flattener.flattened));
}

}  // namespace hannk
