#include "interpret_nn.h"
#include "halide_app_assert.h"

#include <cmath>
#include <list>

namespace interpret_nn {

size_t SizeOfTensorType(TensorType t) {
    switch (t) {
    case TensorType::Float32:
        return 4;
    case TensorType::Float16:
        return 2;
    case TensorType::Int32:
        return 4;
    case TensorType::UInt8:
        return 1;
    case TensorType::Int64:
        return 8;
    case TensorType::Int16:
        return 2;
    case TensorType::Complex64:
        return 16;
    case TensorType::Int8:
        return 1;
    case TensorType::Float64:
        return 8;
    case TensorType::Complex128:
        return 32;
    // case TensorType::String:  fallthru
    // case TensorType::Bool:    fallthru
    default:
        halide_app_error << "Unknown size of type";
        return 0;
    }
}

const char *TensorTypeToString(TensorType t) {
    switch (t) {
    case TensorType::Float32:
        return "float32";
    case TensorType::Float16:
        return "float16";
    case TensorType::Int32:
        return "int32";
    case TensorType::UInt8:
        return "uint8";
    case TensorType::Int64:
        return "int64";
    case TensorType::Int16:
        return "int16";
    case TensorType::Complex64:
        return "complex64";
    case TensorType::Int8:
        return "int8";
    case TensorType::Float64:
        return "float64";
    case TensorType::Complex128:
        return "complex128";
    case TensorType::String:
        return "string";
    case TensorType::Bool:
        return "bool";
    default:
        halide_app_error << "Unhandled interpret_nn::TensorType";
        return "";
    }
}

void Model::Dump(std::ostream& os) {
  os << "Tensors: " << std::endl;
  for (const auto& i : tensors) {
    os << "  " << TensorTypeToString(i->Type()) << " x " << i->Shape()
       << (i->is_allocated() ? " allocated " : " ") << i->Name() << std::endl;
  }

  os << "Ops: " << std::endl;
  for (const auto& i : ops) {
    i->Dump(os);
  }
  os << std::endl;
}

void Tensor::allocate() {
  size_t shape_size = 1;
  for (halide_dimension_t& i : shape_) {
    i.stride = shape_size;
    shape_size *= i.extent;
  }
  shape_size *= SizeOfTensorType(Type());
  if (data_.empty()) {
    data_.resize(shape_size);
  } else {
    halide_app_assert(data_.size() == shape_size);
  }
}

namespace {

const std::pair<int, int> FindEdge(const Op* from, const Op* to) {
  for (int i = 0; i < from->OutputCount(); i++) {
    const Tensor* output_i = from->Output(i);
    for (int j = 0; j < to->InputCount(); j++) {
      if (output_i == to->Input(j)) {
        return {i, j};
      }
    }
  }
  return {-1, -1};
}

int64_t TotalExtent(const CropShape& s) {
  int64_t result = 1;
  for (auto i : s) {
    result *= i.second;
  }
  return result;
}

float ShapeDistance(const CropShape& a, const CropShape& b) {
  halide_app_assert(a.size() == b.size());
  float size_cost = std::log(TotalExtent(a) + TotalExtent(b));
  float max_distance = 0.0f;
  for (int d = 0; d < a.size(); d++) {
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

bool IsIntersectionEmpty(const CropShape& a, const CropShape& b) {
  bool result = true;
  for (int i = 0; i < a.size(); i++) {
    result = result && IsIntersectionEmpty(a[i], b[i]);
  }
  return result;
}

}  // namespace

bool ModelInterpreter::CanReorder(const ModelInterpreter::ScheduledOp& a,
                                  const ModelInterpreter::ScheduledOp& b) {
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

  const CropShape& from_shape = from_bounds.outputs[output_index];
  const CropShape& to_shape = to_bounds.inputs[input_index];
  return IsIntersectionEmpty(from_shape, to_shape);
}

float ModelInterpreter::Distance(const ModelInterpreter::ScheduledOp& from,
                                 const ModelInterpreter::ScheduledOp& to) {
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

    const CropShape& from_shape = from_bounds.outputs[output_index];
    const CropShape& to_shape = to_bounds.inputs[input_index];

    return ShapeDistance(from_shape, to_shape);
  }
}

void ModelInterpreter::Schedule(ScheduleOptions options) {
  schedule_.clear();

  // First, generate a naive schedule that executes each op entirely before
  // moving on to the next.
  std::list<ScheduledOp> schedule;
  for (auto& i : model_->ops) {
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
    for (int j = 0; j + 1 < splits.size(); j++) {
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
    const ScheduledOp& previous = schedule_.back();
    schedule.sort([&](const ScheduledOp& a, const ScheduledOp& b) {
      // TODO: Tabulate the distances first to avoid recomputing them.
      return Distance(previous, a) < Distance(previous, b);
    });

    // Find the first op that is scheduleable after previous.
    for (std::list<ScheduledOp>::iterator i = schedule.begin();
         i != schedule.end(); i++) {
      if (std::all_of(schedule.begin(), i, [&](const ScheduledOp& op) {
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
    std::cout << i.crop[2].first << " " << i.crop[2].second << " ";
    i.op->Dump(std::cout);
  }
}

void ModelInterpreter::Execute() {
  for (ScheduledOp& i : schedule_) {
    i.op->Execute(i.crop);
  }
}

}  // namespace interpret_nn
