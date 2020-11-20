#include "interpreter.h"
#include "app_util.h"

#include <cmath>
#include <list>

namespace interpret_nn {

namespace {

// These functions emulate Halide tracing as if ModelInterpreter::execute were a Halide pipeline.
// This enables HalideTraceViz to be used to debug interpreter schedules.
void trace_loads_stores(HalideBuffer<const void> buf, halide_trace_event_t &event) {
    if (buf.dimensions() == 0) {
        memcpy(event.value, buf.data(), buf.type().bits / 8);
        halide_trace(nullptr, &event);
    } else {
        int min = buf.dim(buf.dimensions() - 1).min();
        int max = buf.dim(buf.dimensions() - 1).max();
        for (int i = min; i <= max; i++) {
            HalideBuffer<const void> buf_i = buf.sliced(buf.dimensions() - 1, i);
            event.coordinates[buf.dimensions() - 1] = i;
            trace_loads_stores(buf_i, event);
        }
    }
}

void trace_loads_stores(int32_t parent_id, const Tensor *t, Box box, bool load) {
    halide_trace_event_t event = {0,};
    event.func = t->name().c_str();

    event.event = load ? halide_trace_consume : halide_trace_produce;
    event.parent_id = parent_id;
    event.parent_id = halide_trace(nullptr, &event);

    event.event = load ? halide_trace_load : halide_trace_store;

    box = intersect(box, without_strides(t->shape()));
    HalideBuffer<const void> buf = t->data<void>(box);

    event.type = buf.type();

    std::vector<int32_t> coords(box.size(), 0);
    event.coordinates = coords.data();
    event.dimensions = box.size();

    assert(event.type.bits <= 64);
    uint8_t value[8] = {0,};
    event.value = &value[0];
    trace_loads_stores(buf, event);

    event.coordinates = 0;
    event.dimensions = 0;
    event.event = load ? halide_trace_end_consume : halide_trace_end_produce;
    halide_trace(nullptr, &event);
}

void trace_loads(int32_t parent_id, const Tensor *t, const Box &box) {
    return trace_loads_stores(parent_id, t, box, true);
}

void trace_stores(int32_t parent_id, const Tensor *t, const Box &box) {
    return trace_loads_stores(parent_id, t, box, false);
}

void begin_trace_execute(const Model &m, std::vector<int32_t> &parent_ids) {
    halide_trace_event_t trace = {0,};
    trace.func = "model";
    trace.event = halide_trace_begin_pipeline;
    parent_ids.push_back(halide_trace(nullptr, &trace));

    // Get a list of the tensors we should trace, in the order they should be traced.
    std::vector<const Tensor *> tensors;
    for (const auto &i : m.ops) {
        for (int j = 0; j < i->output_count(); j++) {
            if (std::find(tensors.begin(), tensors.end(), i->output(j)) == tensors.end()) {
                tensors.push_back(i->output(j));
            }
        }
    }

    // Add trace tags for each tensor we should trace.
    trace.event = halide_trace_tag;
    for (int i = 0; i < (int)tensors.size(); i++) {
        const Tensor *t = tensors[i];
        std::stringstream tag;
        tag << "func_type_and_dim: ";
        halide_type_t type = to_halide_type(t->type());
        tag << 1 << " " << (int)type.code << " " << (int)type.bits << " " << (int)type.lanes;
        tag << " " << t->rank();
        for (int d = 0; d < t->rank(); d++) {
            tag << " " << t->dim(d).min << " " << t->dim(d).extent;
        }
        std::string tag_str = tag.str();
        trace.trace_tag = tag_str.c_str();
        trace.func = t->name().c_str();
        trace.parent_id = parent_ids.back();
        halide_trace(nullptr, &trace);
        trace.trace_tag = nullptr;
    }
}

void trace_op(const ScheduledOp &op, std::vector<int32_t> &parent_ids) {
    Op::Bounds bounds = op.op->infer_bounds(op.crop);

    halide_trace_event_t trace = {0,};
    trace.event = halide_trace_begin_realization;
    for (int i = 0; i < op.op->output_count(); i++) {
        const Tensor *t = op.op->output(i);
        trace.func = t->name().c_str();
        trace.parent_id = parent_ids.back();
        const auto &shape = t->shape();
        std::vector<int32_t> coords(shape.size() * 2);
        for (int j = 0; j < (int)bounds.outputs[i].size(); j++) {
            coords[j * 2 + 0] = bounds.outputs[i][j].min;
            coords[j * 2 + 1] = bounds.outputs[i][j].extent();
        }
        trace.coordinates = coords.data();
        trace.dimensions = coords.size();
        parent_ids.push_back(halide_trace(nullptr, &trace));
    }

    for (int i = 0; i < op.op->input_count(); i++) {
        const Tensor *in = op.op->input(i);
        if (in->is_constant()) {
            continue;
        }
        trace_loads(parent_ids.back(), in, bounds.inputs[i]);
    }
    for (int i = 0; i < op.op->output_count(); i++) {
        const Tensor *out = op.op->output(i);
        trace_stores(parent_ids.back(), out, bounds.outputs[i]);
    }

    trace.event = halide_trace_end_realization;
    for (int i = op.op->output_count() - 1; i >= 0; i--) {
        trace.func = op.op->output(i)->name().c_str();
        trace.parent_id = parent_ids.back();
        parent_ids.pop_back();
        halide_trace(nullptr, &trace);
    }
}

void end_trace_execute(const Model &m, std::vector<int32_t> &parent_ids) {
    halide_trace_event_t trace = {0,};
    trace.func = "model";
    trace.event = halide_trace_end_pipeline;
    trace.parent_id = parent_ids.back();
    halide_trace(nullptr, &trace);
}

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
        if (input->is_constant())
            continue;
        Box required = bounds.inputs[i];
        Box remaining = subtract_done(required, input, done);
        if (!is_empty(remaining)) {
            return false;
        }
    }
    return true;
}

// Schedule ops 'greedily' assuming 'from' was just executed. Execute up to 'parallelism' ops
// using the following priority:
// - Schedule possible consumers
// - Schedule possible producers
// - Schedule possible siblings
// TODO: This algorithm is horrifically unoptimized. It calls infer_bounds repeatedly
// on the same op/crop, and it iterates over all ops repeatedly. It can both be
// optimized significantly by caching results of operations like this, and by
// maybe restructuring things (e.g. don't split ops into slices all up front, do it
// progressively instead.)
void greedy_schedule(ScheduledOpVector& done, ScheduledOpList& todo, const ScheduledOp &from, int parallelism) {
    ScheduledOpList exec;

    // Try to execute all possible consumers first.
    for (int i = 0; i < from.op->output_count(); i++) {
        const Tensor *next = from.op->output(i);

        // Try to schedule each output.
        for (ScheduledOpList::iterator j = todo.begin(); j != todo.end() && (int)exec.size() < parallelism;) {
            if (index_of_input(j->op, next) >= 0 && can_execute(done, *j)) {
                // This next op is executable. Move it to the front of the list.
                exec.emplace_back(std::move(*j));
                j = todo.erase(j);
            } else {
                ++j;
            }
        }
    }

    // If failed, try to schedule producers.
    for (int i = 0; i < from.op->input_count(); i++) {
        const Tensor *next = from.op->input(i);

        // Try to schedule each input.
        for (ScheduledOpList::iterator j = todo.begin(); j != todo.end() && (int)exec.size() < parallelism;) {
            if (index_of_output(j->op, next) >= 0 && can_execute(done, *j)) {
                // This next op is executable. Move it to the front of the list.
                exec.emplace_back(std::move(*j));
                j = todo.erase(j);
            } else {
                ++j;
            }
        }
    }

    // If failed, try to schedule some siblings.
    for (ScheduledOpList::iterator i = todo.begin(); i != todo.end() && (int)exec.size() < parallelism;) {
        if (i->op == from.op && can_execute(done, *i)) {
            exec.emplace_back(std::move(*i));
            i = todo.erase(i);
        } else {
            ++i;
        }
    }

    // Do these in separate loops, so we maintain the parallelism between ops.
    for (auto &i : exec) {
        done.emplace_back(i);
    }
    for (auto &i : exec) {
        greedy_schedule(done, todo, i, parallelism);
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
        schedule_.emplace_back(std::move(schedule.front()));
        schedule.pop_front();
        greedy_schedule(schedule_, schedule, schedule_.back(), options.parallelism);
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

    // Allocate the needed buffers for the tensors.
    // TODO: Identify the lifetimes and fold storage.
    // TODO: Maybe do this during execute to reduce idle memory?
    // Maybe we should have an allocate/free "op" that we can insert
    // in the schedule to manage lifetime more precisely.
    for (auto &i : model_.tensors) {
        i->allocate();
    }
}

void ModelInterpreter::execute() {
    std::vector<int32_t> parent_ids;
    if (trace_) {
        begin_trace_execute(model_, parent_ids);
    }

    for (ScheduledOp &i : schedule_) {
        i.op->execute(i.crop);

        if (trace_) {
            trace_op(i, parent_ids);
        }
    }

    if (trace_) {
        end_trace_execute(model_, parent_ids);
    }
}

Tensor *ModelInterpreter::get_tensor(const std::string &name) {
    APP_CHECK(!model_.tensors.empty());

    for (const auto &t : model_.tensors) {
        if (t->name() == name) {
            return t.get();
        }
    }
    return nullptr;
}

std::vector<Tensor *> ModelInterpreter::inputs() {
    std::vector<Tensor *> result;
    for (auto &i : model_.tensors) {
        if (i->is_input()) {
            result.push_back(i.get());
        }
    }

    return result;
}

std::vector<Tensor *> ModelInterpreter::outputs() {
    std::vector<Tensor *> result;
    for (auto &i : model_.tensors) {
        if (i->is_output()) {
            result.push_back(i.get());
        }
    }

    return result;
}

}  // namespace interpret_nn
