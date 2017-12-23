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

py::dtype type_to_dtype(const Type &t) {
    #define HANDLE_DTYPE(TYPE) if (t == type_of<TYPE>()) return py::dtype::of<TYPE>();
    HANDLE_DTYPE(bool)
    HANDLE_DTYPE(uint8_t)
    HANDLE_DTYPE(uint16_t)
    HANDLE_DTYPE(uint32_t)
    HANDLE_DTYPE(uint64_t)
    HANDLE_DTYPE(int8_t)
    HANDLE_DTYPE(int16_t)
    HANDLE_DTYPE(int32_t)
    HANDLE_DTYPE(int64_t)
    HANDLE_DTYPE(float)
    HANDLE_DTYPE(double)
    #undef HANDLE_DTYPE

    throw py::value_error("Unsupported ndarray type.");
    return py::dtype::of<uint8_t>();
}

Type dtype_to_type(const py::dtype &dtype) {
    #define HANDLE_DTYPE(TYPE) if (dtype.is(py::dtype::of<TYPE>())) return type_of<TYPE>();
    HANDLE_DTYPE(bool)
    HANDLE_DTYPE(uint8_t)
    HANDLE_DTYPE(uint16_t)
    HANDLE_DTYPE(uint32_t)
    HANDLE_DTYPE(uint64_t)
    HANDLE_DTYPE(int8_t)
    HANDLE_DTYPE(int16_t)
    HANDLE_DTYPE(int32_t)
    HANDLE_DTYPE(int64_t)
    HANDLE_DTYPE(float)
    HANDLE_DTYPE(double)
    #undef HANDLE_DTYPE

    throw py::value_error("Unsupported ndarray type.");
    return Type();
}

Buffer<> ndarray_to_buffer(const py::array &array, const std::string &name) {
    Type t = dtype_to_type(array.dtype());

    std::vector<halide_dimension_t> dims;
    dims.reserve(array.ndim());
    for (int i = 0; i < array.ndim(); i++) {
        // TODO: check for ssize_t -> int32_t overflow
        dims.push_back({0, (int32_t) array.shape(i), (int32_t) (array.strides(i) / t.bytes())});
    }

    // TODO: it should be possible to have the Buffer point at the ndarray's
    // memory, if we take care to ensure the ndarray lives, and the ndarray owns
    // the data in the first place. For now, we always copy.

    // tmp doesn't make a copy of the data, it just keeps a pointer.
    return Buffer<>(t, const_cast<void *>(array.data()), (int) array.ndim(), dims.data(), name).copy();
}

py::array buffer_to_ndarray(const Buffer<> &buf) {
    if (buf.data() == nullptr) {
        throw py::value_error("Buffers with null data should be impossible here.");
    }

    std::vector<ssize_t> extent, stride;
    for (int i = 0; i < buf.dimensions(); i++) {
        extent.push_back(buf.dim(i).extent());
        stride.push_back(buf.dim(i).stride() * buf.type().bytes());
    }

    // TODO: should be possible to avoid copying data in at least some instances
    // by passing a handle as the trailing arg. For now, just copy.
    return py::array(type_to_dtype(buf.type()), extent, stride, buf.data());
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
        py::class_<Buffer<>>(m, "Buffer")
        // TODO: do we want a Buffer(ndarray) ctor?
        .def(py::init(&ndarray_to_buffer), py::arg("ndarray"), py::arg("name") = "")

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

        // TODO: do we want this as an instance method?
        .def("to_ndarray", &buffer_to_ndarray)

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
            return buf({pos});
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
    ;

    // TODO: do we want these?
    m.def("ndarray_to_buffer", &ndarray_to_buffer, py::arg("ndarray"), py::arg("name") = "");
    m.def("buffer_to_ndarray", &buffer_to_ndarray, py::arg("buffer"));

}

}  // namespace PythonBindings
}  // namespace Halide
