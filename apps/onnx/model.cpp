#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <sys/time.h>
#include <random>
#include <unordered_set>
#include "common_types.h"
#include "onnx_converter.h"

namespace py = pybind11;

static HalideModel ConvertOnnxModel(
    std::string onnx_model_str,
    std::string device) {
  onnx::ModelProto onnx_model;
  onnx_model.ParseFromString(onnx_model_str);

  if (onnx_model.graph().output_size() == 0) {
    throw std::invalid_argument("No output specified in the model");
  }

  std::unordered_set<std::string> dflt_values;
  for (const auto& dflt : onnx_model.graph().initializer()) {
    dflt_values.insert(dflt.name());
  }
  HalideModel result;
  for (const auto& input : onnx_model.graph().input()) {
    if (dflt_values.find(input.name()) != dflt_values.end()) {
      continue;
    }
    result.input_names.push_back(input.name());
    result.input_types[input.name()] = input.type().tensor_type().elem_type();
  }
  for (const auto& output : onnx_model.graph().output()) {
    result.output_names.push_back(output.name());
    result.output_types.push_back(output.type().tensor_type().elem_type());
  }

  result.model.reset(new Model(ConvertModel(onnx_model, device)));

  std::vector<Halide::Func> funcs;
  for (const auto& output : onnx_model.graph().output()) {
    const auto& tensor = result.model->outputs.at(output.name());
    funcs.push_back(tensor.rep);
  }
  result.rep.reset(new Halide::Pipeline(funcs));
  return result;
}

static std::string AutoSchedule(HalideModel pipeline) {
  // Generate a schedule for the pipeline.
  Halide::Target tgt = Halide::get_host_target();
  std::string schedule = pipeline.rep->auto_schedule(tgt);
  return schedule;
}

template <typename T>
struct Distribution {
  typedef typename std::conditional<
      std::is_floating_point<T>::value,
      std::uniform_real_distribution<T>,
      std::uniform_int_distribution<T>>::type Type;
};
template <>
struct Distribution<bool> {
  typedef typename std::uniform_int_distribution<uint8_t> Type;
};

template <typename HalideBufferType, typename T, bool Random>
static void PrepareImageParam(
    Halide::ImageParam& image_param,
    const std::vector<int>& shape,
    const py::array_t<T>* np_data) {
  if (Random) {
    Halide::Buffer<HalideBufferType> values(shape);

    typename Distribution<HalideBufferType>::Type distrib;
    std::mt19937 generator;

    values.for_each_value(
        [&](HalideBufferType& val) { val = distrib(generator); });
    image_param.set(values);
  } else {
    int stride = 1;
    std::vector<int> np_strides;
    for (int i = 0; i < shape.size(); ++i) {
      np_strides.push_back(stride);
      stride *= shape[shape.size() - (i + 1)];
    }
    std::reverse(np_strides.begin(), np_strides.end());

    const T* raw_data = np_data->data();
    Halide::Buffer<HalideBufferType> values(shape);
    values.for_each_element([&](const int* halide_coords) {
      int np_index = 0;
      for (int i = 0; i < shape.size(); i++) {
        np_index += halide_coords[i] * np_strides[i];
      }
      values(halide_coords) = raw_data[np_index];
    });
    image_param.set(values);
  }
}

template <typename T, bool Random = false>
static void PrepareInput(
    const HalideModel& pipeline,
    const std::string& input_name,
    const std::vector<int>& input_shape,
    const py::array_t<T>& input_array) {
  Halide::ImageParam& input = pipeline.model->inputs.at(input_name);
  const int input_type = pipeline.input_types.at(input_name);
  switch (input_type) {
    case onnx::TensorProto::BOOL:
      PrepareImageParam<bool, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::INT8:
      PrepareImageParam<int8_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::INT16:
      PrepareImageParam<int16_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::INT32:
      PrepareImageParam<int32_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::INT64:
      PrepareImageParam<int64_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::UINT8:
      PrepareImageParam<uint8_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::UINT16:
      PrepareImageParam<uint8_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::UINT32:
      PrepareImageParam<uint32_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::UINT64:
      PrepareImageParam<uint64_t, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::FLOAT:
      PrepareImageParam<float, T, Random>(input, input_shape, &input_array);
      break;
    case onnx::TensorProto::DOUBLE:
      PrepareImageParam<double, T, Random>(input, input_shape, &input_array);
      break;
    default:
      throw std::domain_error("Unsupported input type");
  }
}

template <typename T>
static void PrepareInput(
    const HalideModel& pipeline,
    const std::string& input_name,
    const std::vector<int>& input_shape) {
  py::array_t<T> rand_array;
  PrepareInput<T, true>(pipeline, input_name, input_shape, rand_array);
}

static void PreparePyArrayInput(
    const HalideModel& pipeline,
    const py::array& ndarray,
    const std::string& input_name) {
  const int rank = ndarray.ndim();
  std::vector<int> input_shape;
  for (int i = 0; i < rank; ++i) {
    input_shape.push_back(ndarray.shape(i));
  }

  if (ndarray.dtype().is(py::dtype::of<bool>())) {
    PrepareInput<bool>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<int8_t>())) {
    PrepareInput<int8_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<int16_t>())) {
    PrepareInput<int16_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<int32_t>())) {
    PrepareInput<int32_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<int64_t>())) {
    PrepareInput<int64_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<uint8_t>())) {
    PrepareInput<uint8_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<uint64_t>())) {
    PrepareInput<uint16_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<uint32_t>())) {
    PrepareInput<uint32_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<uint64_t>())) {
    PrepareInput<uint64_t>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<float>())) {
    PrepareInput<float>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().is(py::dtype::of<double>())) {
    PrepareInput<double>(pipeline, input_name, input_shape, ndarray);
  } else if (ndarray.dtype().kind() == 'i') {
    // TODO : Figure out why static type casting doesn't work for singed intger!
    PrepareInput<int>(pipeline, input_name, input_shape, ndarray);
  } else {
    throw std::invalid_argument(
        std::string("Unsupported type ") + ndarray.dtype().kind() +
        " for input " + input_name);
  }
}

static void PrepareRandomInput(
    const HalideModel& pipeline,
    const std::string& input_name) {
  const Tensor& t = pipeline.model->tensors.at(input_name);
  std::vector<int> input_shape;
  for (int i = 0; i < t.shape.size(); ++i) {
    const int64_t* dim = Halide::Internal::as_const_int(t.shape[i]);
    if (!dim) {
      throw std::invalid_argument(
          "Unknown dim " + std::to_string(i) + " for input " + input_name);
    }
    input_shape.push_back(*dim);
  }

  switch (t.type) {
    case onnx::TensorProto::BOOL: {
      PrepareInput<bool>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::INT8: {
      PrepareInput<int8_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::INT16: {
      PrepareInput<int16_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::INT32: {
      PrepareInput<int32_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::INT64: {
      PrepareInput<int64_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::UINT8: {
      PrepareInput<uint8_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::UINT16: {
      PrepareInput<uint16_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::UINT32: {
      PrepareInput<uint32_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::UINT64: {
      PrepareInput<uint64_t>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::FLOAT: {
      PrepareInput<float>(pipeline, input_name, input_shape);
      break;
    }
    case onnx::TensorProto::DOUBLE: {
      PrepareInput<double>(pipeline, input_name, input_shape);
      break;
    }
    default: {
      throw std::invalid_argument(
          std::string("Unsupported type for input ") + input_name);
    }
  }
}

template <typename T>
py::array_t<T> ExportOutput(
    const Halide::Buffer<T>& output_values,
    const HalideModel& pipeline,
    int output_id) {
  Tensor node = pipeline.model->outputs.at(pipeline.output_names.at(output_id));
  std::vector<int> output_shape;
  int stride = 1;
  const int rank = node.shape.size();
  std::vector<int> np_strides;
  for (int i = 0; i < rank; ++i) {
    const int64_t* dim = Halide::Internal::as_const_int(node.shape[i]);
    if (!dim) {
      throw std::invalid_argument(
          "Couldn't statically infer dim " + std::to_string(i) + " of output " +
          std::to_string(output_id));
    }
    output_shape.push_back(*dim);
    np_strides.push_back(stride);
    dim = Halide::Internal::as_const_int(node.shape[rank - (i + 1)]);
    if (!dim) {
      throw std::invalid_argument(
          "Couldn't statically infer dim " + std::to_string(rank - (i + 1)) +
          " of output " + std::to_string(output_id));
    }
    stride *= *dim;
  }
  std::reverse(np_strides.begin(), np_strides.end());

  // TODO:Better handeling of scalar outputs.
  if (output_shape.size() > 0) {
    py::array_t<T, py::array::c_style> result(output_shape);

    T* mutable_data = result.mutable_data();

    output_values.for_each_element([&](const int* halide_coords) {
      int np_index = 0;
      for (int i = 0; i < rank; i++) {
        np_index += halide_coords[i] * np_strides[i];
      }
      mutable_data[np_index] = output_values(halide_coords);
    });
    return result;
  } else {
    py::array_t<T, py::array::c_style> result({1});

    T* mutable_data = result.mutable_data();

    output_values.for_each_element([&](const int* halide_coords) {
      int np_index = 0;
      for (int i = 0; i < rank; i++) {
        np_index += halide_coords[i] * np_strides[i];
      }
      mutable_data[np_index] = output_values(halide_coords);
    });
    return result;
  }
}

static Halide::Type onnx_type_to_halide_type(int t) {
  switch (t) {
    case onnx::TensorProto::FLOAT:
      return Halide::Type(halide_type_float, 8 * sizeof(float), 1);
    case onnx::TensorProto::UINT8:
      return Halide::Type(halide_type_uint, 8 * sizeof(uint8_t), 1);
    case onnx::TensorProto::INT8:
      return Halide::Type(halide_type_int, 8 * sizeof(int8_t), 1);
    case onnx::TensorProto::UINT16:
      return Halide::Type(halide_type_uint, 8 * sizeof(uint16_t), 1);
    case onnx::TensorProto::INT16:
      return Halide::Type(halide_type_int, 8 * sizeof(int16_t), 1);
    case onnx::TensorProto::INT32:
      return Halide::Type(halide_type_int, 8 * sizeof(int32_t), 1);
    case onnx::TensorProto::INT64:
      return Halide::Type(halide_type_int, 8 * sizeof(int64_t), 1);
    case onnx::TensorProto::BOOL:
      return Halide::Type(halide_type_uint, 1, 1);
    case onnx::TensorProto::DOUBLE:
      return Halide::Type(halide_type_float, 8 * sizeof(double), 1);
    case onnx::TensorProto::UINT32:
      return Halide::Type(halide_type_uint, 8 * sizeof(uint32_t), 1);
    case onnx::TensorProto::UINT64:
      return Halide::Type(halide_type_uint, 8 * sizeof(uint64_t), 1);
    default:
      throw std::domain_error("Unsupported output type");
  }
}

static std::vector<py::array> Run(
    HalideModel pipeline,
    const std::vector<py::array>& inputs) {
  if (inputs.size() == pipeline.model->inputs.size()) {
    for (int i = 0; i < inputs.size(); ++i) {
      const std::string& input_name = pipeline.input_names[i];
      PreparePyArrayInput(pipeline, inputs[i], input_name);
    }
  } else {
    throw std::invalid_argument(
        "Expected " + std::to_string(pipeline.model->inputs.size()) +
        " numpy arrays but got " + std::to_string(inputs.size()));
  }

  // Return a list of numpy.ndarray (one per external output)
  const int num_outputs = pipeline.output_names.size();

  std::vector<Halide::Buffer<>> outputs(num_outputs);
  for (int i = 0; i < num_outputs; ++i) {
    Tensor node = pipeline.model->outputs.at(pipeline.output_names.at(i));
    std::vector<int> output_shape;
    const int rank = node.shape.size();
    for (int j = 0; j < rank; ++j) {
      const int64_t* dim = Halide::Internal::as_const_int(node.shape[j]);
      if (!dim) {
        throw std::invalid_argument(
            "Couldn't statically infer dim " + std::to_string(j) +
            " of output " + std::to_string(i));
      }
      output_shape.push_back(*dim);
    }
    outputs[i] = Halide::Buffer<>(
        onnx_type_to_halide_type(pipeline.output_types[i]), output_shape);
  }
  Halide::Realization real(outputs);
  Halide::Target tgt;
  tgt.set_feature(Halide::Target::NoAsserts, true);
  pipeline.rep->realize(real, tgt);

  std::vector<py::array> results;

  for (int i = 0; i < num_outputs; ++i) {
    switch (pipeline.output_types[i]) {
      case onnx::TensorProto::FLOAT:
        results.push_back(ExportOutput<float>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::UINT8:
        results.push_back(ExportOutput<uint8_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::INT8:
        results.push_back(ExportOutput<int8_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::UINT16:
        results.push_back(ExportOutput<uint16_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::INT16:
        results.push_back(ExportOutput<int16_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::INT32:
        results.push_back(ExportOutput<int32_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::INT64:
        results.push_back(ExportOutput<int64_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::BOOL:
        results.push_back(ExportOutput<bool>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::DOUBLE:
        results.push_back(ExportOutput<double>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::UINT32:
        results.push_back(ExportOutput<uint32_t>(outputs[i], pipeline, i));
        break;
      case onnx::TensorProto::UINT64:
        results.emplace_back(ExportOutput<uint64_t>(outputs[i], pipeline, i));
        break;
      default:
        throw std::domain_error("Unsupported output type");
    }
  }

  // Release all the inputs to free the corresponding memory until the next call
  // to Run.
  for (auto input : pipeline.model->inputs) {
    input.second.reset();
  }

  return results;
}

double Benchmark(HalideModel pipeline, int num_iters) {
  if (num_iters < 1) {
    throw std::invalid_argument(
        "Requested " + std::to_string(num_iters) +
        " benchmarking iterations which is less than the required minimum of 1.");
  }

  // Generate random value for every input
  for (ssize_t i = 0; i < pipeline.model->inputs.size(); ++i) {
    const std::string& input_name = pipeline.input_names[i];
    PrepareRandomInput(pipeline, input_name);
  }

  // Jit compile the model and warm it up by producing the outputs once
  const int num_outputs = pipeline.output_names.size();
  std::vector<Halide::Buffer<>> outputs(num_outputs);
  for (int i = 0; i < num_outputs; ++i) {
    Tensor node = pipeline.model->outputs.at(pipeline.output_names.at(i));
    std::vector<int> output_shape;
    const int rank = node.shape.size();
    for (int j = 0; j < rank; ++j) {
      const int64_t* dim = Halide::Internal::as_const_int(node.shape[j]);
      if (!dim) {
        throw std::invalid_argument(
            "Couldn't statically infer dim " + std::to_string(j) +
            " of output " + std::to_string(i));
      }
      output_shape.push_back(*dim);
    }
    outputs[i] = Halide::Buffer<>(
        onnx_type_to_halide_type(pipeline.output_types[i]), output_shape);
  }

  Halide::Realization real(outputs);
  Halide::Target tgt;
  tgt.set_feature(Halide::Target::NoAsserts, true);
  pipeline.rep->realize(real, tgt);

  // Now benchmark by computing the value of the outputs num_iter times
  struct timespec start;
  struct timespec end;
  clock_gettime(CLOCK_REALTIME, &start);
  for (int i = 0; i < num_iters; ++i) {
    pipeline.rep->realize(real, tgt);
  }
  clock_gettime(CLOCK_REALTIME, &end);

  double total_runtime =
      (end.tv_sec - start.tv_sec) * 1e9 + end.tv_nsec - start.tv_nsec;

  // Return the average runtime. TODO: filter the outliers if any.
  return total_runtime / num_iters;
}

PYBIND11_MODULE(model_cpp, m) {
  py::class_<HalideModel>(m, "HalideModel");
  m.def(
      "ConvertOnnxModel",
      &ConvertOnnxModel,
      "Converts onnx model proto into HalideModel object.");
  m.def(
      "AutoSchedule",
      &AutoSchedule,
      "A function to automatic schedule HalideModel.");
  m.def("Run", &Run, "A function to JIT compile and run HalideModel.");
  m.def("Benchmark", &Benchmark, "A function to benchmark the model");
}
