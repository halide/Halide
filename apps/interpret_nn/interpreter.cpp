#include "interpreter.h"
#include "app_util.h"

#include <cmath>
#include <list>

namespace interpret_nn {

namespace {

using ScheduledOpList = std::list<ScheduledOp>;
using ScheduledOpVector = std::vector<ScheduledOp>;

int Produces(const Op *op, const Tensor *t) {
    for (int i = 0; i < op->OutputCount(); i++) {
        if (op->Output(i) == t) {
            return i;
        }
    }
    return -1;
}

int Consumes(const Op *op, const Tensor *t) {
    for (int i = 0; i < op->InputCount(); i++) {
        if (op->Input(i) == t) {
            return i;
        }
    }
    return -1;
}

bool SubtractDone(Box& shape, const Tensor *t, const ScheduledOpVector &done) {
    bool trimmed = false;
    for (ScheduledOpVector::const_iterator i = done.begin(); i != done.end() && !is_empty(shape); i++) {
        int o = Produces(i->op, t);
        if (o >= 0) {
            Op::Bounds bounds = i->op->InferBounds(i->crop);
            const Box& produced = bounds.outputs[o];
            trimmed = trimmed || subtract(shape, produced);
        }
    }
    return trimmed;
}

bool CanExecute(const ScheduledOpVector &done, const ScheduledOp &op) {
    // Check if all of the producers needed by op are produced.
    Op::Bounds bounds = op.op->InferBounds(op.crop);
    // We need all of the input rectangles to be covered in the done list.
    for (int i = 0; i < op.op->InputCount(); i++) {
        const Tensor *input = op.op->Input(i);
        if (input->IsAllocated())
            continue;
        Box required = bounds.inputs[i];

        while (!is_empty(required)) {
            if (!SubtractDone(required, input, done))
                break;
        }

        if (!is_empty(required)) {
            // We needed more of this shape.
            return false;
        }
    }
    return true;
}

void GreedySchedule(ScheduledOpVector& done, ScheduledOpList& todo, ScheduledOpList::iterator op) {
    APP_CHECK(!todo.empty());
    if (done.empty() || done.back().op != op->op || !is_union_exact(done.back().crop, op->crop)) {
        done.emplace_back(std::move(*op));
    } else {
        // The last op and the current op are the same and can be merged.
        done.back().crop = Union(done.back().crop, op->crop);
    }
    todo.erase(op);

    const ScheduledOp &did = done.back();

    bool scheduled = false;
    // Try to execute all possible consumers first.
    for (int i = 0; i < did.op->OutputCount(); i++) {
        const Tensor *next = did.op->Output(i);

        // Try to schedule each output.
        ScheduledOpList exec;
        for (ScheduledOpList::iterator j = todo.begin(); j != todo.end();) {
            if (Consumes(j->op, next) >= 0 && CanExecute(done, *j)) {
                // This next op is executable. Move it to the front of the list.
                exec.emplace_back(std::move(*j));
                j = todo.erase(j);
                scheduled = true;
            } else {
                ++j;
            }
        }
        todo.insert(todo.begin(), exec.begin(), exec.end());
    }

    if (scheduled) {
        return;
    }

    // If failed, try to schedule producers.
    for (int i = 0; i < did.op->InputCount(); i++) {
        const Tensor *next = did.op->Input(i);

        // Try to schedule each input.
        ScheduledOpList exec;
        for (ScheduledOpList::iterator j = todo.begin(); j != todo.end();) {
            if (Produces(j->op, next) >= 0 && CanExecute(done, *j)) {
                // This next op is executable. Move it to the front of the list.
                exec.emplace_front(std::move(*j));
                j = todo.erase(j);
                scheduled = true;
            } else {
                ++j;
            }
        }
        todo.insert(todo.begin(), exec.begin(), exec.end());
    }

    if (scheduled) {
        return;
    }

    // If failed, try to schedule *one* sibling!
    for (ScheduledOpList::iterator i = todo.begin(); i != todo.end(); ++i) {
        if (i->op == did.op && CanExecute(done, *i)) {
            todo.emplace_front(std::move(*i));
            todo.erase(i);
            return;
        }
    }
}

}  // namespace

void ModelInterpreter::Schedule(ScheduleOptions options) {
    schedule_.clear();

    // First, generate a naive schedule that executes each op entirely before
    // moving on to the next.
    std::list<ScheduledOp> schedule;
    for (auto &i : model_.ops) {
        schedule.push_back({i.get(), i->GetFullCrop()});
    }

    if (options.verbose) {
        std::cout << "Before: " << std::endl;
        for (const auto& i : schedule) {
            if (i.crop.size() >= 3) {
                std::cout << i.crop[2].min << " " << i.crop[2].max << " ";
            }
            i.op->Dump(std::cout);
        }
    }

    if (options.target_working_set_size_bytes > 0) {
        for (std::list<ScheduledOp>::iterator i = schedule.begin(); i != schedule.end();) {
            // Split the op the way the op wants it done.
            std::vector<Box> splits = i->op->Split(i->crop);

            // Make a vector of scheduled ops.
            std::vector<ScheduledOp> split_ops;
            split_ops.reserve(splits.size() - 1);
            for (int j = 0; j + 1 < (int) splits.size(); j++) {
                split_ops.push_back({i->op, splits[j]});
            }

            // Replace i with the last split.
            i->crop = splits.back();

            // Insert the new ops before i.
            std::list<ScheduledOp>::iterator insert_at = i++;
            schedule.insert(insert_at, split_ops.begin(), split_ops.end());
        }
    }

    schedule_.reserve(schedule.size());
    while (!schedule.empty()) {
        auto i = schedule.begin();
        GreedySchedule(schedule_, schedule, i);
    }

    if (options.verbose) {
        std::cout << "After: " << std::endl;
        for (const auto& i : schedule_) {
            if (i.crop.size() >= 3) {
                std::cout << i.crop[2].min << " " << i.crop[2].max << " ";
            }
            i.op->Dump(std::cout);
        }
    }

    // Allocate the needed buffers for the tensors.
    // TODO: Identify the lifetimes and fold storage.
    // TODO: Maybe do this during execute to reduce idle memory?
    for (auto &i : model_.tensors) {
        i->Allocate();
    }
}

void ModelInterpreter::Execute() {
    for (ScheduledOp &i : schedule_) {
        i.op->Execute(i.crop);
    }
}

Tensor *ModelInterpreter::GetTensor(const std::string &name) {
    APP_CHECK(!model_.tensors.empty());

    if (tensor_names_.empty()) {
        size_t i = 0;
        for (const auto &t : model_.tensors) {
            tensor_names_[t->Name()] = i++;
        }
    }
    auto it = tensor_names_.find(name);
    if (it != tensor_names_.end()) {
        return model_.tensors.at(it->second).get();
    }
    return nullptr;
}

std::vector<Tensor *> ModelInterpreter::Inputs() {
    Op *first = schedule_.front().op;
    std::vector<Tensor *> result;
    for (int i = 0; i < first->InputCount(); i++) {
        result.emplace_back(first->Input(i));
    }
    return result;
}

std::vector<Tensor *> ModelInterpreter::Outputs() {
    Op *final = schedule_.back().op;
    std::vector<Tensor *> result;
    for (int i = 0; i < final->OutputCount(); i++) {
        result.emplace_back(final->Output(i));
    }
    return result;
}

}  // namespace interpret_nn
