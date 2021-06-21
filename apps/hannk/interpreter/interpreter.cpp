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

Interpreter::~Interpreter() {
}

namespace {

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

}  // namespace

void Interpreter::init(InterpreterOptions options) {
    pad_for_ops(model_.get());
    auto alias_groups = calculate_in_place_aliases(model_.get());
    fold_constants(model_.get());
    remove_dead_ops(model_.get());

    for (const auto &g : alias_groups) {
        // Allocate them all as uint types, for simplicity
        const size_t size_in_bytes = size_in_elements(g.dimensions) * g.element_size_in_bytes;
        aliased_tensor_storage_.emplace_back(new char[size_in_bytes]);
        void* shared_host = aliased_tensor_storage_.back().get();

        // HLOG(INFO) << "Aliasing a group of " << g.tensors.size() << " tensors, shared size " << size_in_bytes << " bytes...\n";

        for (const auto &it : g.tensors) {
            const TensorPtr &tensor = it.first;
            const TensorOffset &offset = it.second;
            HCHECK(!tensor->is_allocated());
            HCHECK(!tensor->is_dynamic());
            HCHECK(!tensor->is_external());
            const HalideBuffer<void> &old_buf = tensor->buffer();
            // We need to construct the new buf with the shared dimensions, then offset it,
            // to ensure that host is updated correctly.
            HalideBuffer<void> new_buf(old_buf.type(), shared_host, g.dimensions.size(), g.dimensions.data());
            for (int i = 0; i < new_buf.dimensions(); i++) {
                Interval dim_o(old_buf.dim(i).min(), old_buf.dim(i).max());
                if (i < (int)offset.size()) {
                    dim_o += offset[i];
                }
                new_buf.crop(i, dim_o.min, dim_o.extent());
                new_buf.translate(i, -dim_o.min);
            }
            tensor->set_external();
            tensor->set_external_buffer(std::move(new_buf));
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
