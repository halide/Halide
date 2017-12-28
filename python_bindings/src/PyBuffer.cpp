#include "PyBuffer.h"

#include <functional>
#include <unordered_map>

#include "PyFunc.h"
#include "PyType.h"

#define USE_NUMPY
#ifdef USE_NUMPY
#ifdef USE_BOOST_NUMPY
#include <boost/numpy.hpp>
#else
#include "halide_numpy/numpy.hpp"
#endif
#endif  // USE_NUMPY

namespace Halide {
namespace PythonBindings {

#ifdef USE_NUMPY
#ifdef USE_BOOST_NUMPY
namespace bn = boost::numpy;
#else
namespace bn = Halide::numpy;
#endif
#endif  // USE_NUMPY

template <typename Ret, typename T, typename... Args>
Ret buffer_call_operator(Buffer<T> &that, Args... args) {
    return that(args...);
}

template <typename T>
Expr buffer_call_operator_tuple(Buffer<T> &that, py::tuple &args_passed) {
    std::vector<Expr> expr_args;
    for (ssize_t i = 0; i < py::len(args_passed); i++) {
        expr_args.push_back(py::extract<Expr>(args_passed[i]));
    }
    return that(expr_args);
}

template <typename T>
T buffer_to_setitem_operator0(Buffer<T> &that, int x, T value) {
    return that(x) = value;
}

template <typename T>
T buffer_to_setitem_operator1(Buffer<T> &that, int x, int y, T value) {
    return that(x, y) = value;
}

template <typename T>
T buffer_to_setitem_operator2(Buffer<T> &that, int x, int y, int z, T value) {
    return that(x, y, z) = value;
}

template <typename T>
T buffer_to_setitem_operator3(Buffer<T> &that, int x, int y, int z, int w, T value) {
    return that(x, y, z, w) = value;
}

template <typename T>
T buffer_to_setitem_operator4(Buffer<T> &that, py::tuple &args_passed, T value) {
    std::vector<int> int_args;
    const size_t args_len = py::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        py::object o = args_passed[i];
        py::extract<int> int32_extract(o);

        if (int32_extract.check()) {
            int_args.push_back(int32_extract());
        }
    }

    if (int_args.size() != args_len) {
        for (size_t j = 0; j < args_len; j += 1) {
            py::object o = args_passed[j];
            const std::string o_str = py::extract<std::string>(py::str(o));
            printf("buffer_to_setitem_operator4 args_passed[%lu] == %s\n", j, o_str.c_str());
        }
        throw std::invalid_argument("buffer_to_setitem_operator4 only handles "
                                    "a tuple of (convertible to) int.");
    }

    switch (int_args.size()) {
    case 1:
        return that(int_args[0]) = value;
    case 2:
        return that(int_args[0], int_args[1]) = value;
    case 3:
        return that(int_args[0], int_args[1], int_args[2]) = value;
    case 4:
        return that(int_args[0], int_args[1], int_args[2], int_args[3]) = value;
    default:
        printf("buffer_to_setitem_operator4 receive a tuple with %zu integers\n", int_args.size());
        throw std::invalid_argument("buffer_to_setitem_operator4 only handles 1 to 4 dimensional tuples");
    }

    return 0;  // this line should never be reached
}

template <typename T>
const T *buffer_data(const Buffer<T> &buffer) {
    return buffer.data();
}

template <typename T>
void buffer_set_min1(Buffer<T> &im, int m0) {
    im.set_min(m0);
}

template <typename T>
void buffer_set_min2(Buffer<T> &im, int m0, int m1) {
    im.set_min(m0, m1);
}

template <typename T>
void buffer_set_min3(Buffer<T> &im, int m0, int m1, int m2) {
    im.set_min(m0, m1, m2);
}

template <typename T>
void buffer_set_min4(Buffer<T> &im, int m0, int m1, int m2, int m3) {
    im.set_min(m0, m1, m2, m3);
}

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

template <typename T>
std::string buffer_repr(const Buffer<T> &buffer) {
    std::ostringstream o;
    o << "<halide.Buffer<" << halide_type_to_string(buffer.type()) << "> shape:" << get_buffer_shape(buffer) << ">";
    return o.str();
}

template <typename T>
py::object get_type_function_wrapper() {
    std::function<Type(Buffer<T> &)> return_type_func =
        [&](Buffer<T> &that) -> Type { return halide_type_of<T>(); };
    auto call_policies = py::default_call_policies();
    typedef boost::mpl::vector<Type, Buffer<T> &> func_sig;
    return py::make_function(return_type_func, call_policies, py::arg("self"), func_sig());
}

template <typename T>
void buffer_copy_to_host(Buffer<T> &im) {
    im.copy_to_host();
}

template <typename T>
void buffer_set_host_dirty(Buffer<T> &im, bool value) {
    im.set_host_dirty(value);
}

template <typename T>
int buffer_channels(Buffer<T> &im) {
    return im.channels();
}

template <typename T>
int buffer_width(Buffer<T> &im) {
    return im.width();
}

template <typename T>
int buffer_height(Buffer<T> &im) {
    return im.height();
}

template <typename T>
int buffer_dimensions(Buffer<T> &im) {
    return im.dimensions();
}

template <typename T>
int buffer_left(Buffer<T> &im) {
    return im.left();
}

template <typename T>
int buffer_right(Buffer<T> &im) {
    return im.right();
}

template <typename T>
int buffer_top(Buffer<T> &im) {
    return im.top();
}

template <typename T>
int buffer_bottom(Buffer<T> &im) {
    return im.bottom();
}

template <typename T>
int buffer_stride(Buffer<T> &im, int d) {
    return im.stride(d);
}

template <typename T>
int buffer_min(Buffer<T> &im, int d) {
    return im.min(d);
}

template <typename T>
int buffer_extent(Buffer<T> &im, int d) {
    return im.extent(d);
}



template <typename T>
void define_buffer_impl(const std::string suffix, const Type type) {
    auto buffer_class =
        py::class_<Buffer<T>>(
            ("Buffer" + suffix).c_str(),
            "A reference-counted handle on a dense multidimensional array "
            "containing scalar values of type T. Can be directly accessed and "
            "modified. May have up to four dimensions. Color images are "
            "represented as three-dimensional, with the third dimension being "
            "the color channel. In general we store color images in "
            "color-planes, as opposed to packed RGB, because this tends to "
            "vectorize more cleanly.",
            py::init<>(py::arg("self"), "Construct an undefined buffer handle"));

    // Constructors
    buffer_class
        .def(py::init<int>(
            py::args("self", "x"),
            "Allocate an buffer with the given dimensions."))

        .def(py::init<int, int>(
            py::args("self", "x", "y"),
            "Allocate an buffer with the given dimensions."))

        .def(py::init<int, int, int>(
            py::args("self", "x", "y", "z"),
            "Allocate an buffer with the given dimensions."))

        .def(py::init<int, int, int, int>(
            py::args("self", "x", "y", "z", "w"),
            "Allocate an buffer with the given dimensions."))

        .def(py::init<Realization &>(
            py::args("self", "r"),
            "Wrap a single-element realization in an Buffer object."))

        .def(py::init<halide_buffer_t>(
            py::args("self", "b"),
            "Wrap a halide_buffer_t in an Buffer object, so that we can access its pixels."));

    buffer_class
        .def("__repr__", buffer_repr<T>, py::arg("self"));

    buffer_class
        .def("data", buffer_data<T>, py::arg("self"),
             py::return_value_policy<py::return_opaque_pointer>(),  // not sure this will do what we want
             "Get a pointer to the element at the min location.")

        .def("copy_to_host", buffer_copy_to_host<T>, py::arg("self"),
             "Manually copy-back data to the host, if it's on a device. ")
        .def("set_host_dirty", buffer_set_host_dirty<T>,
             (py::arg("self"), py::arg("dirty") = true),
             "Mark the buffer as dirty-on-host. ")
        .def("type", get_type_function_wrapper<T>(),
             "Return Type instance for the data type of the buffer.")
        .def("channels", buffer_channels<T>, py::arg("self"),
             "Get the extent of dimension 2, which by convention we use as"
             "the number of color channels (often 3). Unlike extent(2), "
             "returns one if the buffer has fewer than three dimensions.")
        .def("dimensions", buffer_dimensions<T>, py::arg("self"),
             "Get the dimensionality of the data. Typically two for grayscale images, and three for color images.")
        .def("stride", buffer_stride<T>, py::args("self", "dim"),
             "Get the number of elements in the buffer between two adjacent "
             "elements in the given dimension. For example, the stride in "
             "dimension 0 is usually 1, and the stride in dimension 1 is "
             "usually the extent of dimension 0. This is not necessarily true though.")
        .def("extent", buffer_extent<T>, py::args("self", "dim"),
             "Get the size of a dimension.")
        .def("min", buffer_min<T>, py::args("self", "dim"),
             "Get the min coordinate of a dimension. The top left of the "
             "buffer represents this point in a function that was realized "
             "into this buffer.");

    buffer_class
        .def("set_min", buffer_set_min1<T>,
             py::args("self", "m0"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", buffer_set_min2<T>,
             py::args("self", "m0", "m1"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", buffer_set_min3<T>,
             py::args("self", "m0", "m1", "m2"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", buffer_set_min4<T>,
             py::args("self", "m0", "m1", "m2", "m3"),
             "Set the coordinates corresponding to the host pointer.");

    buffer_class
        .def("width", buffer_width<T>, py::arg("self"),
             "Get the extent of dimension 0, which by convention we use as "
             "the width of the image. Unlike extent(0), returns one if the "
             "buffer is zero-dimensional.")
        .def("height", buffer_height<T>, py::arg("self"),
             "Get the extent of dimension 1, which by convention we use as "
             "the height of the image. Unlike extent(1), returns one if the "
             "buffer has fewer than two dimensions.")
        .def("left", buffer_left<T>, py::arg("self"),
             "Get the minimum coordinate in dimension 0, which by convention "
             "is the coordinate of the left edge of the image. Returns zero "
             "for zero-dimensional images.")
        .def("right", buffer_right<T>, py::arg("self"),
             "Get the maximum coordinate in dimension 0, which by convention "
             "is the coordinate of the right edge of the image. Returns zero "
             "for zero-dimensional images.")
        .def("top", buffer_top<T>, py::arg("self"),
             "Get the minimum coordinate in dimension 1, which by convention "
             "is the top of the image. Returns zero for zero- or "
             "one-dimensional images.")
        .def("bottom", buffer_bottom<T>, py::arg("self"),
             "Get the maximum coordinate in dimension 1, which by convention "
             "is the bottom of the image. Returns zero for zero- or "
             "one-dimensional images.");

    const char *get_item_doc =
        "Construct an expression which loads from this buffer. ";

    // Access operators (to Expr, and to actual value)
    buffer_class
        .def("__getitem__", buffer_call_operator<Expr, T, Expr>,
             py::args("self", "x"),
             get_item_doc);
    buffer_class
        .def("__getitem__", buffer_call_operator<Expr, T, Expr, Expr>,
             py::args("self", "x", "y"),
             get_item_doc);
    buffer_class
        .def("__getitem__", buffer_call_operator<Expr, T, Expr, Expr, Expr>,
             py::args("self", "x", "y", "z"),
             get_item_doc)
        .def("__getitem__", buffer_call_operator<Expr, T, Expr, Expr, Expr, Expr>,
             py::args("self", "x", "y", "z", "w"),
             get_item_doc)
        .def("__getitem__", buffer_call_operator_tuple<T>,
             py::args("self", "tuple"),
             get_item_doc)
        // Note that we return copy values (not references like in the C++ API)
        .def("__getitem__", buffer_call_operator<T, T>,
             py::arg("self"),
             "Assuming this buffer is zero-dimensional, get its value")
        .def("__call__", buffer_call_operator<T, T, int>,
             py::args("self", "x"),
             "Assuming this buffer is one-dimensional, get the value of the element at position x")
        .def("__call__", buffer_call_operator<T, T, int, int>,
             py::args("self", "x", "y"),
             "Assuming this buffer is two-dimensional, get the value of the element at position (x, y)")
        .def("__call__", buffer_call_operator<T, T, int, int, int>,
             py::args("self", "x", "y", "z"),
             "Assuming this buffer is three-dimensional, get the value of the element at position (x, y, z)")
        .def("__call__", buffer_call_operator<T, T, int, int, int, int>,
             py::args("self", "x", "y", "z", "w"),
             "Assuming this buffer is four-dimensional, get the value of the element at position (x, y, z, w)")

        .def("__setitem__", buffer_to_setitem_operator0<T>, py::args("self", "x", "value"),
             "Assuming this buffer is one-dimensional, set the value of the element at position x")
        .def("__setitem__", buffer_to_setitem_operator1<T>, py::args("self", "x", "y", "value"),
             "Assuming this buffer is two-dimensional, set the value of the element at position (x, y)")
        .def("__setitem__", buffer_to_setitem_operator2<T>, py::args("self", "x", "y", "z", "value"),
             "Assuming this buffer is three-dimensional, set the value of the element at position (x, y, z)")
        .def("__setitem__", buffer_to_setitem_operator3<T>, py::args("self", "x", "y", "z", "w", "value"),
             "Assuming this buffer is four-dimensional, set the value of the element at position (x, y, z, w)")
        .def("__setitem__", buffer_to_setitem_operator4<T>, py::args("self", "tuple", "value"),
             "Assuming this buffer is one to four-dimensional, "
             "set the value of the element at position indicated by tuple (x, y, z, w)");

    py::implicitly_convertible<Buffer<T>, Argument>();
}

py::object buffer_to_python_object(const Buffer<> &im) {
    PyObject *obj = nullptr;
    if (im.type() == UInt(8)) {
        py::manage_new_object::apply<Buffer<uint8_t> *>::type converter;
        obj = converter(new Buffer<uint8_t>(im));
    } else if (im.type() == UInt(16)) {
        py::manage_new_object::apply<Buffer<uint16_t> *>::type converter;
        obj = converter(new Buffer<uint16_t>(im));
    } else if (im.type() == UInt(32)) {
        py::manage_new_object::apply<Buffer<uint32_t> *>::type converter;
        obj = converter(new Buffer<uint32_t>(im));
    } else if (im.type() == UInt(64)) {
        py::manage_new_object::apply<Buffer<uint64_t> *>::type converter;
        obj = converter(new Buffer<uint64_t>(im));
    } else if (im.type() == Int(8)) {
        py::manage_new_object::apply<Buffer<int8_t> *>::type converter;
        obj = converter(new Buffer<int8_t>(im));
    } else if (im.type() == Int(16)) {
        py::manage_new_object::apply<Buffer<int16_t> *>::type converter;
        obj = converter(new Buffer<int16_t>(im));
    } else if (im.type() == Int(32)) {
        py::manage_new_object::apply<Buffer<int32_t> *>::type converter;
        obj = converter(new Buffer<int32_t>(im));
    } else if (im.type() == Int(64)) {
        py::manage_new_object::apply<Buffer<int64_t> *>::type converter;
        obj = converter(new Buffer<int64_t>(im));
    } else if (im.type() == Float(32)) {
        py::manage_new_object::apply<Buffer<float> *>::type converter;
        obj = converter(new Buffer<float>(im));
    } else if (im.type() == Float(64)) {
        py::manage_new_object::apply<Buffer<double> *>::type converter;
        obj = converter(new Buffer<double>(im));
    } else {
        throw std::invalid_argument("buffer_to_python_object received an Buffer of unsupported type.");
    }

    return py::object(py::handle<>(obj));
}

Buffer<> python_object_to_buffer(py::object obj) {
    py::extract<Buffer<uint8_t>> buffer_extract_uint8(obj);
    py::extract<Buffer<uint16_t>> buffer_extract_uint16(obj);
    py::extract<Buffer<uint32_t>> buffer_extract_uint32(obj);
    py::extract<Buffer<int8_t>> buffer_extract_int8(obj);
    py::extract<Buffer<int16_t>> buffer_extract_int16(obj);
    py::extract<Buffer<int32_t>> buffer_extract_int32(obj);
    py::extract<Buffer<float>> buffer_extract_float(obj);
    py::extract<Buffer<double>> buffer_extract_double(obj);

    if (buffer_extract_uint8.check()) {
        return buffer_extract_uint8();
    } else if (buffer_extract_uint16.check()) {
        return buffer_extract_uint16();
    } else if (buffer_extract_uint32.check()) {
        return buffer_extract_uint32();
    } else if (buffer_extract_int8.check()) {
        return buffer_extract_int8();
    } else if (buffer_extract_int16.check()) {
        return buffer_extract_int16();
    } else if (buffer_extract_int32.check()) {
        return buffer_extract_int32();
    } else if (buffer_extract_float.check()) {
        return buffer_extract_float();
    } else if (buffer_extract_double.check()) {
        return buffer_extract_double();
    } else {
        throw std::invalid_argument("python_object_to_buffer received an object that is not an Buffer<T>");
    }
    return Buffer<>();
}

#ifdef USE_NUMPY

bn::dtype type_to_dtype(const Type &t) {
    if (t == UInt(8)) return bn::dtype::get_builtin<uint8_t>();
    if (t == UInt(16)) return bn::dtype::get_builtin<uint16_t>();
    if (t == UInt(32)) return bn::dtype::get_builtin<uint32_t>();
    if (t == UInt(64)) return bn::dtype::get_builtin<uint64_t>();
    if (t == Int(8)) return bn::dtype::get_builtin<int8_t>();
    if (t == Int(16)) return bn::dtype::get_builtin<int16_t>();
    if (t == Int(32)) return bn::dtype::get_builtin<int32_t>();
    if (t == Int(64)) return bn::dtype::get_builtin<int64_t>();
    if (t == Float(32)) return bn::dtype::get_builtin<float>();
    if (t == Float(64)) return bn::dtype::get_builtin<double>();
    throw std::runtime_error("type_to_dtype received a Halide::Type with no known numpy dtype equivalent");
    return bn::dtype::get_builtin<uint8_t>();
}

Type dtype_to_type(const bn::dtype &t) {
    if (t == bn::dtype::get_builtin<uint8_t>()) return UInt(8);
    if (t == bn::dtype::get_builtin<uint16_t>()) return UInt(16);
    if (t == bn::dtype::get_builtin<uint32_t>()) return UInt(32);
    if (t == bn::dtype::get_builtin<uint64_t>()) return UInt(64);
    if (t == bn::dtype::get_builtin<int8_t>()) return Int(8);
    if (t == bn::dtype::get_builtin<int16_t>()) return Int(16);
    if (t == bn::dtype::get_builtin<int32_t>()) return Int(32);
    if (t == bn::dtype::get_builtin<int64_t>()) return Int(64);
    if (t == bn::dtype::get_builtin<float>()) return Float(32);
    if (t == bn::dtype::get_builtin<double>()) return Float(64);
    throw std::runtime_error("dtype_to_type received a numpy type with no known Halide type equivalent");
    return Type();
}

/// Will create a Halide::Buffer object pointing to the array data
py::object ndarray_to_buffer(bn::ndarray &array) {
    Type t = dtype_to_type(array.get_dtype());
    const int dims = array.get_nd();
    void *host = reinterpret_cast<void *>(array.get_data());
    halide_dimension_t *shape =
        (halide_dimension_t *)__builtin_alloca(sizeof(halide_dimension_t) * dims);
    for (int i = 0; i < dims; i++) {
        shape[i].min = 0;
        shape[i].extent = array.shape(i);
        shape[i].stride = array.strides(i) / t.bytes();
    }

    return buffer_to_python_object(Buffer<>(t, host, dims, shape));
}

bn::ndarray buffer_to_ndarray(py::object buffer_object) {
    Buffer<> im = python_object_to_buffer(buffer_object);

    if (im.data() == nullptr) {
        throw std::invalid_argument("Can't create a numpy array from a Buffer with a null host pointer");
    }

    std::vector<int32_t> extent(im.dimensions()), stride(im.dimensions());
    for (int i = 0; i < im.dimensions(); i++) {
        extent[i] = im.dim(i).extent();
        stride[i] = im.dim(i).stride() * im.type().bytes();
    }

    return bn::from_data(
        im.data(),
        type_to_dtype(im.type()),
        extent,
        stride,
        buffer_object);
}

#endif  // USE_NUMPY

struct BufferFactory {
    template <typename T, typename... Args>
    static py::object create_buffer_object(Args... args) {
        typedef Buffer<T> BufferType;
        typedef typename py::manage_new_object::apply<BufferType *>::type converter_t;
        converter_t converter;
        PyObject *obj = converter(new BufferType(args...));
        return py::object(py::handle<>(obj));
    }

    template <typename... Args>
    static py::object create_buffer_impl(Type t, Args... args) {
        if (t == UInt(8)) return create_buffer_object<uint8_t>(args...);
        if (t == UInt(16)) return create_buffer_object<uint16_t>(args...);
        if (t == UInt(32)) return create_buffer_object<uint32_t>(args...);
        if (t == UInt(64)) return create_buffer_object<uint64_t>(args...);
        if (t == Int(8)) return create_buffer_object<int8_t>(args...);
        if (t == Int(16)) return create_buffer_object<int16_t>(args...);
        if (t == Int(32)) return create_buffer_object<int32_t>(args...);
        if (t == Int(64)) return create_buffer_object<int64_t>(args...);
        if (t == Float(32)) return create_buffer_object<float>(args...);
        if (t == Float(64)) return create_buffer_object<double>(args...);
        throw std::invalid_argument("BufferFactory::create_buffer_impl received type not handled");
        return py::object();
    }

    static py::object create_buffer0(Type type) {
        return create_buffer_impl(type);
    }

    static py::object create_buffer1(Type type, int x) {
        return create_buffer_impl(type, x);
    }

    static py::object create_buffer2(Type type, int x, int y) {
        return create_buffer_impl(type, x, y);
    }

    static py::object create_buffer3(Type type, int x, int y, int z) {
        return create_buffer_impl(type, x, y, z);
    }

    static py::object create_buffer4(Type type, int x, int y, int z, int w) {
        return create_buffer_impl(type, x, y, z, w);
    }

    static py::object create_buffer_from_realization(Type type, Realization &r) {
        return create_buffer_impl(type, r);
    }

    static py::object create_buffer_from_buffer(halide_buffer_t b) {
        return create_buffer_impl(b.type, b);
    }
};

void define_buffer() {
    define_buffer_impl<uint8_t>("_uint8", UInt(8));
    define_buffer_impl<uint16_t>("_uint16", UInt(16));
    define_buffer_impl<uint32_t>("_uint32", UInt(32));
    define_buffer_impl<uint64_t>("_uint64", UInt(64));

    define_buffer_impl<int8_t>("_int8", Int(8));
    define_buffer_impl<int16_t>("_int16", Int(16));
    define_buffer_impl<int32_t>("_int32", Int(32));
    define_buffer_impl<int64_t>("_int64", Int(64));

    define_buffer_impl<float>("_float32", Float(32));
    define_buffer_impl<double>("_float64", Float(64));

    // "Buffer" will look like a class, but instead it will be simply a factory method
    py::def("Buffer", &BufferFactory::create_buffer0,
           py::args("type"),
           "Construct a zero-dimensional buffer of type T");
    py::def("Buffer", &BufferFactory::create_buffer1,
           py::args("type", "x"),
           "Construct a one-dimensional buffer of type T");
    py::def("Buffer", &BufferFactory::create_buffer2,
           py::args("type", "x", "y"),
           "Construct a two-dimensional buffer of type T");
    py::def("Buffer", &BufferFactory::create_buffer3,
           py::args("type", "x", "y", "z"),
           "Construct a three-dimensional buffer of type T");
    py::def("Buffer", &BufferFactory::create_buffer4,
           py::args("type", "x", "y", "z", "w"),
           "Construct a four-dimensional buffer of type T");

    py::def("Buffer", &BufferFactory::create_buffer_from_realization,
           py::args("type", "r"),
           py::with_custodian_and_ward_postcall<0, 2>(),  // the realization reference count is increased
           "Wrap a single-element realization in an Buffer object of type T.");

    py::def("Buffer", &BufferFactory::create_buffer_from_buffer,
           py::args("b"),
           py::with_custodian_and_ward_postcall<0, 2>(),  // the buffer_t reference count is increased
           "Wrap a halide_buffer_t in an Buffer object, so that we can access its pixels.");

#ifdef USE_NUMPY
    bn::initialize();

    py::def("ndarray_to_buffer", &ndarray_to_buffer,
           py::args("array"),
           py::with_custodian_and_ward_postcall<0, 1>(),  // the array reference count is increased
           "Converts a numpy array into a Halide::Buffer."
           "Will take into account the array size, dimensions, and type."
           "Created Buffer refers to the array data (no copy).");

    py::def("Buffer", &ndarray_to_buffer,
           py::args("array"),
           py::with_custodian_and_ward_postcall<0, 1>(),  // the array reference count is increased
           "Wrap numpy array in a Halide::Buffer."
           "Will take into account the array size, dimensions, and type."
           "Created Buffer refers to the array data (no copy).");

    py::def("buffer_to_ndarray", &buffer_to_ndarray,
           py::args("buffer"),
           py::with_custodian_and_ward_postcall<0, 1>(),  // the buffer reference count is increased
           "Creates a numpy array from a Halide::Buffer."
           "Will take into account the Buffer size, dimensions, and type."
           "Created ndarray refers to the Buffer data (no copy).");
#endif  // USE_NUMPY
}

}  // namespace PythonBindings
}  // namespace Halide
