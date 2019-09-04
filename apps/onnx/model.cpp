#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "common_types.h"
#include "onnx_converter.h"
#include <fstream>
#include <random>
#include <sys/time.h>
#include <unordered_set>

namespace py = pybind11;

namespace {

HalideModel convert_onnx_model(
    const std::string &onnx_model_str) {
    onnx::ModelProto onnx_model;
    onnx_model.ParseFromString(onnx_model_str);

    if (onnx_model.graph().output_size() == 0) {
        throw std::invalid_argument("No output specified in the model");
    }

    std::unordered_set<std::string> dflt_values;
    for (const auto &dflt : onnx_model.graph().initializer()) {
        dflt_values.insert(dflt.name());
    }
    HalideModel result;
    for (const auto &input : onnx_model.graph().input()) {
        if (dflt_values.find(input.name()) != dflt_values.end()) {
            continue;
        }
        result.input_names.push_back(input.name());
        result.input_types[input.name()] = input.type().tensor_type().elem_type();
    }
    for (const auto &output : onnx_model.graph().output()) {
        result.output_names.push_back(output.name());
        result.output_types.push_back(output.type().tensor_type().elem_type());
    }

    result.model.reset(new Model(convert_model(onnx_model)));

    std::vector<Halide::Func> funcs;
    for (const auto &output : onnx_model.graph().output()) {
        const auto &tensor = result.model->outputs.at(output.name());
        funcs.push_back(tensor.rep);
    }
    result.rep.reset(new Halide::Pipeline(funcs));

    for (const Halide::Expr &requirement : result.model->requirements) {
        if (Halide::Internal::is_pure(requirement)) {
            result.rep->add_requirement(requirement);
        }
    }

    return result;
}

std::string auto_schedule(const HalideModel &pipeline) {
    // Generate a schedule for the pipeline.
    Halide::Target tgt = Halide::get_host_target();
    auto schedule = pipeline.rep->auto_schedule(tgt);
    return schedule.schedule_source;
}

template<typename T>
struct Distribution {
    typedef typename std::conditional<
        std::is_floating_point<T>::value,
        std::uniform_real_distribution<T>,
        std::uniform_int_distribution<T>>::type Type;
};
template<>
struct Distribution<bool> {
    typedef typename std::uniform_int_distribution<uint8_t> Type;
};

template<typename T, bool Random>
void prepare_image_param(
    Halide::ImageParam &image_param,
    const std::vector<int> &shape,
    const py::array *np_data) {
    if (Random) {
        Halide::Buffer<T> values(shape);

        typename Distribution<T>::Type distrib;
        std::mt19937 generator;

        values.for_each_value(
            [&](T &val) { val = distrib(generator); });
        image_param.set(values);
    } else {
        int stride = 1;
        std::vector<int> np_strides;
        for (int i = 0; i < shape.size(); ++i) {
            np_strides.push_back(stride);
            stride *= shape[shape.size() - (i + 1)];
        }
        std::reverse(np_strides.begin(), np_strides.end());

        const T *raw_data = static_cast<const T *>(np_data->data());
        Halide::Buffer<T> values(shape, image_param.name() + "_buf");
        values.for_each_element([&](const int *halide_coords) {
            int np_index = 0;
            for (int i = 0; i < shape.size(); i++) {
                np_index += halide_coords[i] * np_strides[i];
            }
            values(halide_coords) = raw_data[np_index];
        });
        image_param.set(values);
    }
}

void prepare_py_array_input(
    const HalideModel &pipeline,
    const py::array &ndarray,
    const std::string &input_name) {
    const int rank = ndarray.ndim();
    std::vector<int> input_shape;
    for (int i = 0; i < rank; ++i) {
        input_shape.push_back(ndarray.shape(i));
    }

    Halide::ImageParam &input = pipeline.model->inputs.at(input_name);

    if (py::isinstance<py::array_t<bool>>(ndarray)) {
        prepare_image_param<bool, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::int8_t>>(ndarray)) {
        prepare_image_param<std::int8_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::int16_t>>(ndarray)) {
        prepare_image_param<std::int16_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::int32_t>>(ndarray)) {
        prepare_image_param<std::int32_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::int64_t>>(ndarray)) {
        prepare_image_param<std::int64_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::uint8_t>>(ndarray)) {
        prepare_image_param<std::uint8_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::uint16_t>>(ndarray)) {
        prepare_image_param<std::uint16_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::uint32_t>>(ndarray)) {
        prepare_image_param<std::uint32_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<std::uint64_t>>(ndarray)) {
        prepare_image_param<std::uint64_t, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<float>>(ndarray)) {
        prepare_image_param<float, false>(input, input_shape, &ndarray);
    } else if (py::isinstance<py::array_t<double>>(ndarray)) {
        prepare_image_param<double, false>(input, input_shape, &ndarray);
    } else {
        throw std::invalid_argument(
            std::string("Unsupported type ") + ndarray.dtype().kind() +
            " for input " + input_name);
    }
}

void prepare_random_input(
    const HalideModel &pipeline,
    const std::string &input_name) {
    const Tensor &t = pipeline.model->tensors.at(input_name);
    std::vector<int> input_shape;
    for (int i = 0; i < t.shape.size(); ++i) {
        const int64_t *dim = Halide::Internal::as_const_int(t.shape[i]);
        if (!dim) {
            throw std::invalid_argument(
                "Unknown dim " + std::to_string(i) + " for input " + input_name);
        }
        input_shape.push_back(*dim);
    }

    py::array rand_array;
    Halide::ImageParam &input = pipeline.model->inputs.at(input_name);

    switch (t.type) {
    case onnx::TensorProto::BOOL: {
        prepare_image_param<bool, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::INT8: {
        prepare_image_param<std::int8_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::INT16: {
        prepare_image_param<std::int16_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::INT32: {
        prepare_image_param<std::int32_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::INT64: {
        prepare_image_param<std::int64_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::UINT8: {
        prepare_image_param<std::uint8_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::UINT16: {
        prepare_image_param<std::uint16_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::UINT32: {
        prepare_image_param<std::uint32_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::UINT64: {
        prepare_image_param<std::uint64_t, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::FLOAT: {
        prepare_image_param<float, true>(input, input_shape, &rand_array);
        break;
    }
    case onnx::TensorProto::DOUBLE: {
        prepare_image_param<double, true>(input, input_shape, &rand_array);
        break;
    }
    default: {
        throw std::invalid_argument(
            std::string("Unsupported type for input ") + input_name);
    }
    }
}

template<typename T>
py::array_t<T> export_output(
    const Halide::Buffer<T> &output_values,
    const HalideModel &pipeline,
    const std::vector<int> &output_shape) {
    int stride = 1;
    const int rank = output_shape.size();
    std::vector<int> np_strides;
    for (int i = 0; i < rank; ++i) {
        np_strides.push_back(stride);
        int dim = output_shape[rank - (i + 1)];
        stride *= dim;
    }
    std::reverse(np_strides.begin(), np_strides.end());

    // TODO:Better handling of scalar outputs.
    if (output_shape.size() > 0) {
        py::array_t<T, py::array::c_style> result(output_shape);

        T *mutable_data = result.mutable_data();

        output_values.for_each_element([&](const int *halide_coords) {
            int np_index = 0;
            for (int i = 0; i < rank; i++) {
                np_index += halide_coords[i] * np_strides[i];
            }
            mutable_data[np_index] = output_values(halide_coords);
        });
        return result;
    } else {
        py::array_t<T, py::array::c_style> result({ 1 });

        T *mutable_data = result.mutable_data();

        output_values.for_each_element([&](const int *halide_coords) {
            int np_index = 0;
            for (int i = 0; i < rank; i++) {
                np_index += halide_coords[i] * np_strides[i];
            }
            mutable_data[np_index] = output_values(halide_coords);
        });
        return result;
    }
}

// TODO: Consider using get_tensor_type ?!
Halide::Type onnx_type_to_halide_type(int t) {
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

std::vector<py::array> run(
    const HalideModel &pipeline,
    const std::vector<py::array> &inputs, const std::string &device) {

    std::map<std::string, std::vector<int>> input_shapes;

    if (inputs.size() == pipeline.model->inputs.size()) {
        for (int i = 0; i < inputs.size(); ++i) {
            const std::string &input_name = pipeline.input_names[i];
            prepare_py_array_input(pipeline, inputs[i], input_name);
            std::vector<int> &input_shape = input_shapes[input_name];
            for (int j = 0; j < inputs[i].ndim(); ++j) {
                input_shape.push_back(inputs[i].shape(j));
            }
        }
    } else {
        throw std::invalid_argument(
            "Expected " + std::to_string(pipeline.model->inputs.size()) +
            " numpy arrays but got " + std::to_string(inputs.size()));
    }

    // Return a list of numpy.ndarray (one per external output)
    const int num_outputs = pipeline.output_names.size();

    std::map<std::string, std::vector<int>> output_shapes;
    compute_output_shapes(*pipeline.model, input_shapes, &output_shapes);

    std::vector<Halide::Buffer<>> outputs(num_outputs);
    for (int i = 0; i < num_outputs; ++i) {
        const std::string &output_name = pipeline.output_names.at(i);
        const std::vector<int> &output_shape = output_shapes.at(output_name);
        outputs[i] = Halide::Buffer<>(
            onnx_type_to_halide_type(pipeline.output_types[i]), output_shape);
    }
    Halide::Realization real(outputs);
    Halide::Target tgt;
    tgt.set_feature(Halide::Target::DisableLLVMLoopOpt);
    if (device == "CUDA") {
        tgt.set_feature(Halide::Target::CUDA);
    }

    pipeline.rep->realize(real, tgt);

    std::vector<py::array> results;

    for (int i = 0; i < num_outputs; ++i) {
        const std::string &output_name = pipeline.output_names[i];
        const std::vector<int> &output_shape = output_shapes.at(output_name);
        switch (pipeline.output_types[i]) {
        case onnx::TensorProto::FLOAT:
            results.push_back(export_output<float>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT8:
            results.push_back(export_output<uint8_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT8:
            results.push_back(export_output<int8_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT16:
            results.push_back(export_output<uint16_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT16:
            results.push_back(export_output<int16_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT32:
            results.push_back(export_output<int32_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT64:
            results.push_back(export_output<int64_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::BOOL:
            results.push_back(export_output<bool>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::DOUBLE:
            results.push_back(export_output<double>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT32:
            results.push_back(export_output<uint32_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT64:
            results.emplace_back(export_output<uint64_t>(outputs[i], pipeline, output_shape));
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

double benchmark(const HalideModel &pipeline, int num_iters, const std::string &device) {
    if (num_iters < 1) {
        throw std::invalid_argument(
            "Requested " + std::to_string(num_iters) +
            " benchmarking iterations which is less than the required minimum of 1.");
    }

    // Generate random value for every input
    for (ssize_t i = 0; i < pipeline.model->inputs.size(); ++i) {
        const std::string &input_name = pipeline.input_names[i];
        prepare_random_input(pipeline, input_name);
    }

    // Jit compile the model and warm it up by producing the outputs once
    const int num_outputs = pipeline.output_names.size();
    std::vector<Halide::Buffer<>> outputs(num_outputs);
    for (int i = 0; i < num_outputs; ++i) {
        Tensor node = pipeline.model->outputs.at(pipeline.output_names.at(i));
        std::vector<int> output_shape;
        const int rank = node.shape.size();
        for (int j = 0; j < rank; ++j) {
            const int64_t *dim = Halide::Internal::as_const_int(node.shape[j]);
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
    tgt.set_feature(Halide::Target::DisableLLVMLoopOpt);
    if (device == "CUDA") {
        tgt.set_feature(Halide::Target::CUDA);
    }
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

void print_loop_nest(const HalideModel &pipeline) {
    pipeline.rep->print_loop_nest();
}

void print_lowered_statement(const HalideModel &pipeline) {
    std::string tmp_file = std::tmpnam(nullptr);
    pipeline.rep->compile_to_lowered_stmt(tmp_file, pipeline.rep->infer_arguments());
    std::ifstream is(tmp_file);
    std::string line;
    while (std::getline(is, line)) {
        std::cout << line << std::endl;
    }
    std::remove(tmp_file.c_str());
}

}  // namespace

PYBIND11_MODULE(model_cpp, m) {
    py::class_<HalideModel>(m, "HalideModel");
    m.def(
        "ConvertOnnxModel",
        &convert_onnx_model,
        "Converts onnx model proto into HalideModel object.");
    m.def(
        "AutoSchedule",
        &auto_schedule,
        "A function to automatic schedule HalideModel.");
    m.def("Run", &run, "A function to JIT compile and run HalideModel.");
    m.def("Benchmark", &benchmark, "A function to benchmark the model");
    m.def("PrintLoopNest", &print_loop_nest, "Print a high level representation of the loop nest");
    m.def("PrintLoweredStatement", &print_lowered_statement, "Print a detailed representation of the lowered code");
}
