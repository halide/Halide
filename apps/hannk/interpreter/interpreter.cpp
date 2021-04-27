#include "interpreter/interpreter.h"
#include "interpreter/transforms.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

Interpreter::Interpreter(std::unique_ptr<OpGroup> m, InterpreterOptions options)
    : model_(std::move(m)), trace_(options.trace) {
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

}  // namespace

void Interpreter::init(InterpreterOptions options) {
    pad_for_ops(model_.get());
    in_place(model_.get());
    fold_constants(model_.get());
    remove_dead_ops(model_.get());

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
