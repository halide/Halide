#include "PyBuffer.h"

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
    for (auto &d : shape) {
        if (need_comma) {
            stream << ',';
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
    std::vector<halide_dimension_t> s;
    for (int i = 0; i < b.dimensions(); ++i) {
        s.push_back(b.raw_buffer()->dim[i]);
    }
    return s;
}

std::string type_to_format_descriptor(const Type &type) {

    #define HANDLE_BUFFER_TYPE(TYPE) \
        if (type == type_of<TYPE>()) return py::format_descriptor<TYPE>::format();

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

    #undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return std::string();
}

Type format_descriptor_to_type(const std::string &fd) {

    #define HANDLE_BUFFER_TYPE(TYPE) \
        if (fd == py::format_descriptor<TYPE>::format()) return type_of<TYPE>();

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

    #undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return Type();
}


py::object buffer_getitem_operator(Buffer<> &buf, const std::vector<int> &pos) {
    if ((size_t) pos.size() != (size_t) buf.dimensions()) {
        // TODO improve error
        throw py::value_error("Incorrect number of dimensions.");
    }

    #define HANDLE_BUFFER_TYPE(TYPE) \
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
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

    #undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return py::object();
}

py::object buffer_setitem_operator(Buffer<> &buf, const std::vector<int> &pos, py::object value) {
    if ((size_t) pos.size() != (size_t) buf.dimensions()) {
        // TODO improve error
        throw py::value_error("Incorrect number of dimensions.");
    }
    #define HANDLE_BUFFER_TYPE(TYPE) \
        if (buf.type() == type_of<TYPE>()) \
            return py::cast(buf.as<TYPE>()(pos.data()) = value.cast<TYPE>());

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

    #undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return py::object();
}

}  // namespace

void define_buffer(py::module &m) {
    auto buffer_class =
        py::class_<Buffer<>>(m, "Buffer", py::buffer_protocol())

        // This allows us to use any buffer-like python entity to create a Buffer<>
        // (most notably, an ndarray)
        .def(py::init([](py::buffer buffer, const std::string &name) -> Buffer<> {
            const py::buffer_info info = buffer.request();
            const Type t = format_descriptor_to_type(info.format);

            std::vector<halide_dimension_t> dims;
            dims.reserve(info.ndim);
            for (int i = 0; i < info.ndim; i++) {
                // TODO: check for ssize_t -> int32_t overflow
                dims.push_back({0, (int32_t) info.shape[i], (int32_t) (info.strides[i] / t.bytes())});
            }

            // Note that this does NOT make a copy of the data; it deliberately
            // shares the pointer with the incoming buffer.
            return Buffer<>(t, info.ptr, (int) info.ndim, dims.data(), name);
        }), py::arg("buffer"), py::arg("name") = "")

        // TODO replace with py::args version
        .def(py::init<>())
        .def(py::init<int>())
        .def(py::init<int, int>())
        .def(py::init<int, int, int>())
        .def(py::init<int, int, int, int>())

        // TODO replace with py::args version
        // .def(py::init<Type>())  -- C++ API missing
        .def(py::init<Type, int>())
        .def(py::init<Type, int, int>())
        .def(py::init<Type, int, int, int>())
        .def(py::init<Type, int, int, int, int>())

        .def(py::init<Type, int, int, int, int>())

        .def("set_name", &Buffer<>::set_name)
        .def("name", &Buffer<>::name)

        .def("type", &Buffer<>::type)
        .def("channels", (int (Buffer<>::*)() const) &Buffer<>::channels)
        .def("dimensions", (int (Buffer<>::*)() const) &Buffer<>::dimensions)
        .def("width", (int (Buffer<>::*)() const) &Buffer<>::width)
        .def("height", (int (Buffer<>::*)() const) &Buffer<>::height)
        .def("top", (int (Buffer<>::*)() const) &Buffer<>::top)
        .def("bottom", (int (Buffer<>::*)() const) &Buffer<>::bottom)

        .def("copy_to_host", [](Buffer<> &b) -> int {
            // TODO: do we need a way to specify context?
            return b.copy_to_host(nullptr);
        })

        .def("set_min", [](Buffer<> &b, py::args args) -> void {
            if (args.size() > (size_t) b.dimensions()) {
                throw py::value_error("Too many arguments");
            }
            b.set_min(args_to_vector<int>(args));
        })

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

        .def("__setitem__", [](Buffer<> &buf, const int &pos, py::object value) -> py::object {
            return buffer_setitem_operator(buf, {pos}, value);
        })
        .def("__setitem__", [](Buffer<> &buf, const std::vector<int> &pos, py::object value) -> py::object {
            return buffer_setitem_operator(buf, pos, value);
        })

        .def("__repr__", [](const Buffer<> &b) -> std::string {
            std::ostringstream o;
            o << "<halide.Buffer of type " << halide_type_to_string(b.type()) << " shape:" << get_buffer_shape(b) << ">";
            return o.str();
        })

        // Note that this allows us to convert a Buffer<> to any buffer-like object in Python;
        // most notably, we can convert to an ndarray by calling numpy.array()
       .def_buffer([](Buffer<> &b) -> py::buffer_info {
            const int d = b.dimensions();
            const int bytes = b.type().bytes();
            std::vector<ssize_t> shape, strides;
            for (int i = 0; i < d; i++) {
                shape.push_back((ssize_t) b.raw_buffer()->dim[i].extent);
                strides.push_back((ssize_t) (b.raw_buffer()->dim[i].stride * bytes));
            }
            return py::buffer_info(
                b.data(),                               // Pointer to buffer
                bytes,                                  // Size of one scalar
                type_to_format_descriptor(b.type()),    // Python struct-style format descriptor
                d,                                      // Number of dimensions
                shape,                                  // Buffer dimensions
                strides                                // Strides (in bytes) for each index
            );
        })
    ;
}

}  // namespace PythonBindings
}  // namespace Halide
