#include "interpreter/transforms.h"

namespace hannk {

namespace {

bool is_input(const Op *op, const Tensor *t) {
    for (int i = 0; i < op->input_count(); ++i) {
        if (op->input(i) == t) {
            return true;
        }
    }
    return false;
}

bool is_output(const Op *op, const Tensor *t) {
    for (int i = 0; i < op->output_count(); ++i) {
        if (op->output(i) == t) {
            return true;
        }
    }
    return false;
}

bool is_used(const Op *op, const Tensor *t) {
    return is_input(op, t) || is_output(op, t);
}

}  // namespace

void remove_dead_ops(Model *m) {
    // Find ops with outputs that are unused.
    // Go in reverse order so removing a dead op
    // enables earlier ops to be seen as dead.
    for (int i = (int)m->ops.size() - 1; i >= 0; --i) {
        Op *op = m->ops[i].get();
        bool dead = true;
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

    // Remove tensors not used by any op.
    for (int i = 0; i < (int)m->tensors.size();) {
        bool dead = true;
        for (int j = 0; j < (int)m->ops.size(); ++j) {
            if (is_used(m->ops[j].get(), m->tensors[i].get())) {
                dead = false;
                break;
            }
        }

        if (dead) {
            m->tensors.erase(m->tensors.begin() + i);
        } else {
            ++i;
        }
    }
}

namespace {

// We can alias two tensors if the input is not used after the output is written.
void maybe_alias_tensors(Model *m, Tensor *input, Tensor *output) {
    if (input->rank() != output->rank()) {
        // TODO: We should be able to alias reshapes.
        return;
    }

    // We can't change the shape of an input or output tensor.
    if ((input->is_input() || input->is_output()) &&
        !is_subset_of(output->box(), input->box())) {
        return;
    }
    if ((output->is_input() || output->is_output()) &&
        !is_subset_of(input->box(), output->box())) {
        return;
    }

    bool output_written = false;
    for (const auto &i : m->ops) {
        if (is_output(i.get(), input)) {
            if (output_written) {
                // We've already written the output, so we can't alias these tensors.
                return;
            }
        }
        if (is_output(i.get(), output)) {
            output_written = true;
        }
    }

    input->set_alias_of(output);
}

// Try to alias outputs to inputs when it is safe.
class InPlace : public OpVisitor {
    Model *model_;

    using OpVisitor::visit;

    void visit(AddOp *op) {
        maybe_alias_tensors(model_, op->input(0), op->output());
        maybe_alias_tensors(model_, op->input(1), op->output());
    }

    void visit(PadOp *op) {
        maybe_alias_tensors(model_, op->input(), op->output());
    }

    void visit(ReshapeOp *op) {
        maybe_alias_tensors(model_, op->input(), op->output());
    }

public:
    InPlace(Model *m)
        : model_(m) {
    }
};

}  // namespace

void in_place(Model *m) {
    InPlace v(m);
    m->accept(&v);
}

namespace {

// Find ops that need padding and add an explicit pad op.
class PadForConv : public OpVisitor {
    void pad_for_op(Op *op, const Box &required, int in = 0) {
        Tensor *input = op->input(in);

        if (!is_subset_of(required, input->box())) {
            // Make a PadOp and a new tensor for the padded result.
            std::unique_ptr<Tensor> padded =
                ::hannk::make_unique<Tensor>(input->name() + "_padded", input->type(), required, input->quantization());
            op->set_input(padded.get());

            // Add the new tensor, op, and update the input.
            std::unique_ptr<Op> pad = ::hannk::make_unique<PadOp>(input, nullptr, padded.get());
            new_ops.emplace_back(std::move(pad), std::move(padded));
        }
    }

    void visit(Conv2DOp *op) {
        Box required = op->input_required(op->output()->box());
        assert(required[0].min == 0);
        // TODO: Figure out how to get all this logic into one place. We really need
        // to figure out how to get the unrolled reduction cases for conv working with
        // a GuardWithIf so we only ever require the same unroll factor of 4.
        if (required[0].extent() >= 16) {
            required[0].set_extent((required[0].extent() + 15) & ~15);
        } else {
            required[0].set_extent((required[0].extent() + 3) & ~3);
        }
        pad_for_op(op, required, 0);

        // We also need to tile the filter.
        Tensor *filter = op->filter();
        if (op->filter()->rank() == 4) {
            Box tiled_shape = op->filter_required();

            halide_type_t type = op->filter_type();
            QuantizationInfo quantization = filter->quantization();
            if (type.bits > filter->type().bits) {
                // We're widening the filter. Subtract the offset.
                std::fill(quantization.zero.begin(), quantization.zero.end(), 0);
            }
            std::unique_ptr<Tensor> tiled =
                ::hannk::make_unique<Tensor>(filter->name() + "_tiled", type, tiled_shape, quantization);
            // Maybe more than one op uses this same filter...?
            filter->replace_all_consumers_with(tiled.get());

            std::unique_ptr<Op> tile = ::hannk::make_unique<TileConvFilterOp>(filter, tiled.get());
            new_ops.emplace_back(std::move(tile), std::move(tiled));
        }
    }

    void visit(DepthwiseConv2DOp *op) {
        Box required = op->input_required(op->output()->box());
        pad_for_op(op, required, 0);
    }

public:
    std::vector<std::pair<std::unique_ptr<Op>, std::unique_ptr<Tensor>>> new_ops;
};

}  // namespace

void pad_for_conv(Model *m) {
    PadForConv v;
    m->accept(&v);
    for (auto &i : v.new_ops) {
        m->insert(std::move(i.first));
        m->insert(std::move(i.second));
    }
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

            // Run the op.
            i->execute(i->get_full_crop());

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
