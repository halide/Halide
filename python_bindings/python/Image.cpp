// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/format.hpp>
#include <boost/python.hpp>

#include "Image.h"

#define USE_NUMPY

#ifdef USE_NUMPY
#ifdef USE_BOOST_NUMPY
#include <boost/numpy.hpp>
#else
// we use Halide::numpy
#include "../numpy/numpy.hpp"
#endif
#endif  // USE_NUMPY

#include <boost/cstdint.hpp>
#include <boost/functional/hash/hash.hpp>
#include <boost/mpl/list.hpp>

#include "Func.h"
#include "Type.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace h = Halide;
namespace p = boost::python;

#ifdef USE_NUMPY
#ifdef USE_BOOST_NUMPY
namespace bn = boost::numpy;
#else
namespace bn = Halide::numpy;
#endif
#endif  // USE_NUMPY

template <typename Ret, typename T, typename... Args>
Ret buffer_call_operator(h::Buffer<T> &that, Args... args) {
    return that(args...);
}

template <typename T>
h::Expr buffer_call_operator_tuple(h::Buffer<T> &that, p::tuple &args_passed) {
    std::vector<h::Expr> expr_args;
    for (ssize_t i = 0; i < p::len(args_passed); i++) {
        expr_args.push_back(p::extract<h::Expr>(args_passed[i]));
    }
    return that(expr_args);
}

template <typename T>
T buffer_to_setitem_operator0(h::Buffer<T> &that, int x, T value) {
    return that(x) = value;
}

template <typename T>
T buffer_to_setitem_operator1(h::Buffer<T> &that, int x, int y, T value) {
    return that(x, y) = value;
}

template <typename T>
T buffer_to_setitem_operator2(h::Buffer<T> &that, int x, int y, int z, T value) {
    return that(x, y, z) = value;
}

template <typename T>
T buffer_to_setitem_operator3(h::Buffer<T> &that, int x, int y, int z, int w, T value) {
    return that(x, y, z, w) = value;
}

template <typename T>
T buffer_to_setitem_operator4(h::Buffer<T> &that, p::tuple &args_passed, T value) {
    std::vector<int> int_args;
    const size_t args_len = p::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        p::object o = args_passed[i];
        p::extract<int> int32_extract(o);

        if (int32_extract.check()) {
            int_args.push_back(int32_extract());
        }
    }

    if (int_args.size() != args_len) {
        for (size_t j = 0; j < args_len; j += 1) {
            p::object o = args_passed[j];
            const std::string o_str = p::extract<std::string>(p::str(o));
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
const T *buffer_data(const h::Buffer<T> &buffer) {
    return buffer.data();
}

template <typename T>
void buffer_set_min1(h::Buffer<T> &im, int m0) {
    im.set_min(m0);
}

template <typename T>
void buffer_set_min2(h::Buffer<T> &im, int m0, int m1) {
    im.set_min(m0, m1);
}

template <typename T>
void buffer_set_min3(h::Buffer<T> &im, int m0, int m1, int m2) {
    im.set_min(m0, m1, m2);
}

template <typename T>
void buffer_set_min4(h::Buffer<T> &im, int m0, int m1, int m2, int m3) {
    im.set_min(m0, m1, m2, m3);
}

template <typename T>
std::string buffer_repr(const h::Buffer<T> &buffer) {
    std::string repr;

    h::Type t = halide_type_of<T>();
    std::string suffix = "_???";
    if (t.is_float()) {
        suffix = "_float";
    } else if (t.is_int()) {
        suffix = "_int";
    } else if (t.is_uint()) {
        suffix = "_uint";
    } else if (t.is_bool()) {
        suffix = "_bool";
    } else if (t.is_handle()) {
        suffix = "_handle";
    }

    boost::format f("<halide.Buffer%s%i; element_size %i bytes; "
                    "extent (%i %i %i %i); min (%i %i %i %i); stride (%i %i %i %i)>");

    repr = boost::str(f % suffix % t.bits() % t.bytes() % buffer.extent(0) % buffer.extent(1) % buffer.extent(2) % buffer.extent(3) % buffer.min(0) % buffer.min(1) % buffer.min(2) % buffer.min(3) % buffer.stride(0) % buffer.stride(1) % buffer.stride(2) % buffer.stride(3));

    return repr;
}

template <typename T>
boost::python::object get_type_function_wrapper() {
    std::function<h::Type(h::Buffer<T> &)> return_type_func =
        [&](h::Buffer<T> &that) -> h::Type { return halide_type_of<T>(); };
    auto call_policies = p::default_call_policies();
    typedef boost::mpl::vector<h::Type, h::Buffer<T> &> func_sig;
    return p::make_function(return_type_func, call_policies, p::arg("self"), func_sig());
}

template <typename T>
void buffer_copy_to_host(h::Buffer<T> &im) {
    im.copy_to_host();
}

template <typename T>
void buffer_set_host_dirty(h::Buffer<T> &im, bool value) {
    im.set_host_dirty(value);
}

template <typename T>
int buffer_channels(h::Buffer<T> &im) {
    return im.channels();
}

template <typename T>
int buffer_width(h::Buffer<T> &im) {
    return im.width();
}

template <typename T>
int buffer_height(h::Buffer<T> &im) {
    return im.height();
}

template <typename T>
int buffer_dimensions(h::Buffer<T> &im) {
    return im.dimensions();
}

template <typename T>
int buffer_left(h::Buffer<T> &im) {
    return im.left();
}

template <typename T>
int buffer_right(h::Buffer<T> &im) {
    return im.right();
}

template <typename T>
int buffer_top(h::Buffer<T> &im) {
    return im.top();
}

template <typename T>
int buffer_bottom(h::Buffer<T> &im) {
    return im.bottom();
}

template <typename T>
int buffer_stride(h::Buffer<T> &im, int d) {
    return im.stride(d);
}

template <typename T>
int buffer_min(h::Buffer<T> &im, int d) {
    return im.min(d);
}

template <typename T>
int buffer_extent(h::Buffer<T> &im, int d) {
    return im.extent(d);
}



template <typename T>
void defineBuffer_impl(const std::string suffix, const h::Type type) {
    using h::Buffer;
    using h::Expr;

    auto buffer_class =
        p::class_<Buffer<T>>(
            ("Buffer" + suffix).c_str(),
            "A reference-counted handle on a dense multidimensional array "
            "containing scalar values of type T. Can be directly accessed and "
            "modified. May have up to four dimensions. Color images are "
            "represented as three-dimensional, with the third dimension being "
            "the color channel. In general we store color images in "
            "color-planes, as opposed to packed RGB, because this tends to "
            "vectorize more cleanly.",
            p::init<>(p::arg("self"), "Construct an undefined buffer handle"));

    // Constructors
    buffer_class
        .def(p::init<int>(
            p::args("self", "x"),
            "Allocate an buffer with the given dimensions."))

        .def(p::init<int, int>(
            p::args("self", "x", "y"),
            "Allocate an buffer with the given dimensions."))

        .def(p::init<int, int, int>(
            p::args("self", "x", "y", "z"),
            "Allocate an buffer with the given dimensions."))

        .def(p::init<int, int, int, int>(
            p::args("self", "x", "y", "z", "w"),
            "Allocate an buffer with the given dimensions."))

        .def(p::init<h::Realization &>(
            p::args("self", "r"),
            "Wrap a single-element realization in an Buffer object."))

        .def(p::init<halide_buffer_t>(
            p::args("self", "b"),
            "Wrap a halide_buffer_t in an Buffer object, so that we can access its pixels."));

    buffer_class
        .def("__repr__", buffer_repr<T>, p::arg("self"));

    buffer_class
        .def("data", buffer_data<T>, p::arg("self"),
             p::return_value_policy<p::return_opaque_pointer>(),  // not sure this will do what we want
             "Get a pointer to the element at the min location.")

        .def("copy_to_host", buffer_copy_to_host<T>, p::arg("self"),
             "Manually copy-back data to the host, if it's on a device. ")
        .def("set_host_dirty", buffer_set_host_dirty<T>,
             (p::arg("self"), p::arg("dirty") = true),
             "Mark the buffer as dirty-on-host. ")
        .def("type", get_type_function_wrapper<T>(),
             "Return Type instance for the data type of the buffer.")
        .def("channels", buffer_channels<T>, p::arg("self"),
             "Get the extent of dimension 2, which by convention we use as"
             "the number of color channels (often 3). Unlike extent(2), "
             "returns one if the buffer has fewer than three dimensions.")
        .def("dimensions", buffer_dimensions<T>, p::arg("self"),
             "Get the dimensionality of the data. Typically two for grayscale images, and three for color images.")
        .def("stride", buffer_stride<T>, p::args("self", "dim"),
             "Get the number of elements in the buffer between two adjacent "
             "elements in the given dimension. For example, the stride in "
             "dimension 0 is usually 1, and the stride in dimension 1 is "
             "usually the extent of dimension 0. This is not necessarily true though.")
        .def("extent", buffer_extent<T>, p::args("self", "dim"),
             "Get the size of a dimension.")
        .def("min", buffer_min<T>, p::args("self", "dim"),
             "Get the min coordinate of a dimension. The top left of the "
             "buffer represents this point in a function that was realized "
             "into this buffer.");

    buffer_class
        .def("set_min", buffer_set_min1<T>,
             p::args("self", "m0"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", buffer_set_min2<T>,
             p::args("self", "m0", "m1"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", buffer_set_min3<T>,
             p::args("self", "m0", "m1", "m2"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", buffer_set_min4<T>,
             p::args("self", "m0", "m1", "m2", "m3"),
             "Set the coordinates corresponding to the host pointer.");

    buffer_class
        .def("width", buffer_width<T>, p::arg("self"),
             "Get the extent of dimension 0, which by convention we use as "
             "the width of the image. Unlike extent(0), returns one if the "
             "buffer is zero-dimensional.")
        .def("height", buffer_height<T>, p::arg("self"),
             "Get the extent of dimension 1, which by convention we use as "
             "the height of the image. Unlike extent(1), returns one if the "
             "buffer has fewer than two dimensions.")
        .def("left", buffer_left<T>, p::arg("self"),
             "Get the minimum coordinate in dimension 0, which by convention "
             "is the coordinate of the left edge of the image. Returns zero "
             "for zero-dimensional images.")
        .def("right", buffer_right<T>, p::arg("self"),
             "Get the maximum coordinate in dimension 0, which by convention "
             "is the coordinate of the right edge of the image. Returns zero "
             "for zero-dimensional images.")
        .def("top", buffer_top<T>, p::arg("self"),
             "Get the minimum coordinate in dimension 1, which by convention "
             "is the top of the image. Returns zero for zero- or "
             "one-dimensional images.")
        .def("bottom", buffer_bottom<T>, p::arg("self"),
             "Get the maximum coordinate in dimension 1, which by convention "
             "is the bottom of the image. Returns zero for zero- or "
             "one-dimensional images.");

    const char *get_item_doc =
        "Construct an expression which loads from this buffer. ";

    // Access operators (to Expr, and to actual value)
    buffer_class
        .def("__getitem__", buffer_call_operator<Expr, T, Expr>,
             p::args("self", "x"),
             get_item_doc);
    buffer_class
        .def("__getitem__", buffer_call_operator<Expr, T, Expr, Expr>,
             p::args("self", "x", "y"),
             get_item_doc);
    buffer_class
        .def("__getitem__", buffer_call_operator<Expr, T, Expr, Expr, Expr>,
             p::args("self", "x", "y", "z"),
             get_item_doc)
        .def("__getitem__", buffer_call_operator<Expr, T, Expr, Expr, Expr, Expr>,
             p::args("self", "x", "y", "z", "w"),
             get_item_doc)
        .def("__getitem__", buffer_call_operator_tuple<T>,
             p::args("self", "tuple"),
             get_item_doc)
        // Note that we return copy values (not references like in the C++ API)
        .def("__getitem__", buffer_call_operator<T, T>,
             p::arg("self"),
             "Assuming this buffer is zero-dimensional, get its value")
        .def("__call__", buffer_call_operator<T, T, int>,
             p::args("self", "x"),
             "Assuming this buffer is one-dimensional, get the value of the element at position x")
        .def("__call__", buffer_call_operator<T, T, int, int>,
             p::args("self", "x", "y"),
             "Assuming this buffer is two-dimensional, get the value of the element at position (x, y)")
        .def("__call__", buffer_call_operator<T, T, int, int, int>,
             p::args("self", "x", "y", "z"),
             "Assuming this buffer is three-dimensional, get the value of the element at position (x, y, z)")
        .def("__call__", buffer_call_operator<T, T, int, int, int, int>,
             p::args("self", "x", "y", "z", "w"),
             "Assuming this buffer is four-dimensional, get the value of the element at position (x, y, z, w)")

        .def("__setitem__", buffer_to_setitem_operator0<T>, p::args("self", "x", "value"),
             "Assuming this buffer is one-dimensional, set the value of the element at position x")
        .def("__setitem__", buffer_to_setitem_operator1<T>, p::args("self", "x", "y", "value"),
             "Assuming this buffer is two-dimensional, set the value of the element at position (x, y)")
        .def("__setitem__", buffer_to_setitem_operator2<T>, p::args("self", "x", "y", "z", "value"),
             "Assuming this buffer is three-dimensional, set the value of the element at position (x, y, z)")
        .def("__setitem__", buffer_to_setitem_operator3<T>, p::args("self", "x", "y", "z", "w", "value"),
             "Assuming this buffer is four-dimensional, set the value of the element at position (x, y, z, w)")
        .def("__setitem__", buffer_to_setitem_operator4<T>, p::args("self", "tuple", "value"),
             "Assuming this buffer is one to four-dimensional, "
             "set the value of the element at position indicated by tuple (x, y, z, w)");

    p::implicitly_convertible<Buffer<T>, h::Argument>();

    return;
}

p::object buffer_to_python_object(const h::Buffer<> &im) {
    PyObject *obj = nullptr;
    if (im.type() == h::UInt(8)) {
        p::manage_new_object::apply<h::Buffer<uint8_t> *>::type converter;
        obj = converter(new h::Buffer<uint8_t>(im));
    } else if (im.type() == h::UInt(16)) {
        p::manage_new_object::apply<h::Buffer<uint16_t> *>::type converter;
        obj = converter(new h::Buffer<uint16_t>(im));
    } else if (im.type() == h::UInt(32)) {
        p::manage_new_object::apply<h::Buffer<uint32_t> *>::type converter;
        obj = converter(new h::Buffer<uint32_t>(im));
    } else if (im.type() == h::Int(8)) {
        p::manage_new_object::apply<h::Buffer<int8_t> *>::type converter;
        obj = converter(new h::Buffer<int8_t>(im));
    } else if (im.type() == h::Int(16)) {
        p::manage_new_object::apply<h::Buffer<int16_t> *>::type converter;
        obj = converter(new h::Buffer<int16_t>(im));
    } else if (im.type() == h::Int(32)) {
        p::manage_new_object::apply<h::Buffer<int32_t> *>::type converter;
        obj = converter(new h::Buffer<int32_t>(im));
    } else if (im.type() == h::Float(32)) {
        p::manage_new_object::apply<h::Buffer<float> *>::type converter;
        obj = converter(new h::Buffer<float>(im));
    } else if (im.type() == h::Float(64)) {
        p::manage_new_object::apply<h::Buffer<double> *>::type converter;
        obj = converter(new h::Buffer<double>(im));
    } else {
        throw std::invalid_argument("buffer_to_python_object received an Buffer of unsupported type.");
    }

    return p::object(p::handle<>(obj));
}

h::Buffer<> python_object_to_buffer(p::object obj) {
    p::extract<h::Buffer<uint8_t>> buffer_extract_uint8(obj);
    p::extract<h::Buffer<uint16_t>> buffer_extract_uint16(obj);
    p::extract<h::Buffer<uint32_t>> buffer_extract_uint32(obj);
    p::extract<h::Buffer<int8_t>> buffer_extract_int8(obj);
    p::extract<h::Buffer<int16_t>> buffer_extract_int16(obj);
    p::extract<h::Buffer<int32_t>> buffer_extract_int32(obj);
    p::extract<h::Buffer<float>> buffer_extract_float(obj);
    p::extract<h::Buffer<double>> buffer_extract_double(obj);

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
    return h::Buffer<>();
}

#ifdef USE_NUMPY

bn::dtype type_to_dtype(const h::Type &t) {
    if (t == h::UInt(8)) return bn::dtype::get_builtin<uint8_t>();
    if (t == h::UInt(16)) return bn::dtype::get_builtin<uint16_t>();
    if (t == h::UInt(32)) return bn::dtype::get_builtin<uint32_t>();
    if (t == h::Int(8)) return bn::dtype::get_builtin<int8_t>();
    if (t == h::Int(16)) return bn::dtype::get_builtin<int16_t>();
    if (t == h::Int(32)) return bn::dtype::get_builtin<int32_t>();
    if (t == h::Float(32)) return bn::dtype::get_builtin<float>();
    if (t == h::Float(64)) return bn::dtype::get_builtin<double>();
    throw std::runtime_error("type_to_dtype received a Halide::Type with no known numpy dtype equivalent");
    return bn::dtype::get_builtin<uint8_t>();
}

h::Type dtype_to_type(const bn::dtype &t) {
    if (t == bn::dtype::get_builtin<uint8_t>()) return h::UInt(8);
    if (t == bn::dtype::get_builtin<uint16_t>()) return h::UInt(16);
    if (t == bn::dtype::get_builtin<uint32_t>()) return h::UInt(32);
    if (t == bn::dtype::get_builtin<int8_t>()) return h::Int(8);
    if (t == bn::dtype::get_builtin<int16_t>()) return h::Int(16);
    if (t == bn::dtype::get_builtin<int32_t>()) return h::Int(32);
    if (t == bn::dtype::get_builtin<float>()) return h::Float(32);
    if (t == bn::dtype::get_builtin<double>()) return h::Float(64);
    throw std::runtime_error("dtype_to_type received a numpy type with no known Halide type equivalent");
    return h::Type();
}

/// Will create a Halide::Buffer object pointing to the array data
p::object ndarray_to_buffer(bn::ndarray &array) {
    h::Type t = dtype_to_type(array.get_dtype());
    const int dims = array.get_nd();
    void *host = reinterpret_cast<void *>(array.get_data());
    halide_dimension_t *shape =
        (halide_dimension_t *)__builtin_alloca(sizeof(halide_dimension_t) * dims);
    for (int i = 0; i < dims; i++) {
        shape[i].min = 0;
        shape[i].extent = array.shape(i);
        shape[i].stride = array.strides(i) / t.bytes();
    }

    return buffer_to_python_object(h::Buffer<>(t, host, dims, shape));
}

bn::ndarray buffer_to_ndarray(p::object buffer_object) {
    h::Buffer<> im = python_object_to_buffer(buffer_object);

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

#endif

struct BufferFactory {

    template <typename T, typename... Args>
    static p::object create_buffer_object(Args... args) {
        typedef h::Buffer<T> BufferType;
        typedef typename p::manage_new_object::apply<BufferType *>::type converter_t;
        converter_t converter;
        PyObject *obj = converter(new BufferType(args...));
        return p::object(p::handle<>(obj));
    }

    template <typename... Args>
    static p::object create_buffer_impl(h::Type t, Args... args) {
        if (t == h::UInt(8)) return create_buffer_object<uint8_t>(args...);
        if (t == h::UInt(16)) return create_buffer_object<uint16_t>(args...);
        if (t == h::UInt(32)) return create_buffer_object<uint32_t>(args...);
        if (t == h::Int(8)) return create_buffer_object<int8_t>(args...);
        if (t == h::Int(16)) return create_buffer_object<int16_t>(args...);
        if (t == h::Int(32)) return create_buffer_object<int32_t>(args...);
        if (t == h::Float(32)) return create_buffer_object<float>(args...);
        if (t == h::Float(64)) return create_buffer_object<double>(args...);
        throw std::invalid_argument("BufferFactory::create_buffer_impl received type not handled");
        return p::object();
    }

    static p::object create_buffer0(h::Type type) {
        return create_buffer_impl(type);
    }

    static p::object create_buffer1(h::Type type, int x) {
        return create_buffer_impl(type, x);
    }

    static p::object create_buffer2(h::Type type, int x, int y) {
        return create_buffer_impl(type, x, y);
    }

    static p::object create_buffer3(h::Type type, int x, int y, int z) {
        return create_buffer_impl(type, x, y, z);
    }

    static p::object create_buffer4(h::Type type, int x, int y, int z, int w) {
        return create_buffer_impl(type, x, y, z, w);
    }

    static p::object create_buffer_from_realization(h::Type type, h::Realization &r) {
        return create_buffer_impl(type, r);
    }

    static p::object create_buffer_from_buffer(halide_buffer_t b) {
        return create_buffer_impl(b.type, b);
    }
};

void defineBuffer() {
    defineBuffer_impl<uint8_t>("_uint8", h::UInt(8));
    defineBuffer_impl<uint16_t>("_uint16", h::UInt(16));
    defineBuffer_impl<uint32_t>("_uint32", h::UInt(32));

    defineBuffer_impl<int8_t>("_int8", h::Int(8));
    defineBuffer_impl<int16_t>("_int16", h::Int(16));
    defineBuffer_impl<int32_t>("_int32", h::Int(32));

    defineBuffer_impl<float>("_float32", h::Float(32));
    defineBuffer_impl<double>("_float64", h::Float(64));

    // "Buffer" will look as a class, but instead it will be simply a factory method
    p::def("Buffer", &BufferFactory::create_buffer0,
           p::args("type"),
           "Construct a zero-dimensional buffer of type T");
    p::def("Buffer", &BufferFactory::create_buffer1,
           p::args("type", "x"),
           "Construct a one-dimensional buffer of type T");
    p::def("Buffer", &BufferFactory::create_buffer2,
           p::args("type", "x", "y"),
           "Construct a two-dimensional buffer of type T");
    p::def("Buffer", &BufferFactory::create_buffer3,
           p::args("type", "x", "y", "z"),
           "Construct a three-dimensional buffer of type T");
    p::def("Buffer", &BufferFactory::create_buffer4,
           p::args("type", "x", "y", "z", "w"),
           "Construct a four-dimensional buffer of type T");

    p::def("Buffer", &BufferFactory::create_buffer_from_realization,
           p::args("type", "r"),
           p::with_custodian_and_ward_postcall<0, 2>(),  // the realization reference count is increased
           "Wrap a single-element realization in an Buffer object of type T.");

    p::def("Buffer", &BufferFactory::create_buffer_from_buffer,
           p::args("b"),
           p::with_custodian_and_ward_postcall<0, 2>(),  // the buffer_t reference count is increased
           "Wrap a halide_buffer_t in an Buffer object, so that we can access its pixels.");

#ifdef USE_NUMPY
    bn::initialize();

    p::def("ndarray_to_buffer", &ndarray_to_buffer,
           p::args("array"),
           p::with_custodian_and_ward_postcall<0, 1>(),  // the array reference count is increased
           "Converts a numpy array into a Halide::Buffer."
           "Will take into account the array size, dimensions, and type."
           "Created Buffer refers to the array data (no copy).");

    p::def("Buffer", &ndarray_to_buffer,
           p::args("array"),
           p::with_custodian_and_ward_postcall<0, 1>(),  // the array reference count is increased
           "Wrap numpy array in a Halide::Buffer."
           "Will take into account the array size, dimensions, and type."
           "Created Buffer refers to the array data (no copy).");

    p::def("buffer_to_ndarray", &buffer_to_ndarray,
           p::args("buffer"),
           p::with_custodian_and_ward_postcall<0, 1>(),  // the buffer reference count is increased
           "Creates a numpy array from a Halide::Buffer."
           "Will take into account the Buffer size, dimensions, and type."
           "Created ndarray refers to the Buffer data (no copy).");
#endif

    return;
}
