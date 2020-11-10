#include "interpreter.h"
#include "app_util.h"

#include <cmath>
#include <list>

namespace interpret_nn {

namespace {

const std::pair<int, int> FindEdge(const Op *from, const Op *to) {
    for (int i = 0; i < from->OutputCount(); i++) {
        const Tensor *output_i = from->Output(i);
        for (int j = 0; j < to->InputCount(); j++) {
            if (output_i == to->Input(j)) {
                return {i, j};
            }
        }
    }
    return {-1, -1};
}

int64_t TotalExtent(const CropShape &s) {
    int64_t result = 1;
    for (auto i : s) {
        result *= i.second;
    }
    return result;
}

float ShapeDistance(const CropShape &a, const CropShape &b) {
    APP_CHECK(a.size() == b.size());
    float size_cost = std::log(TotalExtent(a) + TotalExtent(b));
    float max_distance = 0.0f;
    for (int d = 0; d < (int)a.size(); d++) {
        // TODO: This could be more precise, and also maybe should consider strides.
        int a_center = a[d].first + a[d].second / 2;
        int b_center = b[d].first + b[d].second / 2;
        max_distance = std::max<float>(max_distance, std::abs(a_center - b_center));
    }

    return max_distance * size_cost;
}

bool IsIntersectionEmpty(std::pair<int, int> a, std::pair<int, int> b) {
    int max_a = a.first + a.second - 1;
    int max_b = b.first + b.second - 1;
    int min = std::max(a.first, b.first);
    int max = std::min(max_a, max_b);
    return max <= min;
}

bool IsIntersectionEmpty(const CropShape &a, const CropShape &b) {
    bool result = true;
    for (int i = 0; i < (int)a.size(); i++) {
        result = result && IsIntersectionEmpty(a[i], b[i]);
    }
    return result;
}

}  // namespace

bool ModelInterpreter::CanReorder(const ModelInterpreter::ScheduledOp &a,
                                  const ModelInterpreter::ScheduledOp &b) {
    int output_index, input_index;
    std::tie(output_index, input_index) = FindEdge(a.op, b.op);
    if (output_index < 0) {
        // The ops aren't connected.
        return true;
    }

    // The ops are connected, we need to make sure that the bounds of b don't
    // depend on a.
    Op::Bounds from_bounds = a.op->InferBounds(a.crop);
    Op::Bounds to_bounds = b.op->InferBounds(b.crop);

    const CropShape &from_shape = from_bounds.outputs[output_index];
    const CropShape &to_shape = to_bounds.inputs[input_index];
    return IsIntersectionEmpty(from_shape, to_shape);
}

float ModelInterpreter::Distance(const ModelInterpreter::ScheduledOp &from,
                                 const ModelInterpreter::ScheduledOp &to) {
    if (from.op == to.op) {
        //return ShapeDistance(from.crop, to.crop);
        return std::numeric_limits<float>::infinity();
    } else {
        int output_index, input_index;
        std::tie(output_index, input_index) = FindEdge(from.op, to.op);
        if (output_index < 0) {
            return std::numeric_limits<float>::infinity();
        }

        Op::Bounds from_bounds = from.op->InferBounds(from.crop);
        Op::Bounds to_bounds = to.op->InferBounds(to.crop);

        const CropShape &from_shape = from_bounds.outputs[output_index];
        const CropShape &to_shape = to_bounds.inputs[input_index];

        return ShapeDistance(from_shape, to_shape);
    }
}

void ModelInterpreter::Schedule(ScheduleOptions options) {
    schedule_.clear();

    // First, generate a naive schedule that executes each op entirely before
    // moving on to the next.
    std::list<ScheduledOp> schedule;
    for (auto &i : model_->ops) {
        schedule.push_back({i.get(), i->GetFullCrop()});
    }

    std::cout << "Before: " << std::endl;
    for (auto i : schedule) {
        std::cout << i.crop[2].first << " " << i.crop[2].second << " ";
        i.op->Dump(std::cout);
    }

#if 0
  for (std::list<ScheduledOp>::iterator i = schedule.begin();
       i != schedule.end();) {
    // Split the op the way the op wants it done.
    std::vector<CropShape> splits = i->op->Split(i->crop);

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
#endif

    schedule_.reserve(schedule.size());
    while (!schedule.empty()) {
        // Pick an op to start with.
        schedule_.emplace_back(std::move(schedule.front()));
        schedule.pop_front();

        // TODO: Actually try to schedule.
        continue;

        // Sort the remainder of the ops by the distance from the previously
        // scheduled op.
    resort:
        const ScheduledOp &previous = schedule_.back();
        schedule.sort([&](const ScheduledOp &a, const ScheduledOp &b) {
            // TODO: Tabulate the distances first to avoid recomputing them.
            return Distance(previous, a) < Distance(previous, b);
        });

        // Find the first op that is scheduleable after previous.
        for (std::list<ScheduledOp>::iterator i = schedule.begin();
             i != schedule.end(); i++) {
            if (std::all_of(schedule.begin(), i, [&](const ScheduledOp &op) {
                    return CanReorder(op, *i);
                })) {
                schedule_.emplace_back(std::move(*i));
                schedule.erase(i);
                goto resort;
            }
        }
    }

    std::cout << "After: " << std::endl;
    for (auto i : schedule_) {
        if (i.crop.size() >= 3) {
            std::cout << i.crop[2].first << " " << i.crop[2].second << " ";
        }
        i.op->Dump(std::cout);
    }
}

void ModelInterpreter::Execute() {
    for (ScheduledOp &i : schedule_) {
        i.op->Execute(i.crop);
    }
}

Tensor *ModelInterpreter::GetTensor(const std::string &name) {
    APP_CHECK(!model_->tensors.empty());

    if (tensor_names_.empty()) {
        size_t i = 0;
        for (const auto &t : model_->tensors) {
            tensor_names_[t->Name()] = i++;
        }
    }
    auto it = tensor_names_.find(name);
    if (it != tensor_names_.end()) {
        return model_->tensors.at(it->second).get();
    }
    return nullptr;
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
