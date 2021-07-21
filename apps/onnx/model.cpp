#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "benchmarking_utils.h"
#include "common_types.h"
#include "denormal_disabler.h"
#include "onnx_converter.h"
#include <fstream>
#include <random>
#include <sys/time.h>
#include <unordered_set>

namespace py = pybind11;

namespace {

HalideModel convert_onnx_model(
    const std::string &onnx_model_str,
    const std::unordered_map<std::string, int> &expected_dim_sizes,
    const IOLayout layout) {
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

    result.model = std::make_shared<Model>(
        convert_model(onnx_model, expected_dim_sizes, layout));

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

template<typename T>
void prepare_random_image_param(
    Halide::ImageParam &image_param,
    const std::vector<int> &shape) {
    std::vector<int> np_shape = shape;
    std::reverse(np_shape.begin(), np_shape.end());
    Halide::Buffer<T> values(np_shape, image_param.name() + "_rand_buf");
    std::vector<int> dims(shape.size());
    std::iota(dims.rbegin(), dims.rend(), 0);
    values.transpose(dims);
    typename Distribution<T>::Type distrib;
    std::mt19937 generator;
    values.for_each_value([&](T &val) { val = distrib(generator); });
    image_param.set(values);
}

template<typename T>
void prepare_actual_image_param(
    Halide::ImageParam &image_param,
    const std::vector<int> &shape,
    const py::array &np_data) {
    // The numpy layout is the opposite of the halide layout. Create a halide
    // buffer op top of the raw numpy buffer and transpose it to end up with the
    // expected dim order. This will avoid the need to copy the data explicitly.
    std::vector<int> np_shape = shape;
    std::reverse(np_shape.begin(), np_shape.end());
    T *raw_data = static_cast<T *>(const_cast<void *>(np_data.data()));
    Halide::Buffer<T> values(raw_data, np_shape, image_param.name() + "_buf");
    std::vector<int> dims(shape.size());
    std::iota(dims.rbegin(), dims.rend(), 0);
    values.transpose(dims);
    image_param.set(values);
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
    // Make sure the input is contiguous.
    int stride = ndarray.itemsize();
    for (int i = rank - 1; i >= 0; --i) {
        if (stride != ndarray.strides(i)) {
            throw std::invalid_argument(
                std::string("Non contiguous array in dim ") + std::to_string(i) +
                " for input " + input_name + ". Make a copy before calling.");
        }
        stride *= ndarray.shape(i);
    }

    Halide::ImageParam &input = pipeline.model->inputs.at(input_name);

    if (py::isinstance<py::array_t<bool>>(ndarray)) {
        prepare_actual_image_param<bool>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::int8_t>>(ndarray)) {
        prepare_actual_image_param<std::int8_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::int16_t>>(ndarray)) {
        prepare_actual_image_param<std::int16_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::int32_t>>(ndarray)) {
        prepare_actual_image_param<std::int32_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::int64_t>>(ndarray)) {
        prepare_actual_image_param<std::int64_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::uint8_t>>(ndarray)) {
        prepare_actual_image_param<std::uint8_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::uint16_t>>(ndarray)) {
        prepare_actual_image_param<std::uint16_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::uint32_t>>(ndarray)) {
        prepare_actual_image_param<std::uint32_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<std::uint64_t>>(ndarray)) {
        prepare_actual_image_param<std::uint64_t>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<float>>(ndarray)) {
        prepare_actual_image_param<float>(input, input_shape, ndarray);
    } else if (py::isinstance<py::array_t<double>>(ndarray)) {
        prepare_actual_image_param<double>(input, input_shape, ndarray);
    } else {
        throw std::invalid_argument(
            std::string("Unsupported type ") + ndarray.dtype().kind() +
            " for input " + input_name);
    }
}

void prepare_random_input(
    const HalideModel &pipeline,
    const std::string &input_name) {
    const Halide::ImageParam &in = pipeline.model->inputs.at(input_name);
    const Tensor &t = pipeline.model->tensors.at(input_name);
    std::vector<int> input_shape;
    for (int i = 0; i < t.shape.size(); ++i) {
        const int64_t *dim = Halide::Internal::as_const_int(t.shape[i]);
        if (!dim) {
            // The dimension isn't fixed: use the estimated typical value instead if
            // one was provided.
            Halide::Expr d = in.dim(i).extent_estimate();
            dim = Halide::Internal::as_const_int(d);
        }
        if (!dim) {
            throw std::invalid_argument(
                "Unknown dim " + std::to_string(i) + " for input " + input_name);
        }
        input_shape.push_back(*dim);
    }

    Halide::ImageParam &input = pipeline.model->inputs.at(input_name);

    switch (t.type) {
    case onnx::TensorProto::BOOL: {
        prepare_random_image_param<bool>(input, input_shape);
        break;
    }
    case onnx::TensorProto::INT8: {
        prepare_random_image_param<std::int8_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::INT16: {
        prepare_random_image_param<std::int16_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::INT32: {
        prepare_random_image_param<std::int32_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::INT64: {
        prepare_random_image_param<std::int64_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::UINT8: {
        prepare_random_image_param<std::uint8_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::UINT16: {
        prepare_random_image_param<std::uint16_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::UINT32: {
        prepare_random_image_param<std::uint32_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::UINT64: {
        prepare_random_image_param<std::uint64_t>(input, input_shape);
        break;
    }
    case onnx::TensorProto::FLOAT: {
        prepare_random_image_param<float>(input, input_shape);
        break;
    }
    case onnx::TensorProto::DOUBLE: {
        prepare_random_image_param<double>(input, input_shape);
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
    // TODO:Better handling of scalar outputs.
    if (output_shape.size() > 0) {
        py::array_t<T, py::array::c_style> result(output_shape);

        T *mutable_data = result.mutable_data();

        int np_index = 0;
        output_values.for_each_value([&, np_index](const T &v) mutable {
            mutable_data[np_index] = v;
            ++np_index;
        });
        return result;
    } else {
        py::array_t<T, py::array::c_style> result({1});

        T *mutable_data = result.mutable_data();

        output_values.for_each_element([&](const int *halide_coords) {
            mutable_data[0] = output_values(halide_coords);
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
    const std::vector<py::array> &inputs,
    const std::string &device) {
    // Force denormal numbers to be flushed to zero until the class destructor is
    // called.
    DenormalDisabler scoped_denormal_disabler;

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
        std::vector<int> output_shape = output_shapes.at(output_name);
        std::reverse(output_shape.begin(), output_shape.end());
        outputs[i] = Halide::Buffer<>(
            onnx_type_to_halide_type(pipeline.output_types[i]), output_shape);
        std::vector<int> dims(output_shape.size());
        std::iota(dims.rbegin(), dims.rend(), 0);
        outputs[i].transpose(dims);
    }
    Halide::Realization real(outputs);
    Halide::Target tgt = Halide::get_host_target();
    // Don't create buffers larger than 2GB since we use 32bit signed indices to
    // index the data stored in them.
    tgt.set_feature(Halide::Target::LargeBuffers, false);
    if (device == "CUDA") {
        tgt.set_feature(Halide::Target::CUDA, true);
    }

    pipeline.rep->realize(real, tgt);

    std::vector<py::array> results;

    for (int i = 0; i < num_outputs; ++i) {
        const std::string &output_name = pipeline.output_names[i];
        const std::vector<int> &output_shape = output_shapes.at(output_name);
        switch (pipeline.output_types[i]) {
        case onnx::TensorProto::FLOAT:
            results.push_back(
                export_output<float>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT8:
            results.push_back(
                export_output<uint8_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT8:
            results.push_back(
                export_output<int8_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT16:
            results.push_back(
                export_output<uint16_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT16:
            results.push_back(
                export_output<int16_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT32:
            results.push_back(
                export_output<int32_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::INT64:
            results.push_back(
                export_output<int64_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::BOOL:
            results.push_back(
                export_output<bool>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::DOUBLE:
            results.push_back(
                export_output<double>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT32:
            results.push_back(
                export_output<uint32_t>(outputs[i], pipeline, output_shape));
            break;
        case onnx::TensorProto::UINT64:
            results.emplace_back(
                export_output<uint64_t>(outputs[i], pipeline, output_shape));
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

double benchmark(
    const HalideModel &pipeline,
    int num_iters,
    const std::string &device) {
    if (num_iters < 1) {
        throw std::invalid_argument(
            "Requested " + std::to_string(num_iters) +
            " benchmarking iterations which is less than the required minimum of 1.");
    }

    // Force denormal numbers to be flushed to zero until the class destructor is
    // called.
    DenormalDisabler scoped_denormal_disabler;

    // large array used to flush the caches after each iteration of benchmarking.
    CacheEvictor cache_evictor;

    // Generate random value for every input
    for (ssize_t i = 0; i < pipeline.model->inputs.size(); ++i) {
        const std::string &input_name = pipeline.input_names[i];
        prepare_random_input(pipeline, input_name);
    }

    // Jit compile the model and warm it up by producing the outputs once
    std::map<std::string, std::vector<int>> expected_output_shapes;
    compute_expected_output_shapes(*pipeline.model, &expected_output_shapes);

    const int num_outputs = pipeline.output_names.size();
    std::vector<Halide::Buffer<>> outputs(num_outputs);
    for (int i = 0; i < num_outputs; ++i) {
        const std::string &output_name = pipeline.output_names.at(i);
        std::vector<int> output_shape = expected_output_shapes.at(output_name);
        std::reverse(output_shape.begin(), output_shape.end());
        Halide::Buffer<> buf(
            onnx_type_to_halide_type(pipeline.output_types[i]), output_shape);
        std::vector<int> dims(output_shape.size());
        std::iota(dims.rbegin(), dims.rend(), 0);
        outputs[i] = buf.transposed(dims);
    }

    Halide::Realization real(outputs);
    Halide::Target tgt = Halide::get_host_target();
    // Don't create buffers larger than 2GB since we use 32bit signed indices to
    // index the data stored in them.
    tgt.set_feature(Halide::Target::LargeBuffers, false);
    if (device == "CUDA") {
        tgt.set_feature(Halide::Target::CUDA, true);
    }
    pipeline.rep->realize(real, tgt);

    // Now benchmark by computing the value of the outputs num_iter times
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &start);
    for (int i = 0; i < num_iters; ++i) {
        // Increment the coefficients store in the cache evictor: this ensures that
        // all the data left in caches from the previous iteration is flushed out.
        cache_evictor.flush_caches();
        pipeline.rep->realize(real, tgt);
    }
    clock_gettime(CLOCK_REALTIME, &end);

    double total_runtime =
        (end.tv_sec - start.tv_sec) * 1e9 + end.tv_nsec - start.tv_nsec;

    // Figure out how long it took to generate new inputs at every iteration
    // and adjust the runtime accordingly.
    clock_gettime(CLOCK_REALTIME, &start);
    for (int i = 0; i < num_iters; ++i) {
        cache_evictor.flush_caches();
    }
    clock_gettime(CLOCK_REALTIME, &end);
    double input_gen_time =
        (end.tv_sec - start.tv_sec) * 1e9 + end.tv_nsec - start.tv_nsec;

    total_runtime -= input_gen_time;

    // Return the average runtime. TODO: filter the outliers if any.
    return total_runtime / num_iters;
}

void compile(
    const HalideModel &pipeline,
    const std::string &func_name,
    const std::string &lib_name) {
    std::vector<Halide::Argument> inputs;
    for (const std::string &input_name : pipeline.input_names) {
        inputs.push_back(pipeline.model->inputs.at(input_name));
    }
    Halide::Target tgt = Halide::get_host_target();
    // tgt.set_feature(Halide::Target::Debug, true);
    // tgt.set_feature(Halide::Target::NoBoundsQuery, true);
    // tgt.set_feature(Halide::Target::TracePipeline, true);
    // tgt.set_feature(Halide::Target::TraceRealizations, true);
    // pipeline.rep->compile_to_lowered_stmt(std::string("/tmp/")+lib_name+".stmt",
    // inputs, Halide::Text, tgt);
    pipeline.rep->compile_to_file(
        std::string("/tmp/") + lib_name, inputs, func_name, tgt);
    pipeline.rep->compile_to_static_library(
        std::string("/tmp/") + lib_name, inputs, func_name, tgt);
    pipeline.rep->compile_to_c(
        std::string("/tmp/") + lib_name + ".cpp", inputs, func_name, tgt);
    pipeline.rep->compile_to_header(
        std::string("/tmp/") + lib_name + ".h", inputs, func_name, tgt);
}

void print_loop_nest(const HalideModel &pipeline) {
    pipeline.rep->print_loop_nest();
}

void print_lowered_statement(const HalideModel &pipeline) {
    std::string tmp_file = std::tmpnam(nullptr);
    pipeline.rep->compile_to_lowered_stmt(
        tmp_file, pipeline.rep->infer_arguments());
    std::ifstream is(tmp_file);
    std::string line;
    while (std::getline(is, line)) {
        std::cout << line << "\n";
    }
    std::remove(tmp_file.c_str());
}

}  // namespace

PYBIND11_MODULE(model_cpp, m) {
    py::class_<HalideModel>(m, "HalideModel");

    py::enum_<IOLayout>(m, "Layout")
        .value("Native", IOLayout::Native)
        .value("NumPy", IOLayout::NumPy);

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
    m.def("Compile", &compile, "Compile the pipeline");
    m.def(
        "PrintLoopNest",
        &print_loop_nest,
        "Print a high level representation of the loop nest");
    m.def(
        "PrintLoweredStatement",
        &print_lowered_statement,
        "Print a detailed representation of the lowered code");
}
