#include "interpreter/interpreter.h"
#include "interpreter/transforms.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

Interpreter::Interpreter(std::unique_ptr<OpGroup> m, InterpreterOptions options)
    : model_(std::move(m)) {
    init(options);
}

namespace {

size_t size_in_elements(const TensorDimensions &dimensions) {
    ptrdiff_t begin_offset = 0;
    ptrdiff_t end_offset = 0;
    for (int i = 0; i < (int)dimensions.size(); i++) {
        const int stride = dimensions[i].stride;
        if (stride < 0) {
            begin_offset += stride * (ptrdiff_t)(dimensions[i].extent - 1);
        } else /* stride >= 0 */ {
            end_offset += stride * (ptrdiff_t)(dimensions[i].extent - 1);
        }
    }
    end_offset += 1;
    assert(end_offset >= begin_offset);
    return (size_t)(end_offset - begin_offset);
}

class AllocateAll : public OpVisitor {
    void visit(OpGroup *g) {
        for (int i = 0; i < g->op_count(); i++) {
            Op *op = g->op(i);
            for (int j = 0; j < op->input_count(); j++) {
                op->input(j)->allocate();
            }
            for (int j = 0; j < op->output_count(); j++) {
                op->output(j)->allocate();
            }
            op->accept(this);
        }
    }
};

}  // namespace

void Interpreter::init(InterpreterOptions options) {
    pad_for_ops(model_.get());
    auto alias_groups = calculate_in_place_aliases(model_.get());
    fold_constants(model_.get());
    remove_dead_ops(model_.get());

    for (const auto &g : alias_groups) {
        // Allocate them all uint types, for simplicity
        halide_type_t type(halide_type_uint, g.element_size_in_bytes * 8);
        external_buffers_.emplace_back(type, nullptr, g.dimensions.size(), g.dimensions.data());
        external_buffers_.back().allocate();

        for (const auto &it : g.tensors) {
            HCHECK(!it.tensor->is_allocated());
            HCHECK(!it.tensor->is_dynamic());
            HCHECK(!it.tensor->is_external());
            HalideBuffer<void> old_buf = it.tensor->buffer();
            HalideBuffer<void> new_buf = external_buffers_.back();
            // Dance directly on the runtime type to make sure it matches.
            // (This is temporary.)
            new_buf.raw_buffer()->type = old_buf.raw_buffer()->type;

            for (int i = 0; i < new_buf.dimensions(); i++) {
                Interval dim_o(old_buf.dim(i).min(), old_buf.dim(i).max());
                if (i < (int)it.offset.size()) {
                    dim_o += it.offset[i];
                }
                new_buf.crop(i, dim_o.min, dim_o.extent());
                new_buf.translate(i, -dim_o.min);
            }
            it.tensor->set_external();
            it.tensor->set_external_buffer(new_buf);
        }
    }

    // TODO: Find a better schedule for executing the ops, including
    // better lifetime management for these allocations.
    AllocateAll allocate_all;

    model_->accept(&allocate_all);
}

void Interpreter::execute() {
    model_->execute();
}

TensorPtr Interpreter::get_tensor(const std::string &name) {
    for (int i = 0; i < model_->op_count(); i++) {
        Op *op = model_->op(i);
        for (int j = 0; j < op->input_count(); j++) {
            if (op->input(j)->name() == name) {
                return op->input(j);
            }
        }
        for (int j = 0; j < op->output_count(); j++) {
            if (op->output(j)->name() == name) {
                return op->output(j);
            }
        }
    }
    return nullptr;
}

std::vector<TensorPtr> Interpreter::inputs() {
    std::vector<TensorPtr> result;
    for (int i = 0; i < model_->input_count(); i++) {
        result.push_back(model_->input(i));
    }

    return result;
}

std::vector<TensorPtr> Interpreter::outputs() {
    std::vector<TensorPtr> result;
    for (int i = 0; i < model_->output_count(); i++) {
        result.push_back(model_->output(i));
    }

    return result;
}

}  // namespace hannk
