#include "Image.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include <boost/format.hpp>

#define USE_NUMPY

#ifdef USE_NUMPY
#ifdef USE_BOOST_NUMPY
#include <boost/numpy.hpp>
#else
// we use Halide::numpy
#include "../numpy/numpy.hpp"
#endif
#endif // USE_NUMPY

#include <boost/cstdint.hpp>
#include <boost/mpl/list.hpp>
#include <boost/functional/hash/hash.hpp>

#include "../../src/Image.h"
#include "Type.h"
#include "Func.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

namespace h = Halide;
namespace p = boost::python;

#ifdef USE_NUMPY
#ifdef USE_BOOST_NUMPY
namespace bn = boost::numpy;
#else
namespace bn = Halide::numpy;
#endif
#endif // USE_NUMPY


template<typename T>
h::Expr image_to_expr_operator0(h::Image<T> &that)
{
    return that();
}

template<typename T>
h::Expr image_to_expr_operator1(h::Image<T> &that, h::Expr x)
{
    return that(x);
}

template<typename T>
h::Expr image_to_expr_operator2(h::Image<T> &that, h::Expr x, h::Expr y)
{
    return that(x,y);
}

template<typename T>
h::Expr image_to_expr_operator3(h::Image<T> &that, h::Expr x, h::Expr y, h::Expr z)
{
    return that(x,y,z);
}

template<typename T>
h::Expr image_to_expr_operator4(h::Image<T> &that, h::Expr x, h::Expr y, h::Expr z, h::Expr w)
{
    return that(x,y,z,w);
}

template<typename T>
h::Expr image_to_expr_operator5(h::Image<T> &that, std::vector<h::Expr> args_passed)
{
    return that(args_passed);
}

template<typename T>
h::Expr image_to_expr_operator6(h::Image<T> &that, std::vector<h::Var> args_passed)
{
    return that(args_passed);
}

template<typename T>
h::Expr image_to_expr_operator7(h::Image<T> &that, p::tuple &args_passed)
{
    std::vector<h::Var> var_args;
    std::vector<h::Expr> expr_args;
    const size_t args_len = p::len(args_passed);

    tuple_to_var_expr_vector("Image<T>", args_passed, var_args, expr_args);

    h::Expr ret;

    // We prioritize Args over Expr variant
    if(var_args.size() == args_len)
    {
        ret = that(var_args);
    }
    else
    {   user_assert(expr_args.size() == args_len) << "Not all image_to_expr_operator7 arguments where converted to Expr";
        ret = that(expr_args);
    }

    return ret;
}


template<typename T>
T image_call_operator0(h::Image<T> &that, int x)
{
    return that(x);
}

template<typename T>
T image_call_operator1(h::Image<T> &that, int x, int y)
{
    return that(x,y);
}

template<typename T>
T image_call_operator2(h::Image<T> &that, int x, int y, int z)
{
    return that(x,y,z);
}

template<typename T>
T image_call_operator3(h::Image<T> &that, int x, int y, int z, int w)
{
    return that(x,y,z,w);
}


template<typename T>
T image_to_setitem_operator0(h::Image<T> &that, int x, T value)
{
    return that(x) = value;
}

template<typename T>
T image_to_setitem_operator1(h::Image<T> &that, int x, int y, T value)
{
    return that(x, y) = value;
}

template<typename T>
T image_to_setitem_operator2(h::Image<T> &that, int x, int y, int z, T value)
{
    return that(x, y, z) = value;
}

template<typename T>
T image_to_setitem_operator3(h::Image<T> &that, int x, int y, int z, int w, T value)
{
    return that(x, y, z, w) = value;
}


template<typename T>
T image_to_setitem_operator4(h::Image<T> &that, p::tuple &args_passed, T value)
{
    std::vector<int> int_args;
    const size_t args_len = p::len(args_passed);
    for(size_t i=0; i < args_len; i+=1)
    {
        p::object o = args_passed[i];
        p::extract<int> int32_extract(o);

        if(int32_extract.check())
        {
            int_args.push_back(int32_extract());
        }
    }

    if(int_args.size() != args_len)
    {
        for(size_t j=0; j < args_len; j+=1)
        {
            p::object o = args_passed[j];
            const std::string o_str = p::extract<std::string>(p::str(o));
            printf("image_to_setitem_operator4 args_passed[%lu] == %s\n", j, o_str.c_str());
        }
        throw std::invalid_argument("image_to_setitem_operator4 only handles "
                                    "a tuple of (convertible to) int.");
    }

    switch(int_args.size())
    {
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

    return 0; // this line should never be reached
}


template<typename T>
std::string image_repr(const h::Image<T> &image)
{
    std::string repr;

    h::Buffer b = image;
    h::Type t = b.type();
    std::string suffix = "_???";
    if(t.is_float())
    {
        suffix = "_float";
    }
    else if(t.is_int())
    {
        suffix = "_int";
    }
    else if(t.is_uint())
    {
        suffix = "_uint";
    }
    else if(t.is_bool())
    {
        suffix = "_bool";
    }
    else if(t.is_handle())
    {
        suffix = "_handle";
    }

    boost::format f("<halide.Image%s%i; element_size %i bytes; "
                    "extent (%i %i %i %i); min (%i %i %i %i); stride (%i %i %i %i)>");

    repr = boost::str(f % suffix % t.bits % b.raw_buffer()->elem_size
                      % b.extent(0) % b.extent(1) % b.extent(2) % b.extent(3)
                      % b.min(0) % b.min(1) % b.min(2) % b.min(3)
                      % b.stride(0) % b.stride(1) % b.stride(2) % b.stride(3));

    return repr;
}

template<typename T>
h::Buffer image_to_buffer(h::Image<T> &image)
{
    return image; // class operator Buffer()
}

template<typename T>
boost::python::object get_type_function_wrapper()
{
    std::function<h::Type(h::Image<T> &)> return_type_func = [&](h::Image<T> &that)-> h::Type { return h::Buffer(that).type(); };
    auto call_policies = p::default_call_policies();
    typedef boost::mpl::vector<h::Type, h::Image<T> &> func_sig;
    return p::make_function(return_type_func, call_policies, p::arg("self"), func_sig());
}


template<typename T>
void defineImage_impl(const std::string suffix, const h::Type type)
{
    using h::Image;
    

    auto image_class =
            p::class_< Image<T>, p::bases<h::ImageBase> >(
                ("Image" + suffix).c_str(),
                "A reference-counted handle on a dense multidimensional array "
                "containing scalar values of type T. Can be directly accessed and "
                "modified. May have up to four dimensions. Color images are "
                "represented as three-dimensional, with the third dimension being "
                "the color channel. In general we store color images in "
                "color-planes, as opposed to packed RGB, because this tends to "
                "vectorize more cleanly.",
                p::init<>(p::arg("self"), "Construct an undefined image handle"))

            .def(p::init<int, int, int, int,std::string>(
                     (p::arg("self"), p::arg("x"), p::arg("y")=0, p::arg("z")=0, p::arg("w")=0, p::arg("name")=""),
                     "Allocate an image with the given dimensions."))

            .def(p::init<h::Buffer &>(p::args("self", "buf"),
                                      "Wrap a buffer in an Image object, so that we can directly "
                                      "access its pixels in a type-safe way."))

            .def(p::init<h::Realization &>(p::args("self", "r"),
                                           "Wrap a single-element realization in an Image object."))

            .def(p::init<buffer_t *, std::string>((p::arg("self"), p::arg("b"), p::arg("name")=""),
                                                  "Wrap a buffer_t in an Image object, so that we can access its pixels."))

            .def("__repr__", &image_repr<T>, p::arg("self"))

            .def("data", &Image<T>::data, p::arg("self"),
                 p::return_value_policy< p::return_opaque_pointer >(), // not sure this will do what we want
                 "Get a pointer to the element at the min location.")

            // These are the ImageBase methods
            .def("copy_to_host", &Image<T>::copy_to_host, p::arg("self"),
                 "Manually copy-back data to the host, if it's on a device. This "
                 "is done for you if you construct an image from a buffer, but "
                 "you might need to call this if you realize a gpu kernel into an "
                 "existing image")
            .def("defined", &Image<T>::defined, p::arg("self"),
                 "Check if this image handle points to actual data")
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
                 "into this image.")
            .def("set_min", &Image<T>::set_min,
                 (p::arg("self"), p::arg("m0"), p::arg("m1")=0, p::arg("m2")=0, p::arg("m3")=0),
                 "Set the min coordinates of a dimension.")
            ;

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
                 "one-dimensional images.")
            ;


    const std::string get_item_doc = "Construct an expression which loads from this image. "
                                     "The location is extended with enough implicit variables to match "
                                     "the dimensionality of the image (see \\ref Var::implicit)";

    // Access operators (to Expr, and to actual value)
    image_class

            .def("__getitem__", &image_to_expr_operator0<T>, p::arg("self"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator1<T>, p::args("self", "x"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator2<T>, p::args("self", "x", "y"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator3<T>, p::args("self", "x", "y", "z"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator4<T>, p::args("self", "x", "y", "z", "w"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator5<T>, p::args("self", "args_passed"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator6<T>, p::args("self", "args_passed"),
                 get_item_doc.c_str())
            .def("__getitem__", &image_to_expr_operator7<T>, p::args("self", "tuple"),
                 get_item_doc.c_str())

            // Note that for now we return copy values (not references like in the C++ API)
            .def("__call__", &image_call_operator0<T>, p::args("self", "x"),
                 "Assuming this image is one-dimensional, get the value of the element at position x")
            .def("__call__", &image_call_operator1<T>, p::args("self", "x", "y"),
                 "Assuming this image is two-dimensional, get the value of the element at position (x, y)")
            .def("__call__", &image_call_operator2<T>, p::args("self", "x", "y", "z"),
                 "Assuming this image is three-dimensional, get the value of the element at position (x, y, z)")
            .def("__call__", &image_call_operator3<T>, p::args("self", "x", "y", "z", "w"),
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
                 "set the value of the element at position indicated by tuple (x, y, z, w)")

            .def("buffer", &image_to_buffer<T>, p::args("self"),
                 "Cast to Halide::buffer")
            ;


    //        "Get a handle on the Buffer that this image holds"
    //        operator Buffer() const {
    //            return buffer;
    //        }

    //        "Convert this image to an argument to a halide pipeline."
    //        operator Argument() const {
    //            return Argument(buffer);
    //        }

    //        "Convert this image to an argument to an extern stage."
    //        operator ExternFuncArgument() const {
    //            return ExternFuncArgument(buffer);
    //        }

    //        "Treating the image as an Expr is equivalent to call it with no "
    //         "arguments. For example, you can say:\n\n"

    //         "\\code\n"
    //         "Image im(10, 10);\n"
    //         "Func f;\n"
    //         "f = im*2;\n"
    //         "\\endcode\n\n"
    //         "This will define f as a two-dimensional function with value at
    //         "position (x, y) equal to twice the value of the image at the
    //         "same location. "
    //        operator Expr() const {
    //            return (*this)(_);
    //        }

    p::implicitly_convertible<Image<T>, h::Buffer>();
    p::implicitly_convertible<Image<T>, h::Argument>();
    p::implicitly_convertible<Image<T>, h::Expr>();
    //p::implicitly_convertible<Image<T>, h::ExternFuncArgument>();

    return;
}

#ifdef USE_NUMPY

// To be used with care, since return object uses a  C++ pointer to data,
// use p::with_custodian_and_ward_postcall to link the objects lifetime
p::object raw_buffer_to_image(bn::ndarray &array, buffer_t &raw_buffer, const std::string &name)
{
    PyObject* obj = NULL;

    if(array.get_dtype() == bn::dtype::get_builtin<boost::uint8_t>())
    {
        h::Type t = h::UInt(8);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<boost::uint8_t> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::uint16_t>())
    {
        h::Type t = h::UInt(16);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<boost::uint16_t> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::uint32_t>())
    {
        h::Type t = h::UInt(32);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<boost::uint32_t> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::int8_t>())
    {
        h::Type t = h::Int(8);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<boost::int8_t> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::int16_t>())
    {
        h::Type t = h::Int(16);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<boost::int16_t> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::int32_t>())
    {
        h::Type t = h::Int(32);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<boost::int32_t> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<float>())
    {
        h::Type t = h::Float(32);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<float> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<double>())
    {
        h::Type t = h::Float(64);
        h::Buffer buffer(t, &raw_buffer, name);
        typedef h::Image<double> image_t;

        p::manage_new_object::apply<image_t *>::type converter;
        obj = converter( new image_t(buffer));
    }

    if(obj == NULL)
    {
        const std::string type_repr = p::extract<std::string>(p::str(array.get_dtype()));
        printf("numpy_to_image input array type: %s", type_repr.c_str());
        throw std::invalid_argument("numpy_to_image received an array of type not managed in Halide.");
    }


    const bool debug = false;
    if(debug)
    {
        const std::string type_repr = p::extract<std::string>(p::str(array.get_dtype()));

        printf("raw_buffer_to_image array of type '%s'. "
               "extent (%i, %i, %i, %i); stride (%i, %i, %i, %i)\n",
               type_repr.c_str(),
               raw_buffer.extent[0], raw_buffer.extent[1], raw_buffer.extent[2], raw_buffer.extent[3],
                raw_buffer.stride[0], raw_buffer.stride[1], raw_buffer.stride[2], raw_buffer.stride[3]);
    }


    p::object return_object = p::object( p::handle<>( obj ) );
    return return_object;
}


buffer_t ndarray_to_buffer_t(bn::ndarray &array)
{
    size_t num_elements = 0;
    for(size_t i = 0; i < array.get_nd(); i += 1)
    {
        if(i == 0)
        {
            num_elements = array.shape(i);
        }
        else
        {
            num_elements *= array.shape(i);
        }
    }

    if(num_elements == 0)
    {
        throw std::invalid_argument("numpy_to_image recieved an empty array");
    }

    if(array.get_nd() > 4)
    {
        throw std::invalid_argument("numpy_to_image received array with more than 4 dimensions. "
                                    "Halide only supports 4 or less dimensions");
    }

    // buffer_t initialization based on BufferContents::BufferContents
    buffer_t raw_buffer;
    raw_buffer.dev = 0;
    raw_buffer.host = reinterpret_cast<boost::uint8_t *>(array.get_data());
    raw_buffer.elem_size = array.get_dtype().get_itemsize(); // in bytes
    raw_buffer.host_dirty = false;
    raw_buffer.dev_dirty = false;

    for(int c=0; c < 4; c += 1)
    {
        if(c < array.get_nd())
        {
            raw_buffer.extent[c] = array.shape(c);
            // numpy counts stride in bytes, while Halide counts in number of elements
            user_assert((array.strides(c) % raw_buffer.elem_size) == 0);
            raw_buffer.stride[c] = array.strides(c) / raw_buffer.elem_size;
        }
        else
        {
            raw_buffer.extent[c] = 0;
            raw_buffer.stride[c] = 0;
        }
        raw_buffer.min[c] = 0;
    }

    return raw_buffer;
}

/// Will create a Halide::Image object pointing to the array data
p::object ndarray_to_image(bn::ndarray &array, const std::string name="")
{
    buffer_t raw_buffer = ndarray_to_buffer_t(array);
    return raw_buffer_to_image(array, raw_buffer, name);
}


namespace std
{
template<>
struct hash<h::Type>
{
    typedef h::Type argument_type;
    typedef std::size_t result_type;

    result_type operator()(argument_type const& t) const
    {
        size_t seed = 0;
        boost::hash_combine(seed, static_cast<int>(t.code));
        boost::hash_combine(seed, t.bits);
        boost::hash_combine(seed, t.width);
        return seed;
    }
};
}


bn::dtype type_to_dtype(const h::Type &t)
{
    using bn::dtype;

    const std::unordered_map<h::Type, dtype> m =
    {
        {h::UInt(8), dtype::get_builtin<boost::uint8_t>()},
        {h::UInt(16), dtype::get_builtin<boost::uint16_t>()},
        {h::UInt(32), dtype::get_builtin<boost::uint32_t>()},

        {h::Int(8), dtype::get_builtin<boost::int8_t>()},
        {h::Int(16), dtype::get_builtin<boost::int16_t>()},
        {h::Int(32), dtype::get_builtin<boost::int32_t>()},

        {h::Float(32), dtype::get_builtin<float>()},
        {h::Float(64), dtype::get_builtin<double>()}
    };

    if(m.find(t) == m.end())
    {
        printf("type_to_dtype received %s\n", type_repr(t).c_str());
        throw std::runtime_error("type_to_dtype received a Halide::Type with no known numpy dtype equivalent");
    }

    return m.at(t);
}


bn::ndarray image_to_ndarray(p::object image_object)
{
    p::extract<h::ImageBase &> image_base_extract(image_object);

    if(image_base_extract.check() == false)
    {
        throw std::invalid_argument("image_to_ndarray received an object that is not an Image<T>");
    }

    h::Buffer b = p::extract<h::Buffer>(image_object.attr("buffer")());
    user_assert(b.host_ptr() != NULL) << "image_to_ndarray received an image without host data";

    const h::Type& t = p::extract<h::Type &>(image_object.attr("type")());

    buffer_t &raw_buffer = *b.raw_buffer();
    std::vector<std::int32_t> shape_array(4), stride_array(4);
    std::copy(raw_buffer.extent, raw_buffer.extent + 4, shape_array.begin());
    std::copy(raw_buffer.stride, raw_buffer.stride + 4, stride_array.begin());


    // we make sure the array shape does not include the "0 extent" dimensions
    // we always keep at least one dimension (even if zero size)
    for(size_t i = 3; i > 0; i -= 1)
    {
        if(shape_array[i] == 0)
        {
            shape_array.pop_back();
            stride_array.pop_back();
        }
    }

    // numpy counts stride in bytes, while Halide counts in number of elements
    for(size_t i = 0; i < 4; i += 1)
    {
        stride_array[i] *= raw_buffer.elem_size;
    }

    return bn::from_data(
                b.host_ptr(),
                type_to_dtype(t),
                shape_array,
                stride_array,
                image_object);
}


#endif


template<typename T, typename ...Args>
p::object create_image_object(Args ...args)
{
    typedef h::Image<T> ImageType;
    typedef typename p::manage_new_object::apply<ImageType *>::type converter_t;
    converter_t converter;
    PyObject* obj = converter( new ImageType(args...) );
    return p::object( p::handle<>( obj ) );
}


// C++ fun, variadic template recursive function !
template<typename PixelTypes>
p::object create_image0_impl(h::Type type)
{
    typedef typename boost::mpl::front<PixelTypes>::type pixel_t;
    if(h::type_of<pixel_t>() == type)
    {
        return create_image_object<pixel_t>();
    }
    else
    {
        typedef typename boost::mpl::pop_front<PixelTypes>::type pixels_types_tail_t;
        return create_image0_impl<pixels_types_tail_t>(type); // keep recursing
    }
}

template<>
p::object create_image0_impl<boost::mpl::l_end::type>(h::Type type)
{ // end of recursion, did not find a matching type
    printf("create_image0_impl<boost::mpl::l_end::type> received %s\n", type_repr(type).c_str());
    throw std::invalid_argument("ImageFactory::create_image0_impl received type not handled");
    return p::object();
}



// C++ fun, variadic template recursive function !
// (if you wonder why struct::operator() and not a function,
// see http://artofsoftware.org/2012/12/20/c-template-function-partial-specialization )
template<typename PixelTypes, typename ...Args>
struct create_image1_impl_t
{
    p::object operator()(h::Type type, Args... args)
    {
        typedef typename boost::mpl::empty<PixelTypes>::type pixels_types_list_is_empty_t;
        if(pixels_types_list_is_empty_t::value == true)
        {
            // end of recursion, did not find a matching type
            printf("create_image1_impl<end_of_recursion_t> received %s\n", type_repr(type).c_str());
            throw std::invalid_argument("ImageFactory::create_image1_impl received type not handled");
            return p::object();
        }

        typedef typename boost::mpl::front<PixelTypes>::type pixel_t;
        if(h::type_of<pixel_t>() == type)
        {
            return create_image_object<pixel_t, Args...>(args...);
        }
        else
        { // keep recursing
            typedef typename boost::mpl::pop_front<PixelTypes>::type pixels_types_tail_t;
            return create_image1_impl_t<pixels_types_tail_t, Args...>()(type, args...);
        }
    }
};


template<typename ...Args>
struct create_image1_impl_t<boost::mpl::l_end::type, Args...>
{
    p::object operator()(h::Type type, Args... args)
    {
        // end of recursion, did not find a matching type
        printf("create_image1_impl<boost::mpl::l_end::type> received %s\n", type_repr(type).c_str());
        throw std::invalid_argument("ImageFactory::create_image1_impl received type not handled");
        return p::object();
    }
};


struct ImageFactory
{
    typedef boost::mpl::list<boost::uint8_t, boost::uint16_t, boost::uint32_t,
    boost::int8_t, boost::int16_t, boost::int32_t,
    float, double> pixel_types_t;

    static p::object create_image0(h::Type type)
    {
        return create_image0_impl<pixel_types_t>(type);
    }

    static p::object create_image1(h::Type type, int x, int y, int z, int w, std::string name)
    {
        return create_image1_impl_t<pixel_types_t, int, int, int, int, std::string>()(type, x, y, z, w, name);
    }

    static p::object create_image2(h::Type type, h::Buffer &buf)
    {
        return create_image1_impl_t<pixel_types_t, h::Buffer &>()(type, buf);
    }

    static p::object create_image3(h::Type type, h::Realization &r)
    {
        return create_image1_impl_t<pixel_types_t, h::Realization &>()(type, r);
    }

    static p::object create_image4(h::Type type, buffer_t *b, std::string name)
    {
        return create_image1_impl_t<pixel_types_t, buffer_t *, std::string>()(type, b, name);
    }

};



void defineImage()
{
    // only defined so that Boost.Python knows about it,
    // not methods exposed
    p::class_<h::ImageBase>("ImageBase",
                            "Base class shared by all Image<T> classes.",
                            p::no_init);

    defineImage_impl<uint8_t>("_uint8", h::UInt(8));
    defineImage_impl<uint16_t>("_uint16", h::UInt(16));
    defineImage_impl<uint32_t>("_uint32", h::UInt(32));

    defineImage_impl<int8_t>("_int8", h::Int(8));
    defineImage_impl<int16_t>("_int16", h::Int(16));
    defineImage_impl<int32_t>("_int32", h::Int(32));

    defineImage_impl<float>("_float32", h::Float(32));
    defineImage_impl<double>("_float64", h::Float(64));


    // "Image" will look as a class, but instead it will be simply a factory method
    p::def("Image", &ImageFactory::create_image0, p::args("type"),
           "Construct an undefined image handle of type T");

    p::def("Image", &ImageFactory::create_image1,
           (p::arg("type"), p::arg("x"), p::arg("y")=0, p::arg("z")=0, p::arg("w")=0, p::arg("name")=""),
           "Allocate an image of type T with the given dimensions.");

    p::def("Image", &ImageFactory::create_image2, p::args("type", "buf"),
           p::with_custodian_and_ward_postcall<0, 2>(), // the buffer reference count is increased
           "Wrap a buffer in an Image object of type T, "
           "so that we can directly access its pixels in a type-safe way.");

    p::def("Image", &ImageFactory::create_image3, p::args("type", "r"),
           p::with_custodian_and_ward_postcall<0, 2>(), // the realization reference count is increased
           "Wrap a single-element realization in an Image object of type T.");

    p::def("Image", &ImageFactory::create_image4, (p::arg("type"), p::arg("b"), p::arg("name")=""),
           p::with_custodian_and_ward_postcall<0, 2>(), // the buffer_t reference count is increased
           "Wrap a buffer_t in an Image object of type T, so that we can access its pixels.");


#ifdef USE_NUMPY
    bn::initialize();

    p::def("ndarray_to_image", &ndarray_to_image, (p::arg("array"), p::arg("name")=""),
           p::with_custodian_and_ward_postcall<0, 1>(), // the array reference count is increased
           "Converts a numpy array into a Halide::Image."
           "Will take into account the array size, dimensions, and type."
           "Created Image refers to the array data (no copy).");

    p::def("Image", &ndarray_to_image, (p::arg("array"), p::arg("name")=""),
           p::with_custodian_and_ward_postcall<0, 1>(), // the array reference count is increased
           "Wrap numpy array in a Halide::Image."
           "Will take into account the array size, dimensions, and type."
           "Created Image refers to the array data (no copy).");

    p::def("image_to_ndarray", &image_to_ndarray, p::arg("image"),
           p::with_custodian_and_ward_postcall<0, 1>(), // the image reference count is increased
           "Creates a numpy array from a Halide::Image."
           "Will take into account the Image size, dimensions, and type."
           "Created ndarray refers to the Image data (no copy).");
#endif

    return;
}


