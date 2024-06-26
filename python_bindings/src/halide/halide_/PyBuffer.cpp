#include "PyBuffer.h"

#include <utility>

#include "PyFunc.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

namespace {

// Standard stream output for halide_dimension_t
std::ostream &operator<<(std::ostream &stream, const halide_dimension_t &d) {
    stream << "[" << d.min << "," << d.extent << "," << d.stride << "]";
    return stream;
}

// Standard stream output for vector<halide_dimension_t>
std::ostream &operator<<(std::ostream &stream, const std::vector<halide_dimension_t> &shape) {
    stream << "[";
    bool need_comma = false;
    for (const auto &d : shape) {
        if (need_comma) {
            stream << ",";
        }
        stream << d;
        need_comma = true;
    }
    stream << "]";
    return stream;
}

// Given a Buffer<>, return its shape in the form of a vector<halide_dimension_t>.
// (Oddly, Buffer<> has no API to do this directly.)
std::vector<halide_dimension_t> get_buffer_shape(const Buffer<> &b) {
    if (!b.defined()) {
        // Return an empty vector if the buffer is not defined.
        return {};
    }
    std::vector<halide_dimension_t> s;
    for (int i = 0; i < b.dimensions(); ++i) {
        s.push_back(b.raw_buffer()->dim[i]);
    }
    return s;
}

// PyBind11 doesn't have baked-in support for float16, so add just
// enough special-case wrapping to support it.
template<typename T>
inline T value_cast(const py::object &value) {
    return value.cast<T>();
}

template<>
inline float16_t value_cast<float16_t>(const py::object &value) {
    return float16_t(value.cast<double>());
}

// TODO: https://github.com/halide/Halide/issues/6849
// template<>
// inline bfloat16_t value_cast<bfloat16_t>(const py::object &value) {
//     return bfloat16_t(value.cast<double>());
// }

template<typename T>
inline std::string format_descriptor() {
    return py::format_descriptor<T>::format();
}

template<>
inline std::string format_descriptor<float16_t>() {
    return "e";
}

// TODO: https://github.com/halide/Halide/issues/6849
// template<>
// inline std::string format_descriptor<bfloat16_t>() {
//     return there-is-no-python-buffer-format-descriptor-for-bfloat16;
// }

void call_fill(Buffer<> &b, const py::object &value) {

#define HANDLE_BUFFER_TYPE(TYPE)                    \
    if (b.type() == type_of<TYPE>()) {              \
        b.as<TYPE>().fill(value_cast<TYPE>(value)); \
        return;                                     \
    }

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
}

bool call_all_equal(Buffer<> &b, const py::object &value) {

#define HANDLE_BUFFER_TYPE(TYPE)                                \
    if (b.type() == type_of<TYPE>()) {                          \
        return b.as<TYPE>().all_equal(value_cast<TYPE>(value)); \
    }

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return false;
}

std::string type_to_format_descriptor(const Type &type) {

#define HANDLE_BUFFER_TYPE(TYPE) \
    if (type == type_of<TYPE>()) return format_descriptor<TYPE>();

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return std::string();
}

void check_out_of_bounds(Buffer<> &buf, const std::vector<int> &pos) {
    const int d = buf.dimensions();
    if ((size_t)pos.size() != (size_t)d) {
        throw py::value_error("Incorrect number of dimensions.");
    }
    for (int i = 0; i < d; i++) {
        const auto &dim = buf.dim(i);
        if (pos[i] < dim.min() || pos[i] > dim.max()) {
            // Try to mimic the wording of similar errors in NumPy
            std::ostringstream o;
            o << "index " << pos[i] << " is out of bounds for axis " << i << " with min=" << dim.min() << ", extent=" << dim.extent();
            throw py::index_error(o.str());
        }
    }
}

}  // namespace

Type format_descriptor_to_type(const std::string &fd) {

#define HANDLE_BUFFER_TYPE(TYPE) \
    if (fd == format_descriptor<TYPE>()) return type_of<TYPE>();

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    // The string 'l' corresponds to np.int_, which is essentially
    // a C 'long'; return a 32 or 64 bit int as appropriate.
    if (fd == "l") {
        return sizeof(long) == 8 ? type_of<int64_t>() : type_of<int32_t>();
    }

    throw py::value_error("Unsupported Buffer<> type.");
    return Type();
}

py::object buffer_getitem_operator(Buffer<> &buf, const std::vector<int> &pos) {
    check_out_of_bounds(buf, pos);

#define HANDLE_BUFFER_TYPE(TYPE)       \
    if (buf.type() == type_of<TYPE>()) \
        return py::cast(buf.as<TYPE>()(pos.data()));

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return py::object();
}

namespace {

py::object buffer_setitem_operator(Buffer<> &buf, const std::vector<int> &pos, const py::object &value) {
    check_out_of_bounds(buf, pos);

#define HANDLE_BUFFER_TYPE(TYPE)       \
    if (buf.type() == type_of<TYPE>()) \
        return py::cast(buf.as<TYPE>()(pos.data()) = value_cast<TYPE>(value));

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return py::object();
}

// Use an alias class so that if we are created via a py::buffer, we can
// keep the py::buffer_info class alive for the life of the Buffer<>,
// ensuring the data isn't collected out from under us.
class PyBuffer : public Buffer<> {
    py::buffer_info info;

    PyBuffer(py::buffer_info &&info, const std::string &name, bool reverse_axes)
        : Buffer<>(pybufferinfo_to_halidebuffer(info, reverse_axes), name),
          info(std::move(info)) {
    }

public:
    PyBuffer()
        : Buffer<>(), info() {
    }

    explicit PyBuffer(const Buffer<> &b)
        : Buffer<>(b), info() {
    }

    PyBuffer(const py::buffer &buffer, const std::string &name, bool reverse_axes)
        : PyBuffer(buffer.request(/*writable*/ true), name, reverse_axes) {
        // Default to setting host-dirty on any PyBuffer we create from an existing py::buffer;
        // this allows (e.g.) code like
        //
        //     input = hl.Buffer(imageio.imread(image_path))
        //
        // to work as you'd expected when the buffer is used for input. (Without
        // host_dirty being set, copying to GPU can be skipped and produce surprising
        // results.)
        //
        // Note that this is a bit different from the C++ Buffer<> ctors that
        // take pointer-to-existing data (which do *not* set host dirty); a crucial
        // difference there is that those also do not take ownership, but here,
        // we always take (shared) ownership of the underlying buffer.
        this->set_host_dirty();
    }

    ~PyBuffer() override = default;
};

py::buffer_info to_buffer_info(Buffer<> &b, bool reverse_axes = true) {
    if (b.data() == nullptr) {
        throw py::value_error("Cannot convert a Buffer<> with null host ptr to a Python buffer.");
    }

    const int d = b.dimensions();
    const int bytes = b.type().bytes();
    std::vector<Py_ssize_t> shape(d), strides(d);
    for (int i = 0; i < d; i++) {
        const int dst_axis = reverse_axes ? (d - i - 1) : i;
        shape[dst_axis] = (Py_ssize_t)b.raw_buffer()->dim[i].extent;
        strides[dst_axis] = (Py_ssize_t)b.raw_buffer()->dim[i].stride * (Py_ssize_t)bytes;
    }

    return py::buffer_info(
        b.data(),                             // Pointer to buffer
        bytes,                                // Size of one scalar
        type_to_format_descriptor(b.type()),  // Python struct-style format descriptor
        d,                                    // Number of dimensions
        shape,                                // Buffer dimensions
        strides                               // Strides (in bytes) for each index
    );
}

}  // namespace

void define_buffer(py::module &m) {
    using BufferDimension = Halide::Runtime::Buffer<>::Dimension;

    auto buffer_dimension_class =
        py::class_<BufferDimension>(m, "BufferDimension")
            .def("min", &BufferDimension::min)
            .def("stride", &BufferDimension::stride)
            .def("extent", &BufferDimension::extent)
            .def("max", &BufferDimension::max);

    auto buffer_class =
        py::class_<Buffer<>, PyBuffer>(m, "Buffer", py::buffer_protocol())

            // Note that this allows us to convert a Buffer<> to any buffer-like object in Python;
            // most notably, we can convert to an ndarray by calling numpy.array()
            .def_buffer([](Buffer<> &b) -> py::buffer_info {
                return to_buffer_info(b, /*reverse_axes*/ true);
            })

            // This allows us to use any buffer-like python entity to create a Buffer<>
            // (most notably, an ndarray)
            .def(py::init_alias<py::buffer, const std::string &, bool>(), py::arg("buffer"), py::arg("name") = "", py::arg("reverse_axes") = true)
            .def(py::init_alias<>())
            .def(py::init_alias<const Buffer<> &>())
            .def(py::init([](Type type, const std::vector<int> &sizes, const std::string &name) -> Buffer<> {
                     return Buffer<>(type, sizes, name);
                 }),
                 py::arg("type"), py::arg("sizes"), py::arg("name") = "")

            .def(py::init([](Type type, const std::vector<int> &sizes, const std::vector<int> &storage_order, const std::string &name) -> Buffer<> {
                     return Buffer<>(type, sizes, storage_order, name);
                 }),
                 py::arg("type"), py::arg("sizes"), py::arg("storage_order"), py::arg("name") = "")

            // Note that this exists solely to allow you to create a Buffer with a null host ptr;
            // this is necessary for some bounds-query operations (e.g. Func::infer_input_bounds).
            .def_static(
                "make_bounds_query", [](Type type, const std::vector<int> &sizes, const std::string &name) -> Buffer<> {
                    return Buffer<>(type, nullptr, sizes, name);
                },
                py::arg("type"), py::arg("sizes"), py::arg("name") = "")

            .def_static("make_scalar", (Buffer<>(*)(Type, const std::string &))Buffer<>::make_scalar, py::arg("type"), py::arg("name") = "")
            .def_static("make_interleaved", (Buffer<>(*)(Type, int, int, int, const std::string &))Buffer<>::make_interleaved, py::arg("type"), py::arg("width"), py::arg("height"), py::arg("channels"), py::arg("name") = "")
            .def_static(
                "make_with_shape_of", [](const Buffer<> &buffer, const std::string &name) -> Buffer<> {
                    return Buffer<>::make_with_shape_of(buffer, nullptr, nullptr, name);
                },
                py::arg("src"), py::arg("name") = "")

            .def("set_name", &Buffer<>::set_name)
            .def("name", &Buffer<>::name)

            .def("same_as", (bool(Buffer<>::*)(const Buffer<> &other) const) & Buffer<>::same_as, py::arg("other"))
            .def("defined", &Buffer<>::defined)

            .def("type", &Buffer<>::type)
            .def("channels", (int(Buffer<>::*)() const) & Buffer<>::channels)
            .def("dimensions", (int(Buffer<>::*)() const) & Buffer<>::dimensions)
            .def("width", (int(Buffer<>::*)() const) & Buffer<>::width)
            .def("height", (int(Buffer<>::*)() const) & Buffer<>::height)
            .def("top", (int(Buffer<>::*)() const) & Buffer<>::top)
            .def("bottom", (int(Buffer<>::*)() const) & Buffer<>::bottom)
            .def("left", (int(Buffer<>::*)() const) & Buffer<>::left)
            .def("right", (int(Buffer<>::*)() const) & Buffer<>::right)
            .def("number_of_elements", (size_t(Buffer<>::*)() const) & Buffer<>::number_of_elements)
            .def("size_in_bytes", (size_t(Buffer<>::*)() const) & Buffer<>::size_in_bytes)
            .def("has_device_allocation", (bool(Buffer<>::*)() const) & Buffer<>::has_device_allocation)
            .def("host_dirty", (bool(Buffer<>::*)() const) & Buffer<>::host_dirty)
            .def("device_dirty", (bool(Buffer<>::*)() const) & Buffer<>::device_dirty)

            .def(
                "set_host_dirty", [](Buffer<> &b, bool dirty) -> void {
                    b.set_host_dirty(dirty);
                },
                py::arg("dirty") = true)
            .def(
                "set_device_dirty", [](Buffer<> &b, bool dirty) -> void {
                    b.set_device_dirty(dirty);
                },
                py::arg("dirty") = true)

            .def("copy", &Buffer<>::copy)
            .def("copy_from", &Buffer<>::copy_from<void, Buffer<>::AnyDims>)
            .def("reverse_axes", [](Buffer<> &b) -> Buffer<> {
                const int d = b.dimensions();
                std::vector<int> order;
                for (int i = 0; i < b.dimensions(); i++) {
                    order.push_back(d - i - 1);
                }
                return b.transposed(order);
            })

            .def("add_dimension", (void(Buffer<>::*)()) & Buffer<>::add_dimension)

            .def("allocate", [](Buffer<> &b) -> void {
                b.allocate(nullptr, nullptr);
            })
            .def("deallocate", (void(Buffer<>::*)()) & Buffer<>::deallocate)
            .def("device_deallocate", (void(Buffer<>::*)()) & Buffer<>::device_deallocate)

            .def(
                "crop", [](Buffer<> &b, int d, int min, int extent) -> void {
                    b.crop(d, min, extent);
                },
                py::arg("dimension"), py::arg("min"), py::arg("extent"))
            .def(
                "crop", [](Buffer<> &b, const std::vector<std::pair<int, int>> &rect) -> void {
                    b.crop(rect);
                },
                py::arg("rect"))

            // Present in Runtime::Buffer but not Buffer
            // .def("cropped", [](Buffer<> &b, int d, int min, int extent) -> Buffer<> {
            //     return b.cropped(d, min, extent);
            // }, py::arg("dimension"), py::arg("min"), py::arg("extent"))
            // .def("cropped", [](Buffer<> &b, const std::vector<std::pair<int, int>> &rect) -> Buffer<> {
            //     return b.cropped(rect);
            // }, py::arg("rect"))

            .def(
                "embed", [](Buffer<> &b, int d, int pos) -> void {
                    b.embed(d, pos);
                },
                py::arg("dimension"), py::arg("pos"))
            .def(
                "embedded", [](Buffer<> &b, int d, int pos) -> Buffer<> {
                    return b.embedded(d, pos);
                },
                py::arg("dimension"), py::arg("pos"))

            .def(
                "embed", [](Buffer<> &b, int d) -> void {
                    b.embed(d);
                },
                py::arg("dimension"))
            .def(
                "embedded", [](Buffer<> &b, int d) -> Buffer<> {
                    return b.embedded(d);
                },
                py::arg("dimension"))

            .def(
                "slice", [](Buffer<> &b, int d, int pos) -> void {
                    b.slice(d, pos);
                },
                py::arg("dimension"), py::arg("pos"))
            .def(
                "sliced", [](Buffer<> &b, int d, int pos) -> Buffer<> {
                    return b.sliced(d, pos);
                },
                py::arg("dimension"), py::arg("pos"))

            .def(
                "slice", [](Buffer<> &b, int d) -> void {
                    b.slice(d);
                },
                py::arg("dimension"))
            .def(
                "sliced", [](Buffer<> &b, int d) -> Buffer<> {
                    return b.sliced(d);
                },
                py::arg("dimension"))

            .def(
                "translate", [](Buffer<> &b, int d, int dx) -> void {
                    b.translate(d, dx);
                },
                py::arg("dimension"), py::arg("dx"))
            .def(
                "translate", [](Buffer<> &b, const std::vector<int> &delta) -> void {
                    b.translate(delta);
                },
                py::arg("delta"))

            // Present in Runtime::Buffer but not Buffer
            // .def("translated", [](Buffer<> &b, int d, int dx) -> Buffer<> {
            //     return b.translated(d, dx);
            // }, py::arg("dimension"), py::arg("dx"))
            // .def("translated", [](Buffer<> &b, const std::vector<int> &delta) -> Buffer<> {
            //     return b.translated(delta);
            // }, py::arg("delta"))

            .def(
                "transpose", [](Buffer<> &b, int d1, int d2) -> void {
                    b.transpose(d1, d2);
                },
                py::arg("d1"), py::arg("d2"))

            .def(
                "transposed", [](Buffer<> &b, int d1, int d2) -> Buffer<> {
                    return b.transposed(d1, d2);
                },
                py::arg("d1"), py::arg("d2"))

            .def(
                "transpose", [](Buffer<> &b, const std::vector<int> &order) -> void {
                    b.transpose(order);
                },
                py::arg("order"))

            .def(
                "transposed", [](Buffer<> &b, const std::vector<int> &order) -> Buffer<> {
                    return b.transposed(order);
                },
                py::arg("order"))

            .def(
                "dim", [](Buffer<> &b, int dimension) -> BufferDimension {
                    return b.dim(dimension);
                },
                py::arg("dimension"), py::keep_alive<0, 1>()  // Keep Buffer<> alive while Dimension exists
                )

            .def(
                "for_each_element", [](Buffer<> &b, py::function f) -> void {
                    const int d = b.dimensions();
                    std::vector<int> pos_v(d, 0);
                    b.for_each_element([&f, &pos_v](const int *pos) -> void {
                        for (size_t i = 0; i < pos_v.size(); ++i) {
                            pos_v[i] = pos[i];
                        }
                        f(pos_v);
                    });
                },
                py::arg("f"))

            .def("fill", &call_fill, py::arg("value"))
            .def("all_equal", &call_all_equal, py::arg("value"))

            // TODO: for_each_value() needs to be rethought a bit for Python;
            // for C++ is passes a by-reference to each value (for mutation),
            // but Python doesn't allow mutable references to intrinsic types
            // like int. Leaving unimplemented for now.
            // .def("for_each_value", [](Buffer<> &b, py::args f, py::args other_buffers) -> void {
            // }, py::arg("f"))

            .def("copy_to_host", [](Buffer<> &b) -> int {
                return b.copy_to_host(nullptr);
            })
            .def("device_detach_native", [](Buffer<> &b) -> int {
                return b.device_detach_native(nullptr);
            })
            .def("device_free", [](Buffer<> &b) -> int {
                return b.device_free(nullptr);
            })
            .def("device_sync", [](Buffer<> &b) -> int {
                return b.device_sync(nullptr);
            })

            .def(
                "copy_to_device", [](Buffer<> &b, const Target &target) -> int {
                    return b.copy_to_device(to_jit_target(target));
                },
                py::arg("target") = Target())

            .def(
                "copy_to_device", [](Buffer<> &b, const DeviceAPI &d, const Target &target) -> int {
                    return b.copy_to_device(d, to_jit_target(target));
                },
                py::arg("device_api"), py::arg("target") = Target())
            .def(
                "device_malloc", [](Buffer<> &b, const Target &target) -> int {
                    return b.device_malloc(to_jit_target(target));
                },
                py::arg("target") = Target())

            .def(
                "device_malloc", [](Buffer<> &b, const DeviceAPI &d, const Target &target) -> int {
                    return b.device_malloc(d, to_jit_target(target));
                },
                py::arg("device_api"), py::arg("target") = Target())

            .def(
                "set_min", [](Buffer<> &b, const std::vector<int> &mins) -> void {
                    if (mins.size() > (size_t)b.dimensions()) {
                        throw py::value_error("Too many arguments");
                    }
                    b.set_min(mins);
                },
                py::arg("mins"))

            .def(
                "contains", [](Buffer<> &b, const std::vector<int> &coords) -> bool {
                    if (coords.size() > (size_t)b.dimensions()) {
                        throw py::value_error("Too many arguments");
                    }
                    return b.contains(coords);
                },
                py::arg("coords"))

            .def("__getitem__", [](Buffer<> &buf, const int &pos) -> py::object {
                return buffer_getitem_operator(buf, {pos});
            })
            .def("__getitem__", [](Buffer<> &buf, const std::vector<int> &pos) -> py::object {
                return buffer_getitem_operator(buf, pos);
            })

            .def("__getitem__", [](Buffer<> &buf, const Expr &pos) -> Expr {
                return buf(std::vector<Expr>{pos});
            })
            .def("__getitem__", [](Buffer<> &buf, const std::vector<Expr> &pos) -> Expr {
                return buf(pos);
            })

            .def("__setitem__", [](Buffer<> &buf, const int &pos, const py::object &value) -> py::object {
                return buffer_setitem_operator(buf, {pos}, value);
            })
            .def("__setitem__", [](Buffer<> &buf, const std::vector<int> &pos, const py::object &value) -> py::object {
                return buffer_setitem_operator(buf, pos, value);
            })

            .def("__repr__", [](const Buffer<> &b) -> std::string {
                std::ostringstream o;
                if (b.defined()) {
                    o << "<halide.Buffer of type " << halide_type_to_string(b.type()) << " shape:" << get_buffer_shape(b) << ">";
                } else {
                    o << "<undefined halide.Buffer>";
                }
                return o.str();
            });
}

}  // namespace PythonBindings
}  // namespace Halide
