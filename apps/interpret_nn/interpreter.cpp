#include "interpreter.h"
#include "app_util.h"

#include <cmath>
#include <list>

namespace interpret_nn {

namespace {

using ScheduledOpList = std::list<ScheduledOp>;
using ScheduledOpVector = std::vector<ScheduledOp>;

int index_of_output(const Op *op, const Tensor *t) {
    for (int i = 0; i < op->output_count(); i++) {
        if (op->output(i) == t) {
            return i;
        }
    }
    return -1;
}

int index_of_input(const Op *op, const Tensor *t) {
    for (int i = 0; i < op->input_count(); i++) {
        if (op->input(i) == t) {
            return i;
        }
    }
    return -1;
}

// Subtract the computed parts of t from shape.
Box subtract_done(Box shape, const Tensor *t, const ScheduledOpVector &done) {
    // Subtraction can fail if the result is not a single box. But, another
    // subtraction later could change that. So we iterate, until no done ops
    // succeed in any subtraction.
    while (!is_empty(shape)) {
        bool trimmed = false;
        for (ScheduledOpVector::const_iterator i = done.begin(); i != done.end() && !is_empty(shape); i++) {
            int o = index_of_output(i->op, t);
            if (o >= 0) {
                Op::Bounds bounds = i->op->infer_bounds(i->crop);
                const Box& produced = bounds.outputs[o];
                trimmed = trimmed || subtract(shape, produced);
            }
        }
        if (!trimmed) {
            // We didn't do anything to the shape, trying again won't help.
            break;
        }
    }
    return shape;
}

// Returns true if op can be executed (all of its producers are done).
bool can_execute(const ScheduledOpVector &done, const ScheduledOp &op) {
    // Check if all of the producers needed by op are produced.
    Op::Bounds bounds = op.op->infer_bounds(op.crop);
    // We need all of the input rectangles to be covered in the done list.
    for (int i = 0; i < op.op->input_count(); i++) {
        const Tensor *input = op.op->input(i);
        if (input->is_allocated())
            continue;
        Box required = bounds.inputs[i];
        Box remaining = subtract_done(required, input, done);
        if (!is_empty(remaining)) {
            // We needed more of this shape.
            return false;
        }
    }
    return true;
}

// Schedule op 'greedily':
// - Moving it to the back of the done vector and remove it from todo.
// - Schedule all possible consumers
// - If not possible, schedule all possible producers.
// - If not possible, schedule one sibling.
// TODO: This algorithm is horrifically unoptimized. It calls infer_bounds repeatedly
// on the same op/crop, and it iterates over all ops repeatedly. It can both be
// optimized significantly by caching results of operations like this, and by
// changing the overall structure.
void greedy_schedule(ScheduledOpVector& done, ScheduledOpList& todo, ScheduledOpList::iterator op) {
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
    for (int i = 0; i < did.op->output_count(); i++) {
        const Tensor *next = did.op->output(i);

        // Try to schedule each output.
        ScheduledOpList exec;
        for (ScheduledOpList::iterator j = todo.begin(); j != todo.end();) {
            if (index_of_input(j->op, next) >= 0 && can_execute(done, *j)) {
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
    for (int i = 0; i < did.op->input_count(); i++) {
        const Tensor *next = did.op->input(i);

        // Try to schedule each input.
        ScheduledOpList exec;
        for (ScheduledOpList::iterator j = todo.begin(); j != todo.end();) {
            if (index_of_output(j->op, next) >= 0 && can_execute(done, *j)) {
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

    // If failed, try to schedule *one* sibling.
    for (ScheduledOpList::iterator i = todo.begin(); i != todo.end(); ++i) {
        if (i->op == did.op && can_execute(done, *i)) {
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
        schedule.push_back({i.get(), i->get_full_crop()});
    }

    if (options.verbose) {
        std::cout << "Before: " << std::endl;
        for (const auto& i : schedule) {
            if (i.crop.size() >= 3) {
                std::cout << i.crop[2].min << " " << i.crop[2].max << " ";
            }
            i.op->dump(std::cout);
        }
    }

    if (options.target_working_set_size_bytes > 0) {
        for (std::list<ScheduledOp>::iterator i = schedule.begin(); i != schedule.end();) {
            // Split the op the way the op wants it done.
            std::vector<Box> splits = i->op->split(i->crop);

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
        greedy_schedule(schedule_, schedule, i);
    }

    if (options.verbose) {
        std::cout << "After: " << std::endl;
        for (const auto& i : schedule_) {
            if (i.crop.size() >= 3) {
                std::cout << i.crop[2].min << " " << i.crop[2].max << " ";
            }
            i.op->dump(std::cout);
        }
    }

    // allocate the needed buffers for the tensors.
    // TODO: Identify the lifetimes and fold storage.
    // TODO: Maybe do this during execute to reduce idle memory?
    // Maybe we should have an allocate/free "op" that we can insert
    // in the schedule to manage lifetime more precisely.
    for (auto &i : model_.tensors) {
        i->allocate();
    }
}

void ModelInterpreter::execute() {
    for (ScheduledOp &i : schedule_) {
        i.op->execute(i.crop);
    }
}

Tensor *ModelInterpreter::GetTensor(const std::string &name) {
    APP_CHECK(!model_.tensors.empty());

    if (tensor_names_.empty()) {
        size_t i = 0;
        for (const auto &t : model_.tensors) {
            tensor_names_[t->name()] = i++;
        }
    }
    auto it = tensor_names_.find(name);
    if (it != tensor_names_.end()) {
        return model_.tensors.at(it->second).get();
    }
    return nullptr;
}

std::vector<Tensor *> ModelInterpreter::inputs() {
    // TODO: This is wrong, it needs to find all tensors that are only
    // consumed and not produced, and are not constant.
    Op *first = schedule_.front().op;
    std::vector<Tensor *> result;
    for (int i = 0; i < first->input_count(); i++) {
        result.emplace_back(first->input(i));
    }
    return result;
}

std::vector<Tensor *> ModelInterpreter::outputs() {
    // TODO: This is wrong, it needs to find all tensors that are only
    // produced and not consumed.
    Op *final = schedule_.back().op;
    std::vector<Tensor *> result;
    for (int i = 0; i < final->output_count(); i++) {
        result.emplace_back(final->output(i));
    }
    return result;
}

}  // namespace interpret_nn
