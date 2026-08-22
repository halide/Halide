#include "PyRuntimeBuffer.h"

#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Float16.h"
#include "HalideBuffer.h"

namespace py = pybind11;

namespace Halide::PythonRuntimeBindings {
namespace {

using RuntimeBuffer = Halide::Runtime::Buffer<>;
using BufferDimension = RuntimeBuffer::Dimension;

std::string unique_name() {
    static std::atomic<uint64_t> next{0};
    return "b" + std::to_string(next++);
}

template<typename T>
std::string format_descriptor() {
    return py::format_descriptor<T>::format();
}

template<>
std::string format_descriptor<float16_t>() {
    return "e";
}

halide_type_t format_descriptor_to_type(std::string format, py::ssize_t itemsize) {
    char byte_order = '@';
    if (!format.empty() && std::strchr("@<>!=", format.front())) {
        byte_order = format.front();
        format.erase(format.begin());
    }
    const uint16_t endian_test = 1;
    const bool native_is_little_endian = *reinterpret_cast<const uint8_t *>(&endian_test) == 1;
    const bool non_native_byte_order =
        (native_is_little_endian && (byte_order == '>' || byte_order == '!')) ||
        (!native_is_little_endian && byte_order == '<');
    if (non_native_byte_order && itemsize > 1) {
        throw py::value_error("Non-native byte order is not supported.");
    }

#define HANDLE_TYPE(TYPE)                                                               \
    if (format == format_descriptor<TYPE>()) {                                          \
        const halide_type_t type = Halide::Runtime::Buffer<TYPE>::static_halide_type(); \
        if (type.bytes() != itemsize) {                                                 \
            throw py::value_error("Buffer item size does not match its format.");       \
        }                                                                               \
        return type;                                                                    \
    }
    HANDLE_TYPE(bool)
    HANDLE_TYPE(uint8_t)
    HANDLE_TYPE(uint16_t)
    HANDLE_TYPE(uint32_t)
    HANDLE_TYPE(uint64_t)
    HANDLE_TYPE(int8_t)
    HANDLE_TYPE(int16_t)
    HANDLE_TYPE(int32_t)
    HANDLE_TYPE(int64_t)
    HANDLE_TYPE(float16_t)
    HANDLE_TYPE(float)
    HANDLE_TYPE(double)
#undef HANDLE_TYPE
    if (format == "l") {
        const halide_type_t type = itemsize == 8 ? Runtime::Buffer<int64_t>::static_halide_type() : Runtime::Buffer<int32_t>::static_halide_type();
        if (type.bytes() != itemsize) {
            throw py::value_error("Buffer item size does not match its format.");
        }
        return type;
    }
    throw py::value_error("Unsupported Buffer<> type.");
}

std::string type_to_format_descriptor(halide_type_t type) {
#define HANDLE_TYPE(TYPE)                                      \
    if (type == Runtime::Buffer<TYPE>::static_halide_type()) { \
        return format_descriptor<TYPE>();                      \
    }
    HANDLE_TYPE(bool)
    HANDLE_TYPE(uint8_t)
    HANDLE_TYPE(uint16_t)
    HANDLE_TYPE(uint32_t)
    HANDLE_TYPE(uint64_t)
    HANDLE_TYPE(int8_t)
    HANDLE_TYPE(int16_t)
    HANDLE_TYPE(int32_t)
    HANDLE_TYPE(int64_t)
    HANDLE_TYPE(float16_t)
    HANDLE_TYPE(float)
    HANDLE_TYPE(double)
#undef HANDLE_TYPE
    throw py::value_error("Unsupported Buffer<> type.");
}

std::string type_string(const halide_type_t &type) {
    std::ostringstream out;
    out << type;
    return out.str();
}

template<typename T>
T value_cast(const py::object &value) {
    return value.cast<T>();
}

template<>
float16_t value_cast<float16_t>(const py::object &value) {
    return float16_t(value.cast<double>());
}

class Buffer {
public:
    Buffer() = default;

    Buffer(const Buffer &other)
        : buffer_(other.runtime_buffer()),
          owner_(other.owner_), identity_(other.identity_), name_(other.name_), defined_(other.defined_) {
    }

    Buffer(Buffer &&other) noexcept
        : buffer_(std::move(other.buffer_)), borrowed_buffer_(other.borrowed_buffer_),
          owner_(std::move(other.owner_)), identity_(std::move(other.identity_)),
          name_(std::move(other.name_)), defined_(other.defined_) {
    }

    Buffer &operator=(const Buffer &other) {
        if (this != &other) {
            buffer_ = other.runtime_buffer();
            borrowed_buffer_ = nullptr;
            owner_ = other.owner_;
            identity_ = other.identity_;
            name_ = other.name_;
            defined_ = other.defined_;
        }
        return *this;
    }

    Buffer &operator=(Buffer &&other) noexcept {
        if (this != &other) {
            buffer_ = std::move(other.buffer_);
            borrowed_buffer_ = other.borrowed_buffer_;
            owner_ = std::move(other.owner_);
            identity_ = std::move(other.identity_);
            name_ = std::move(other.name_);
            defined_ = other.defined_;
        }
        return *this;
    }

    Buffer(py::buffer source, std::string name, bool reverse_axes)
        : owner_(std::move(source)), identity_(std::make_shared<char>()), name_(name.empty() ? unique_name() : std::move(name)), defined_(true) {
        py::buffer_info info = py::reinterpret_borrow<py::buffer>(owner_).request(true);
        if (info.ndim < 0 || info.ndim > INT32_MAX) {
            throw py::value_error("Out of range dimensionality in buffer conversion.");
        }
        const halide_type_t type = format_descriptor_to_type(info.format, info.itemsize);
        std::vector<halide_dimension_t> dims(info.ndim);
        for (int i = 0; i < info.ndim; ++i) {
            if (info.strides[i] % type.bytes() != 0) {
                throw py::value_error("Buffer byte stride is not a multiple of its item size.");
            }
            const auto stride = info.strides[i] / type.bytes();
            if (info.shape[i] < 0 || info.shape[i] > INT32_MAX || stride < INT32_MIN || stride > INT32_MAX) {
                throw py::value_error("Out of range dimensions in buffer conversion.");
            }
            const int dst = reverse_axes ? info.ndim - i - 1 : i;
            dims[dst] = {0, static_cast<int32_t>(info.shape[i]), static_cast<int32_t>(stride)};
        }
        buffer_ = RuntimeBuffer(type, info.ptr, info.ndim, dims.data());
        runtime_buffer().set_host_dirty();
    }

    Buffer(halide_type_t type, const std::vector<int> &sizes, std::string name)
        : buffer_(type, sizes), identity_(std::make_shared<char>()), name_(name.empty() ? unique_name() : std::move(name)), defined_(true) {
    }

    Buffer(halide_type_t type, const std::vector<int> &sizes,
           const std::vector<int> &storage_order, std::string name)
        : buffer_(type, sizes, storage_order), identity_(std::make_shared<char>()), name_(name.empty() ? unique_name() : std::move(name)), defined_(true) {
    }

    static Buffer from_runtime(const RuntimeBuffer &buffer, std::string name, bool defined) {
        Buffer result;
        result.buffer_ = buffer;
        result.identity_ = std::make_shared<char>();
        result.name_ = std::move(name);
        result.defined_ = defined;
        return result;
    }

    static Buffer from_borrowed(const py::capsule &capsule, py::object owner, std::string name) {
        Buffer result;
        result.borrowed_buffer_ = static_cast<RuntimeBuffer *>(PyCapsule_GetPointer(capsule.ptr(), runtime_buffer_capsule_name));
        if (!result.borrowed_buffer_) {
            throw py::error_already_set();
        }
        result.owner_ = std::move(owner);
        result.identity_ = std::make_shared<char>();
        result.name_ = std::move(name);
        result.defined_ = true;
        return result;
    }

    Buffer view(const RuntimeBuffer &buffer) const {
        Buffer result = from_runtime(buffer, unique_name(), true);
        result.owner_ = owner_;
        result.identity_ = identity_;
        return result;
    }

    static Buffer make_bounds_query(halide_type_t type, const std::vector<int> &sizes, std::string name) {
        return from_runtime(RuntimeBuffer(type, nullptr, sizes), name.empty() ? unique_name() : std::move(name), true);
    }

    static Buffer make_scalar(halide_type_t type, std::string name) {
        return from_runtime(RuntimeBuffer::make_scalar(type), name.empty() ? unique_name() : std::move(name), true);
    }

    static Buffer make_interleaved(halide_type_t type, int width, int height, int channels, std::string name) {
        return from_runtime(RuntimeBuffer::make_interleaved(type, width, height, channels),
                            name.empty() ? unique_name() : std::move(name), true);
    }

    static Buffer make_with_shape_of(const Buffer &source, std::string name) {
        return from_runtime(RuntimeBuffer::make_with_shape_of(source.runtime_buffer()),
                            name.empty() ? unique_name() : std::move(name), true);
    }

    RuntimeBuffer &runtime_buffer() {
        return borrowed_buffer_ ? *borrowed_buffer_ : buffer_;
    }

    const RuntimeBuffer &runtime_buffer() const {
        return borrowed_buffer_ ? *borrowed_buffer_ : buffer_;
    }

    py::capsule halide_buffer_t_capsule() {
        require_defined();
        return py::capsule(runtime_buffer().raw_buffer(), halide_buffer_capsule_name);
    }

    py::capsule runtime_buffer_capsule() {
        require_defined();
        return py::capsule(&runtime_buffer(), runtime_buffer_capsule_name);
    }

    bool defined() const {
        return defined_;
    }

    bool same_as(const Buffer &other) const {
        return identity_ == other.identity_;
    }

    const std::string &name() const {
        return name_;
    }

    void set_name(const std::string &name) {
        name_ = name;
    }

    py::buffer_info buffer_info(bool reverse_axes) {
        require_defined();
        RuntimeBuffer &buffer = runtime_buffer();
        if (buffer.data() == nullptr) {
            throw py::value_error("Cannot convert a Buffer<> with null host ptr to a Python buffer.");
        }
        const int d = buffer.dimensions();
        const int bytes = buffer.type().bytes();
        std::vector<py::ssize_t> shape(d), strides(d);
        for (int i = 0; i < d; ++i) {
            const int dst = reverse_axes ? d - i - 1 : i;
            shape[dst] = buffer.dim(i).extent();
            strides[dst] = static_cast<py::ssize_t>(buffer.dim(i).stride()) * bytes;
        }
        return py::buffer_info(buffer.data(), bytes, type_to_format_descriptor(buffer.type()), d, shape, strides);
    }

    py::array numpy_view(bool reverse_axes) {
        return py::array(buffer_info(reverse_axes), py::cast(this));
    }

    py::object getitem(const std::vector<int> &pos) {
        check_position(pos);
        RuntimeBuffer &buffer = runtime_buffer();
        if (buffer.type() == Runtime::Buffer<float16_t>::static_halide_type()) {
            return py::float_(static_cast<double>(buffer.as<float16_t>()(pos.data())));
        }
#define HANDLE_TYPE(TYPE)                                               \
    if (buffer.type() == Runtime::Buffer<TYPE>::static_halide_type()) { \
        return py::cast(buffer.as<TYPE>()(pos.data()));                 \
    }
        HANDLE_TYPE(bool)
        HANDLE_TYPE(uint8_t)
        HANDLE_TYPE(uint16_t)
        HANDLE_TYPE(uint32_t)
        HANDLE_TYPE(uint64_t)
        HANDLE_TYPE(int8_t)
        HANDLE_TYPE(int16_t)
        HANDLE_TYPE(int32_t)
        HANDLE_TYPE(int64_t)
        HANDLE_TYPE(float)
        HANDLE_TYPE(double)
#undef HANDLE_TYPE
        throw py::value_error("Unsupported Buffer<> type.");
    }

    py::object setitem(const std::vector<int> &pos, const py::object &value) {
        check_position(pos);
        RuntimeBuffer &buffer = runtime_buffer();
        if (buffer.type() == Runtime::Buffer<float16_t>::static_halide_type()) {
            buffer.as<float16_t>()(pos.data()) = value_cast<float16_t>(value);
            return value;
        }
#define HANDLE_TYPE(TYPE)                                                         \
    if (buffer.type() == Runtime::Buffer<TYPE>::static_halide_type()) {           \
        return py::cast(buffer.as<TYPE>()(pos.data()) = value_cast<TYPE>(value)); \
    }
        HANDLE_TYPE(bool)
        HANDLE_TYPE(uint8_t)
        HANDLE_TYPE(uint16_t)
        HANDLE_TYPE(uint32_t)
        HANDLE_TYPE(uint64_t)
        HANDLE_TYPE(int8_t)
        HANDLE_TYPE(int16_t)
        HANDLE_TYPE(int32_t)
        HANDLE_TYPE(int64_t)
        HANDLE_TYPE(float)
        HANDLE_TYPE(double)
#undef HANDLE_TYPE
        throw py::value_error("Unsupported Buffer<> type.");
    }

    void fill(const py::object &value) {
        RuntimeBuffer &buffer = runtime_buffer();
#define HANDLE_TYPE(TYPE)                                               \
    if (buffer.type() == Runtime::Buffer<TYPE>::static_halide_type()) { \
        buffer.as<TYPE>().fill(value_cast<TYPE>(value));                \
        return;                                                         \
    }
        HANDLE_TYPE(bool)
        HANDLE_TYPE(uint8_t)
        HANDLE_TYPE(uint16_t)
        HANDLE_TYPE(uint32_t)
        HANDLE_TYPE(uint64_t)
        HANDLE_TYPE(int8_t)
        HANDLE_TYPE(int16_t)
        HANDLE_TYPE(int32_t)
        HANDLE_TYPE(int64_t)
        HANDLE_TYPE(float16_t)
        HANDLE_TYPE(float)
        HANDLE_TYPE(double)
#undef HANDLE_TYPE
        throw py::value_error("Unsupported Buffer<> type.");
    }

    bool all_equal(const py::object &value) {
        RuntimeBuffer &buffer = runtime_buffer();
#define HANDLE_TYPE(TYPE)                                               \
    if (buffer.type() == Runtime::Buffer<TYPE>::static_halide_type()) { \
        return buffer.as<TYPE>().all_equal(value_cast<TYPE>(value));    \
    }
        HANDLE_TYPE(bool)
        HANDLE_TYPE(uint8_t)
        HANDLE_TYPE(uint16_t)
        HANDLE_TYPE(uint32_t)
        HANDLE_TYPE(uint64_t)
        HANDLE_TYPE(int8_t)
        HANDLE_TYPE(int16_t)
        HANDLE_TYPE(int32_t)
        HANDLE_TYPE(int64_t)
        HANDLE_TYPE(float16_t)
        HANDLE_TYPE(float)
        HANDLE_TYPE(double)
#undef HANDLE_TYPE
        throw py::value_error("Unsupported Buffer<> type.");
    }

private:
    void require_defined() const {
        if (!defined_) {
            throw py::value_error("Undefined buffer");
        }
    }

    void check_position(const std::vector<int> &pos) const {
        require_defined();
        const RuntimeBuffer &buffer = runtime_buffer();
        if (pos.size() != static_cast<size_t>(buffer.dimensions())) {
            throw py::value_error("Incorrect number of dimensions.");
        }
        for (int i = 0; i < buffer.dimensions(); ++i) {
            const auto dim = buffer.dim(i);
            if (pos[i] < dim.min() || pos[i] > dim.max()) {
                std::ostringstream out;
                out << "index " << pos[i] << " is out of bounds for axis " << i
                    << " with min=" << dim.min() << ", extent=" << dim.extent();
                throw py::index_error(out.str());
            }
        }
    }

    RuntimeBuffer buffer_;
    RuntimeBuffer *borrowed_buffer_ = nullptr;
    py::object owner_;
    std::shared_ptr<char> identity_;
    std::string name_;
    bool defined_ = false;
};

}  // namespace

void define_buffer(py::module_ &m) {
    py::class_<halide_type_t>(m, "Type")
        .def(py::init([](int code, int bits) {
                 return halide_type_t(static_cast<halide_type_code_t>(code), static_cast<uint8_t>(bits));
             }),
             py::arg("code"), py::arg("bits"))
        .def("code", [](const halide_type_t &type) { return static_cast<int>(type.code); })
        .def("bits", [](const halide_type_t &type) { return type.bits; })
        .def("__eq__", [](const halide_type_t &a, const py::object &other) {
            return py::isinstance<halide_type_t>(other) && a == other.cast<halide_type_t>();
        })
        .def("__str__", &type_string)
        .def("__repr__", &type_string);

    py::class_<BufferDimension>(m, "BufferDimension")
        .def("min", &BufferDimension::min)
        .def("stride", &BufferDimension::stride)
        .def("extent", &BufferDimension::extent)
        .def("max", &BufferDimension::max);

    py::class_<Buffer>(m, "Buffer", py::buffer_protocol())
        .def(py::init<>())
        .def(py::init<const Buffer &>(), py::keep_alive<1, 2>())
        .def(py::init<py::buffer, std::string, bool>(), py::arg("buffer"), py::arg("name") = "", py::arg("reverse_axes") = true)
        .def(py::init<halide_type_t, const std::vector<int> &, std::string>(),
             py::arg("type"), py::arg("sizes"), py::arg("name") = "")
        .def(py::init<halide_type_t, const std::vector<int> &, const std::vector<int> &, std::string>(),
             py::arg("type"), py::arg("sizes"), py::arg("storage_order"), py::arg("name") = "")
        .def_static("_from_borrowed", &Buffer::from_borrowed,
                    py::arg("capsule"), py::arg("owner"), py::arg("name"))
        .def_static("make_bounds_query", &Buffer::make_bounds_query, py::arg("type"), py::arg("sizes"), py::arg("name") = "")
        .def_static("make_scalar", &Buffer::make_scalar, py::arg("type"), py::arg("name") = "")
        .def_static("make_interleaved", &Buffer::make_interleaved, py::arg("type"), py::arg("width"), py::arg("height"), py::arg("channels"), py::arg("name") = "")
        .def_static("make_with_shape_of", &Buffer::make_with_shape_of, py::arg("src"), py::arg("name") = "")
        .def_buffer([](Buffer &b) { return b.buffer_info(true); })
        .def("numpy_view", &Buffer::numpy_view, py::arg("reverse_axes") = true)
        .def("set_name", &Buffer::set_name)
        .def("name", &Buffer::name)
        .def("defined", &Buffer::defined)
        .def("same_as", &Buffer::same_as, py::arg("other"))
        .def("type", [](const Buffer &b) { return b.runtime_buffer().type(); })
        .def("channels", [](const Buffer &b) { return b.runtime_buffer().channels(); })
        .def("dimensions", [](const Buffer &b) { return b.runtime_buffer().dimensions(); })
        .def("width", [](const Buffer &b) { return b.runtime_buffer().width(); })
        .def("height", [](const Buffer &b) { return b.runtime_buffer().height(); })
        .def("top", [](const Buffer &b) { return b.runtime_buffer().top(); })
        .def("bottom", [](const Buffer &b) { return b.runtime_buffer().bottom(); })
        .def("left", [](const Buffer &b) { return b.runtime_buffer().left(); })
        .def("right", [](const Buffer &b) { return b.runtime_buffer().right(); })
        .def("number_of_elements", [](const Buffer &b) { return b.runtime_buffer().number_of_elements(); })
        .def("size_in_bytes", [](const Buffer &b) { return b.runtime_buffer().size_in_bytes(); })
        .def("has_device_allocation", [](const Buffer &b) { return b.runtime_buffer().has_device_allocation(); })
        .def("host_dirty", [](const Buffer &b) { return b.runtime_buffer().host_dirty(); })
        .def("device_dirty", [](const Buffer &b) { return b.runtime_buffer().device_dirty(); })
        .def("set_host_dirty", [](Buffer &b, bool dirty) { b.runtime_buffer().set_host_dirty(dirty); }, py::arg("dirty") = true)
        .def("set_device_dirty", [](Buffer &b, bool dirty) { b.runtime_buffer().set_device_dirty(dirty); }, py::arg("dirty") = true)
        .def("copy", [](const Buffer &b) { return Buffer::from_runtime(b.runtime_buffer().copy(), unique_name(), true); })
        .def("copy_from", [](Buffer &b, const Buffer &other) { b.runtime_buffer().copy_from(other.runtime_buffer()); })
        .def("reverse_axes", [](const Buffer &b) {
            std::vector<int> order;
            for (int i = b.runtime_buffer().dimensions() - 1; i >= 0; --i) {
                order.push_back(i);
            }
            return b.view(b.runtime_buffer().transposed(order)); })
        .def("add_dimension", [](Buffer &b) { b.runtime_buffer().add_dimension(); })
        .def("allocate", [](Buffer &b) { b.runtime_buffer().allocate(nullptr, nullptr); })
        .def("deallocate", [](Buffer &b) { b.runtime_buffer().deallocate(); })
        .def("device_deallocate", [](Buffer &b) { b.runtime_buffer().device_deallocate(); })
        .def("crop", [](Buffer &b, int d, int min, int extent) { b.runtime_buffer().crop(d, min, extent); }, py::arg("dimension"), py::arg("min"), py::arg("extent"))
        .def("crop", [](Buffer &b, const std::vector<std::pair<int, int>> &rect) { b.runtime_buffer().crop(rect); }, py::arg("rect"))
        .def("cropped", [](const Buffer &b, int d, int min, int extent) { return b.view(b.runtime_buffer().cropped(d, min, extent)); })
        .def("cropped", [](const Buffer &b, const std::vector<std::pair<int, int>> &rect) { return b.view(b.runtime_buffer().cropped(rect)); })
        .def("embed", [](Buffer &b, int d, int pos) { b.runtime_buffer().embed(d, pos); })
        .def("embed", [](Buffer &b, int d) { b.runtime_buffer().embed(d); })
        .def("embedded", [](const Buffer &b, int d, int pos) { return b.view(b.runtime_buffer().embedded(d, pos)); })
        .def("embedded", [](const Buffer &b, int d) { return b.view(b.runtime_buffer().embedded(d)); })
        .def("slice", [](Buffer &b, int d, int pos) { b.runtime_buffer().slice(d, pos); })
        .def("slice", [](Buffer &b, int d) { b.runtime_buffer().slice(d); })
        .def("sliced", [](const Buffer &b, int d, int pos) { return b.view(b.runtime_buffer().sliced(d, pos)); })
        .def("sliced", [](const Buffer &b, int d) { return b.view(b.runtime_buffer().sliced(d)); })
        .def("translate", [](Buffer &b, int d, int dx) { b.runtime_buffer().translate(d, dx); })
        .def("translate", [](Buffer &b, const std::vector<int> &delta) { b.runtime_buffer().translate(delta); })
        .def("translated", [](const Buffer &b, int d, int dx) { return b.view(b.runtime_buffer().translated(d, dx)); })
        .def("translated", [](const Buffer &b, const std::vector<int> &delta) { return b.view(b.runtime_buffer().translated(delta)); })
        .def("transpose", [](Buffer &b, int d1, int d2) { b.runtime_buffer().transpose(d1, d2); })
        .def("transpose", [](Buffer &b, const std::vector<int> &order) { b.runtime_buffer().transpose(order); })
        .def("transposed", [](const Buffer &b, int d1, int d2) { return b.view(b.runtime_buffer().transposed(d1, d2)); })
        .def("transposed", [](const Buffer &b, const std::vector<int> &order) { return b.view(b.runtime_buffer().transposed(order)); })
        .def("dim", [](Buffer &b, int dimension) { return b.runtime_buffer().dim(dimension); }, py::keep_alive<0, 1>())
        .def("for_each_element", [](Buffer &b, const py::function &f) {
            std::vector<int> position(b.runtime_buffer().dimensions());
            b.runtime_buffer().for_each_element([&](const int *pos) {
                std::copy(pos, pos + position.size(), position.begin());
                f(position);
            }); })
        .def("fill", &Buffer::fill)
        .def("all_equal", &Buffer::all_equal)
        .def("copy_to_host", [](Buffer &b) { return b.runtime_buffer().copy_to_host(nullptr); })
        .def("device_detach_native", [](Buffer &b) { return b.runtime_buffer().device_detach_native(nullptr); })
        .def("device_free", [](Buffer &b) { return b.runtime_buffer().device_free(nullptr); })
        .def("device_sync", [](Buffer &b) { return b.runtime_buffer().device_sync(nullptr); })
        .def("set_min", [](Buffer &b, const std::vector<int> &mins) {
            if (mins.size() > static_cast<size_t>(b.runtime_buffer().dimensions())) {
                throw py::value_error("Too many arguments");
            }
            b.runtime_buffer().set_min(mins); })
        .def("contains", [](const Buffer &b, const std::vector<int> &coords) {
            if (coords.size() > static_cast<size_t>(b.runtime_buffer().dimensions())) {
                throw py::value_error("Too many arguments");
            }
            return b.runtime_buffer().contains(coords); })
        .def("__getitem__", [](Buffer &b, int pos) { return b.getitem({pos}); })
        .def("__getitem__", [](Buffer &b, const std::vector<int> &pos) { return b.getitem(pos); })
        .def("__setitem__", [](Buffer &b, int pos, const py::object &value) { return b.setitem({pos}, value); })
        .def("__setitem__", [](Buffer &b, const std::vector<int> &pos, const py::object &value) { return b.setitem(pos, value); })
        .def("__repr__", [](const Buffer &b) {
            if (!b.defined()) {
                return std::string("<undefined halide.runtime.Buffer>");
            }
            std::ostringstream out;
            out << "<halide.runtime.Buffer of type " << b.runtime_buffer().type() << " shape:[";
            for (int i = 0; i < b.runtime_buffer().dimensions(); ++i) {
                if (i) {
                    out << ",";
                }
                const auto d = b.runtime_buffer().raw_buffer()->dim[i];
                out << "[" << d.min << "," << d.extent << "," << d.stride << "]";
            }
            return out.str() + "]>"; })
        .def("_get_halide_buffer_t_capsule", &Buffer::halide_buffer_t_capsule)
        .def("_get_halide_runtime_buffer_capsule", &Buffer::runtime_buffer_capsule);
}

}  // namespace Halide::PythonRuntimeBindings
