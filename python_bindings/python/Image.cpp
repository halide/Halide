#include "Image.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/format.hpp>
#include <boost/python.hpp>

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

#include "../../src/runtime/HalideBuffer.h"
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
Ret image_call_operator(h::Image<T> &that, Args... args) {
    return that(args...);
}

template <typename T>
h::Expr image_call_operator_tuple(h::Image<T> &that, p::tuple &args_passed) {
    std::vector<h::Expr> expr_args;
    for (ssize_t i = 0; i < p::len(args_passed); i++) {
        expr_args.push_back(p::extract<h::Expr>(args_passed[i]));
    }
    return that(expr_args);
}

template <typename T>
T image_to_setitem_operator0(h::Image<T> &that, int x, T value) {
    return that(x) = value;
}

template <typename T>
T image_to_setitem_operator1(h::Image<T> &that, int x, int y, T value) {
    return that(x, y) = value;
}

template <typename T>
T image_to_setitem_operator2(h::Image<T> &that, int x, int y, int z, T value) {
    return that(x, y, z) = value;
}

template <typename T>
T image_to_setitem_operator3(h::Image<T> &that, int x, int y, int z, int w, T value) {
    return that(x, y, z, w) = value;
}

template <typename T>
T image_to_setitem_operator4(h::Image<T> &that, p::tuple &args_passed, T value) {
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
            printf("image_to_setitem_operator4 args_passed[%lu] == %s\n", j, o_str.c_str());
        }
        throw std::invalid_argument("image_to_setitem_operator4 only handles "
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
        printf("image_to_setitem_operator4 receive a tuple with %zu integers\n", int_args.size());
        throw std::invalid_argument("image_to_setitem_operator4 only handles 1 to 4 dimensional tuples");
    }

    return 0;  // this line should never be reached
}

template <typename T>
const T *image_data(const h::Image<T> &image) {
    return image.data();
}

template <typename T>
void image_set_min1(h::Image<T> &im, int m0) {
    im.set_min(m0);
}

template <typename T>
void image_set_min2(h::Image<T> &im, int m0, int m1) {
    im.set_min(m0, m1);
}

template <typename T>
void image_set_min3(h::Image<T> &im, int m0, int m1, int m2) {
    im.set_min(m0, m1, m2);
}

template <typename T>
void image_set_min4(h::Image<T> &im, int m0, int m1, int m2, int m3) {
    im.set_min(m0, m1, m2, m3);
}

template <typename T>
std::string image_repr(const h::Image<T> &image) {
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

    boost::format f("<halide.Image%s%i; element_size %i bytes; "
                    "extent (%i %i %i %i); min (%i %i %i %i); stride (%i %i %i %i)>");

    repr = boost::str(f % suffix % t.bits() % t.bytes() % image.extent(0) % image.extent(1) % image.extent(2) % image.extent(3) % image.min(0) % image.min(1) % image.min(2) % image.min(3) % image.stride(0) % image.stride(1) % image.stride(2) % image.stride(3));

    return repr;
}

template <typename T>
boost::python::object get_type_function_wrapper() {
    std::function<h::Type(h::Image<T> &)> return_type_func =
        [&](h::Image<T> &that) -> h::Type { return halide_type_of<T>(); };
    auto call_policies = p::default_call_policies();
    typedef boost::mpl::vector<h::Type, h::Image<T> &> func_sig;
    return p::make_function(return_type_func, call_policies, p::arg("self"), func_sig());
}

template <typename T>
void image_copy_to_host(h::Image<T> &im) {
    im.copy_to_host();
}

template <typename T>
void defineImage_impl(const std::string suffix, const h::Type type) {
    using h::Image;
    using h::Expr;

    auto image_class =
        p::class_<Image<T>>(
            ("Image" + suffix).c_str(),
            "A reference-counted handle on a dense multidimensional array "
            "containing scalar values of type T. Can be directly accessed and "
            "modified. May have up to four dimensions. Color images are "
            "represented as three-dimensional, with the third dimension being "
            "the color channel. In general we store color images in "
            "color-planes, as opposed to packed RGB, because this tends to "
            "vectorize more cleanly.",
            p::init<>(p::arg("self"), "Construct an undefined image handle"));

    // Constructors
    image_class
        .def(p::init<int>(
            p::args("self", "x"),
            "Allocate an image with the given dimensions."))

        .def(p::init<int, int>(
            p::args("self", "x", "y"),
            "Allocate an image with the given dimensions."))

        .def(p::init<int, int, int>(
            p::args("self", "x", "y", "z"),
            "Allocate an image with the given dimensions."))

        .def(p::init<int, int, int, int>(
            p::args("self", "x", "y", "z", "w"),
            "Allocate an image with the given dimensions."))

        .def(p::init<h::Realization &>(
            p::args("self", "r"),
            "Wrap a single-element realization in an Image object."))

        .def(p::init<buffer_t>(
            p::args("self", "b"),
            "Wrap a buffer_t in an Image object, so that we can access its pixels."));

    image_class
        .def("__repr__", &image_repr<T>, p::arg("self"));

    image_class
        .def("data", &image_data<T>, p::arg("self"),
             p::return_value_policy<p::return_opaque_pointer>(),  // not sure this will do what we want
             "Get a pointer to the element at the min location.")

        .def("copy_to_host", &image_copy_to_host<T>, p::arg("self"),
             "Manually copy-back data to the host, if it's on a device. This "
             "is done for you if you construct an image from a buffer, but "
             "you might need to call this if you realize a gpu kernel into an "
             "existing image")
        .def("set_host_dirty", &Image<T>::set_host_dirty,
             (p::arg("self"), p::arg("dirty") = true),
             "Mark the buffer as dirty-on-host.  is done for you if you "
             "construct an image from a buffer, but you might need to call "
             "this if you realize a gpu kernel into an existing image, or "
             "modify the data via some other back-door.")
        .def("type", get_type_function_wrapper<T>(),
             "Return Type instance for the data type of the image.")
        .def("channels", &Image<T>::channels, p::arg("self"),
             "Get the extent of dimension 2, which by convention we use as"
             "the number of color channels (often 3). Unlike extent(2), "
             "returns one if the buffer has fewer than three dimensions.")
        .def("dimensions", &Image<T>::dimensions, p::arg("self"),
             "Get the dimensionality of the data. Typically two for grayscale images, and three for color images.")
        .def("stride", &Image<T>::stride, p::args("self", "dim"),
             "Get the number of elements in the buffer between two adjacent "
             "elements in the given dimension. For example, the stride in "
             "dimension 0 is usually 1, and the stride in dimension 1 is "
             "usually the extent of dimension 0. This is not necessarily true though.")
        .def("extent", &Image<T>::extent, p::args("self", "dim"),
             "Get the size of a dimension.")
        .def("min", &Image<T>::min, p::args("self", "dim"),
             "Get the min coordinate of a dimension. The top left of the "
             "image represents this point in a function that was realized "
             "into this image.");

    image_class
        .def("set_min", &image_set_min1<T>,
             p::args("self", "m0"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", &image_set_min2<T>,
             p::args("self", "m0", "m1"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", &image_set_min3<T>,
             p::args("self", "m0", "m1", "m2"),
             "Set the coordinates corresponding to the host pointer.")
        .def("set_min", &image_set_min4<T>,
             p::args("self", "m0", "m1", "m2", "m3"),
             "Set the coordinates corresponding to the host pointer.");

    image_class
        .def("width", &Image<T>::width, p::arg("self"),
             "Get the extent of dimension 0, which by convention we use as "
             "the width of the image. Unlike extent(0), returns one if the "
             "buffer is zero-dimensional.")
        .def("height", &Image<T>::height, p::arg("self"),
             "Get the extent of dimension 1, which by convention we use as "
             "the height of the image. Unlike extent(1), returns one if the "
             "buffer has fewer than two dimensions.")
        .def("left", &Image<T>::left, p::arg("self"),
             "Get the minimum coordinate in dimension 0, which by convention "
             "is the coordinate of the left edge of the image. Returns zero "
             "for zero-dimensional images.")
        .def("right", &Image<T>::right, p::arg("self"),
             "Get the maximum coordinate in dimension 0, which by convention "
             "is the coordinate of the right edge of the image. Returns zero "
             "for zero-dimensional images.")
        .def("top", &Image<T>::top, p::arg("self"),
             "Get the minimum coordinate in dimension 1, which by convention "
             "is the top of the image. Returns zero for zero- or "
             "one-dimensional images.")
        .def("bottom", &Image<T>::bottom, p::arg("self"),
             "Get the maximum coordinate in dimension 1, which by convention "
             "is the bottom of the image. Returns zero for zero- or "
             "one-dimensional images.");

    const char *get_item_doc =
        "Construct an expression which loads from this image. "
        "The location is extended with enough implicit variables to match "
        "the dimensionality of the image (see \\ref Var::implicit)";

    // Access operators (to Expr, and to actual value)
    image_class
        .def("__getitem__", &image_call_operator<Expr, T, Expr>,
             p::args("self", "x"),
             get_item_doc);
    image_class
        .def("__getitem__", &image_call_operator<Expr, T, Expr, Expr>,
             p::args("self", "x", "y"),
             get_item_doc);
    image_class
        .def("__getitem__", &image_call_operator<Expr, T, Expr, Expr, Expr>,
             p::args("self", "x", "y", "z"),
             get_item_doc)
        .def("__getitem__", &image_call_operator<Expr, T, Expr, Expr, Expr, Expr>,
             p::args("self", "x", "y", "z", "w"),
             get_item_doc)
        .def("__getitem__", &image_call_operator_tuple<T>,
             p::args("self", "tuple"),
             get_item_doc)
        // Note that we return copy values (not references like in the C++ API)
        .def("__getitem__", &image_call_operator<T, T>,
             p::arg("self"),
             "Assuming this image is zero-dimensional, get its value")
        .def("__call__", &image_call_operator<T, T, int>,
             p::args("self", "x"),
             "Assuming this image is one-dimensional, get the value of the element at position x")
        .def("__call__", &image_call_operator<T, T, int, int>,
             p::args("self", "x", "y"),
             "Assuming this image is two-dimensional, get the value of the element at position (x, y)")
        .def("__call__", &image_call_operator<T, T, int, int, int>,
             p::args("self", "x", "y", "z"),
             "Assuming this image is three-dimensional, get the value of the element at position (x, y, z)")
        .def("__call__", &image_call_operator<T, T, int, int, int, int>,
             p::args("self", "x", "y", "z", "w"),
             "Assuming this image is four-dimensional, get the value of the element at position (x, y, z, w)")

        .def("__setitem__", &image_to_setitem_operator0<T>, p::args("self", "x", "value"),
             "Assuming this image is one-dimensional, set the value of the element at position x")
        .def("__setitem__", &image_to_setitem_operator1<T>, p::args("self", "x", "y", "value"),
             "Assuming this image is two-dimensional, set the value of the element at position (x, y)")
        .def("__setitem__", &image_to_setitem_operator2<T>, p::args("self", "x", "y", "z", "value"),
             "Assuming this image is three-dimensional, set the value of the element at position (x, y, z)")
        .def("__setitem__", &image_to_setitem_operator3<T>, p::args("self", "x", "y", "z", "w", "value"),
             "Assuming this image is four-dimensional, set the value of the element at position (x, y, z, w)")
        .def("__setitem__", &image_to_setitem_operator4<T>, p::args("self", "tuple", "value"),
             "Assuming this image is one to four-dimensional, "
             "set the value of the element at position indicated by tuple (x, y, z, w)");

    p::implicitly_convertible<Image<T>, h::Argument>();

    return;
}

p::object image_to_python_object(const h::Image<> &im) {
    PyObject *obj = nullptr;
    if (im.type() == h::UInt(8)) {
        p::manage_new_object::apply<h::Image<uint8_t> *>::type converter;
        obj = converter(new h::Image<uint8_t>(im));
    } else if (im.type() == h::UInt(16)) {
        p::manage_new_object::apply<h::Image<uint16_t> *>::type converter;
        obj = converter(new h::Image<uint16_t>(im));
    } else if (im.type() == h::UInt(32)) {
        p::manage_new_object::apply<h::Image<uint32_t> *>::type converter;
        obj = converter(new h::Image<uint32_t>(im));
    } else if (im.type() == h::Int(8)) {
        p::manage_new_object::apply<h::Image<int8_t> *>::type converter;
        obj = converter(new h::Image<int8_t>(im));
    } else if (im.type() == h::Int(16)) {
        p::manage_new_object::apply<h::Image<int16_t> *>::type converter;
        obj = converter(new h::Image<int16_t>(im));
    } else if (im.type() == h::Int(32)) {
        p::manage_new_object::apply<h::Image<int32_t> *>::type converter;
        obj = converter(new h::Image<int32_t>(im));
    } else if (im.type() == h::Float(32)) {
        p::manage_new_object::apply<h::Image<float> *>::type converter;
        obj = converter(new h::Image<float>(im));
    } else if (im.type() == h::Float(64)) {
        p::manage_new_object::apply<h::Image<double> *>::type converter;
        obj = converter(new h::Image<double>(im));
    } else {
        throw std::invalid_argument("image_to_python_object received an Image of unsupported type.");
    }

    return p::object(p::handle<>(obj));
}

h::Image<> python_object_to_image(p::object obj) {
    p::extract<h::Image<uint8_t>> image_extract_uint8(obj);
    p::extract<h::Image<uint16_t>> image_extract_uint16(obj);
    p::extract<h::Image<uint32_t>> image_extract_uint32(obj);
    p::extract<h::Image<int8_t>> image_extract_int8(obj);
    p::extract<h::Image<int16_t>> image_extract_int16(obj);
    p::extract<h::Image<int32_t>> image_extract_int32(obj);
    p::extract<h::Image<float>> image_extract_float(obj);
    p::extract<h::Image<double>> image_extract_double(obj);

    if (image_extract_uint8.check()) {
        return image_extract_uint8();
    } else if (image_extract_uint16.check()) {
        return image_extract_uint16();
    } else if (image_extract_uint32.check()) {
        return image_extract_uint32();
    } else if (image_extract_int8.check()) {
        return image_extract_int8();
    } else if (image_extract_int16.check()) {
        return image_extract_int16();
    } else if (image_extract_int32.check()) {
        return image_extract_int32();
    } else if (image_extract_float.check()) {
        return image_extract_float();
    } else if (image_extract_double.check()) {
        return image_extract_double();
    } else {
        throw std::invalid_argument("python_object_to_image received an object that is not an Image<T>");
    }
    return h::Image<>();
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

/// Will create a Halide::Image object pointing to the array data
p::object ndarray_to_image(bn::ndarray &array) {
    h::Type t = dtype_to_type(array.get_dtype());
    const int dims = array.get_nd();
    void *host = reinterpret_cast<void *>(array.get_data());
    halide_dimension_t shape[dims];
    for (int i = 0; i < dims; i++) {
        shape[i].min = 0;
        shape[i].extent = array.shape(i);
        shape[i].stride = array.strides(i) / t.bytes();
    }

    return image_to_python_object(h::Image<>(t, host, dims, shape));
}

bn::ndarray image_to_ndarray(p::object image_object) {
    h::Image<> im = python_object_to_image(image_object);

    user_assert(im.data() != nullptr)
        << "image_to_ndarray received an image without host data";

    std::vector<int32_t> extent(im.dimensions()), stride(im.dimensions());
    for (int i = 0; i < im.dimensions(); i++) {
        extent[i] = im.dim(i).extent();
        stride[i] = im.dim(i).stride() * im.type().bytes();
    }

    return bn::from_data(
        im.host_ptr(),
        type_to_dtype(im.type()),
        extent,
        stride,
        image_object);
}

#endif

struct ImageFactory {

    template <typename T, typename... Args>
    static p::object create_image_object(Args... args) {
        typedef h::Image<T> ImageType;
        typedef typename p::manage_new_object::apply<ImageType *>::type converter_t;
        converter_t converter;
        PyObject *obj = converter(new ImageType(args...));
        return p::object(p::handle<>(obj));
    }

    template <typename... Args>
    static p::object create_image_impl(h::Type t, Args... args) {
        if (t == h::UInt(8)) return create_image_object<uint8_t>(args...);
        if (t == h::UInt(16)) return create_image_object<uint16_t>(args...);
        if (t == h::UInt(32)) return create_image_object<uint32_t>(args...);
        if (t == h::Int(8)) return create_image_object<int8_t>(args...);
        if (t == h::Int(16)) return create_image_object<int16_t>(args...);
        if (t == h::Int(32)) return create_image_object<int32_t>(args...);
        if (t == h::Float(32)) return create_image_object<float>(args...);
        if (t == h::Float(64)) return create_image_object<double>(args...);
        throw std::invalid_argument("ImageFactory::create_image_impl received type not handled");
        return p::object();
    }

    static p::object create_image0(h::Type type) {
        return create_image_impl(type);
    }

    static p::object create_image1(h::Type type, int x) {
        return create_image_impl(type, x);
    }

    static p::object create_image2(h::Type type, int x, int y) {
        return create_image_impl(type, x, y);
    }

    static p::object create_image3(h::Type type, int x, int y, int z) {
        return create_image_impl(type, x, y, z);
    }

    static p::object create_image4(h::Type type, int x, int y, int z, int w) {
        return create_image_impl(type, x, y, z, w);
    }

    static p::object create_image_from_realization(h::Type type, h::Realization &r) {
        return create_image_impl(type, r);
    }

    static p::object create_image_from_buffer(h::Type type, buffer_t b) {
        return create_image_impl(type, b);
    }
};

void defineImage() {
    defineImage_impl<uint8_t>("_uint8", h::UInt(8));
    defineImage_impl<uint16_t>("_uint16", h::UInt(16));
    defineImage_impl<uint32_t>("_uint32", h::UInt(32));

    defineImage_impl<int8_t>("_int8", h::Int(8));
    defineImage_impl<int16_t>("_int16", h::Int(16));
    defineImage_impl<int32_t>("_int32", h::Int(32));

    defineImage_impl<float>("_float32", h::Float(32));
    defineImage_impl<double>("_float64", h::Float(64));

    // "Image" will look as a class, but instead it will be simply a factory method
    p::def("Image", &ImageFactory::create_image0,
           p::args("type"),
           "Construct a zero-dimensional image of type T");
    p::def("Image", &ImageFactory::create_image1,
           p::args("type", "x"),
           "Construct a one-dimensional image of type T");
    p::def("Image", &ImageFactory::create_image2,
           p::args("type", "x", "y"),
           "Construct a two-dimensional image of type T");
    p::def("Image", &ImageFactory::create_image3,
           p::args("type", "x", "y", "z"),
           "Construct a three-dimensional image of type T");
    p::def("Image", &ImageFactory::create_image4,
           p::args("type", "x", "y", "z", "w"),
           "Construct a four-dimensional image of type T");

    p::def("Image", &ImageFactory::create_image_from_realization,
           p::args("type", "r"),
           p::with_custodian_and_ward_postcall<0, 2>(),  // the realization reference count is increased
           "Wrap a single-element realization in an Image object of type T.");

    p::def("Image", &ImageFactory::create_image_from_buffer,
           p::args("type", "b"),
           p::with_custodian_and_ward_postcall<0, 2>(),  // the buffer_t reference count is increased
           "Wrap a buffer_t in an Image object of type T, so that we can access its pixels.");

#ifdef USE_NUMPY
    bn::initialize();

    p::def("ndarray_to_image", &ndarray_to_image,
           p::args("array"),
           p::with_custodian_and_ward_postcall<0, 1>(),  // the array reference count is increased
           "Converts a numpy array into a Halide::Image."
           "Will take into account the array size, dimensions, and type."
           "Created Image refers to the array data (no copy).");

    p::def("Image", &ndarray_to_image,
           p::args("array"),
           p::with_custodian_and_ward_postcall<0, 1>(),  // the array reference count is increased
           "Wrap numpy array in a Halide::Image."
           "Will take into account the array size, dimensions, and type."
           "Created Image refers to the array data (no copy).");

    p::def("image_to_ndarray", &image_to_ndarray,
           p::args("image"),
           p::with_custodian_and_ward_postcall<0, 1>(),  // the image reference count is increased
           "Creates a numpy array from a Halide::Image."
           "Will take into account the Image size, dimensions, and type."
           "Created ndarray refers to the Image data (no copy).");
#endif

    return;
}
