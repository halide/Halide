#include "interpreter/interpreter.h"
#include "interpreter/transforms.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

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
    halide_trace_event_t event = {
        0,
    };
    event.func = t->name().c_str();

    event.event = load ? halide_trace_consume : halide_trace_produce;
    event.parent_id = parent_id;
    event.parent_id = halide_trace(nullptr, &event);

    event.event = load ? halide_trace_load : halide_trace_store;

    box = intersect(box, t->box());
    HalideBuffer<const void> buf = t->buffer(box);

    event.type = buf.type();

    std::vector<int32_t> coords(box.size(), 0);
    event.coordinates = coords.data();
    event.dimensions = box.size();

    assert(event.type.bits <= 64);
    uint8_t value[8] = {
        0,
    };
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
    halide_trace_event_t trace = {
        0,
    };
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
        halide_type_t type = t->type();
        tag << 1 << " " << (int)type.code << " " << (int)type.bits << " " << (int)type.lanes;
        tag << " " << t->rank();
        const auto &b = t->buffer();
        for (int d = 0; d < b.dimensions(); d++) {
            const auto &dim = b.dim(d);
            tag << " " << dim.min() << " " << dim.extent();
        }
        std::string tag_str = tag.str();
        trace.trace_tag = tag_str.c_str();
        trace.func = t->name().c_str();
        trace.parent_id = parent_ids.back();
        halide_trace(nullptr, &trace);
        trace.trace_tag = nullptr;
    }
}

void trace_op(const Op *op, const Box &crop, std::vector<int32_t> &parent_ids) {
    halide_trace_event_t trace = {
        0,
    };
    trace.event = halide_trace_begin_realization;
    const Tensor *out = op->output();
    trace.func = out->name().c_str();
    trace.parent_id = parent_ids.back();
    std::vector<int32_t> coords(out->rank() * 2);
    for (int j = 0; j < (int)crop.size(); j++) {
        coords[j * 2 + 0] = crop[j].min;
        coords[j * 2 + 1] = crop[j].extent();
    }
    trace.coordinates = coords.data();
    trace.dimensions = coords.size();
    parent_ids.push_back(halide_trace(nullptr, &trace));

    for (int i = 0; i < op->input_count(); i++) {
        const Tensor *in = op->input(i);
        if (in->is_constant()) {
            continue;
        }
        BoundsMap deps = op->map_bounds(i, 0);
        Box input_bounds = deps.evaluate(crop);
        trace_loads(parent_ids.back(), in, input_bounds);
    }
    trace_stores(parent_ids.back(), out, crop);

    trace.event = halide_trace_end_realization;
    for (int i = op->output_count() - 1; i >= 0; i--) {
        trace.func = op->output(i)->name().c_str();
        trace.parent_id = parent_ids.back();
        parent_ids.pop_back();
        halide_trace(nullptr, &trace);
    }
}

void end_trace_execute(const Model &m, std::vector<int32_t> &parent_ids) {
    halide_trace_event_t trace = {
        0,
    };
    trace.func = "model";
    trace.event = halide_trace_end_pipeline;
    trace.parent_id = parent_ids.back();
    halide_trace(nullptr, &trace);
}

}  // namespace

ModelInterpreter::ModelInterpreter(Model m, InterpreterOptions options)
    : model_(std::move(m)), trace_(options.trace) {
    init(options);
}

ModelInterpreter::~ModelInterpreter() {
}

void ModelInterpreter::init(InterpreterOptions options) {
    pad_for_ops(&model_);
    in_place(&model_);
    fold_constants(&model_);
    remove_dead_ops(&model_);

    // TODO: Find a better schedule for executing the ops, including
    // better lifetime management for these allocations.
    for (auto &i : model_.tensors) {
        i->allocate();
    }
}

void ModelInterpreter::execute() {
    std::vector<int32_t> parent_ids;
    if (trace_) {
        begin_trace_execute(model_, parent_ids);
    }

    for (auto &i : model_.ops) {
        Box crop = i->output()->box();
        i->execute(crop);

        if (trace_) {
            trace_op(i.get(), crop, parent_ids);
        }
    }

    if (trace_) {
        end_trace_execute(model_, parent_ids);
    }
}

Tensor *ModelInterpreter::get_tensor(const std::string &name) {
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

}  // namespace hannk
