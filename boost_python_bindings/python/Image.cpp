#include "Image.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#define USE_BOOST_NUMPY

#ifdef USE_BOOST_NUMPY
#include <boost/numpy.hpp>
#include <boost/cstdint.hpp>
#endif



#include "../../src/Image.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;


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
void defineImage_impl(const std::string suffix, const h::Type type)
{
    using h::Image;
    using p::self;


    p::class_< Image<T> >(("Image" + suffix).c_str(),
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
            .def_readonly("type", type,
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
            .def("bottom", &Image<T>::bottom, p::arg("self"),
                 "Get the maximum coordinate in dimension 1, which by convention "
                 "is the bottom of the image. Returns zero for zero- or "
                 "one-dimensional images.")

            // Access operators (to Expr, and to actual value)

            // should these be __call__ "a(b)" or __getitem___ "a[b]" ?

            .def("__call__", &image_to_expr_operator0<T>, p::arg("self"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")
            .def("__call__", &image_to_expr_operator1<T>, p::args("self", "x"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")
            .def("__call__", &image_to_expr_operator2<T>, p::args("self", "x", "y"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")
            .def("__call__", &image_to_expr_operator3<T>, p::args("self", "x", "y", "z"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")
            .def("__call__", &image_to_expr_operator4<T>, p::args("self", "x", "y", "z", "w"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")
            .def("__call__", &image_to_expr_operator5<T>, p::args("self", "args_passed"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")
            .def("__call__", &image_to_expr_operator6<T>, p::args("self", "args_passed"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit)")


            // Note that for now we return copy values (not references like in the C++ API)
            .def("__call__", &image_call_operator0<T>, p::args("self", "x"),
                 "Assuming this image is one-dimensional, get the value of the element at position x")
            .def("__call__", &image_call_operator1<T>, p::args("self", "x", "y"),
                 "Assuming this image is two-dimensional, get the value of the element at position (x, y)")
            .def("__call__", &image_call_operator2<T>, p::args("self", "x", "y", "z"),
                 "Assuming this image is three-dimensional, get the value of the element at position (x, y, z)")
            .def("__call__", &image_call_operator3<T>, p::args("self", "x", "y", "z", "w"),
                 "Assuming this image is four-dimensional, get the value of the element at position (x, y, z, w)")

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

    //    p::implicitly_convertible<Image<T>, h::Buffer>();
    //    p::implicitly_convertible<Image<T>, h::Argument>();
    //    p::implicitly_convertible<Image<T>, h::Expr>();

    return;
}


#ifdef USE_BOOST_NUMPY
p::object ndarray_to_image(const boost::numpy::ndarray &array, const std::string name="")
{
    namespace bn = boost::numpy;

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

    h::Type t;

    if(array.get_dtype() == bn::dtype::get_builtin<boost::uint8_t>())
    {
        t = h::UInt(8);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::uint16_t>())
    {
        t = h::UInt(16);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::uint32_t>())
    {
        t = h::UInt(32);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::int8_t>())
    {
        t = h::Int(8);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::int16_t>())
    {
        t = h::Int(16);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<boost::int32_t>())
    {
        t = h::Int(32);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<float>())
    {
        t = h::Float(32);
    }
    else if(array.get_dtype() == bn::dtype::get_builtin<double>())
    {
        t = h::Float(64);
    }
    else
    {
        const std::string type_repr = p::extract<std::string>(p::str(array.get_dtype()));
        printf("numpy_to_image input array type: %s", type_repr.c_str());
        throw std::invalid_argument("numpy_to_image received an array of type not managed in Halide.");
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
            raw_buffer.stride[c] = array.strides(c);
        }
        else
        {
            raw_buffer.extent[c] = 0;
            raw_buffer.stride[c] = 0;
        }
        raw_buffer.min[c] = 0;
    }

    const bool debug = true;
    if(debug)
    {
        const std::string type_repr = p::extract<std::string>(p::str(array.get_dtype()));

        printf("numpy_to_image array of type '%s'. "
               "extent (%i, %i, %i, %i); stride (%i, %i, %i, %i)",
               type_repr.c_str(),
               raw_buffer.extent[0], raw_buffer.extent[1], raw_buffer.extent[2], raw_buffer.extent[3],
                raw_buffer.stride[0], raw_buffer.stride[1], raw_buffer.stride[2], raw_buffer.stride[3]);
    }

    p::manage_new_object::apply<h::Buffer *>::type converter;
    PyObject* obj = converter( new h::Buffer(t, &raw_buffer, name));
    p::object return_object = p::object( p::handle<>( obj ) );

    return return_object;
}
#endif

void defineImage()
{

    defineImage_impl<uint8_t>("_uint8", h::UInt(8));
    defineImage_impl<uint16_t>("_uint16", h::UInt(16));
    defineImage_impl<uint32_t>("_uint32", h::UInt(32));

    defineImage_impl<int8_t>("_int8", h::Int(8));
    defineImage_impl<int16_t>("_int16", h::Int(16));
    defineImage_impl<int32_t>("_int32", h::Int(32));

    defineImage_impl<float>("_float32", h::Float(32));
    defineImage_impl<double>("_float64", h::Float(64));


#ifdef USE_BOOST_NUMPY

    boost::numpy::initialize();

    p::def("ndarray_to_image", &ndarray_to_image, (p::arg("array"), p::arg("name")=""),
           "Converts a numpy array into a Halide::Image."
           "Will take into account the array size, dimensions, and type.");

#endif

    //    class Image(object):
    //        """
    //        Construct an Image::

    //        Image(contents, [scale=None])
    //              Image([typeval=Int(n), UInt(n), Float(n), Bool()], contents, [scale=None])

    //              The contents can be:

    //              - PIL image
    //              - Numpy array
    //              - Filename of existing file (typeval defaults to UInt(8))
    //              - halide.Buffer
    //              - An int or tuple -- constructs an n-D image (typeval argument is required).

    //              The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr.

    //              If not provided (or None) then the typeval is inferred from the input argument.

    //              For PIL, numpy, and filename constructors, if scale is provided then the input is scaled by the floating point scale factor
    //              (for example, Image(filename, UInt(16), 1.0/256) reads a UInt(16) image rescaled to have maximum value 255). If omitted,
    //              scale is set to convert between source and target data type ranges, where int types range from 0 to maxval, and float types
    //              range from 0 to 1.
    //              """
    //              def __new__(cls, typeval, contents=None, scale=None):
    //              if contents is None:                        # If contents is None then Image(contents) is assumed as the call
    //              (typeval, contents) = (None, typeval)

    //              if isinstance(contents, str_types):    # Convert filename to PIL image
    //              contents = PIL.open(contents)
    //              if typeval is None:
    //              typeval = UInt(8)
    //              contents = numpy.asarray(contents, 'uint8')

    //              if hasattr(contents, 'putpixel'):           # Convert PIL images to numpy
    //              contents = numpy.asarray(contents)

    //              if typeval is None:
    //              if hasattr(contents, 'type'):
    //              typeval = contents.type()
    //              elif isinstance(contents, numpy.ndarray):
    //              typeval = _numpy_to_type(contents)
    //              elif isinstance(contents, Realization):
    //              typeval = contents.as_vector()[0].type()
    //              else:
    //              raise ValueError('unknown halide.Image constructor %r' % contents)

    //              assert isinstance(typeval, TypeType), typeval
    //              sig = (typeval.bits, typeval.is_int(), typeval.is_uint(), typeval.is_float())

    //              if sig == (8, True, False, False):
    //              C = Image_int8
    //              target_dtype = 'int8'
    //              elif sig == (16, True, False, False):
    //              C = Image_int16
    //              target_dtype = 'int16'
    //              elif sig == (32, True, False, False):
    //              C = Image_int32
    //              target_dtype = 'int32'
    //              elif sig == (8, False, True, False):
    //              C = Image_uint8
    //              target_dtype = 'uint8'
    //              elif sig == (16, False, True, False):
    //              C = Image_uint16
    //              target_dtype = 'uint16'
    //              elif sig == (32, False, True, False):
    //              C = Image_uint32
    //              target_dtype = 'uint32'
    //              elif sig == (32, False, False, True):
    //              C = Image_float32
    //              target_dtype = 'float32'
    //              elif sig == (64, False, False, True):
    //              C = Image_float64
    //              target_dtype = 'float64'
    //              else:
    //              raise ValueError('unimplemented halide.Image type signature %r' % typeval)

    //              if isinstance(contents, numpy.ndarray):
    //              if scale is None:
    //              in_range = _numpy_to_type(contents).typical_max()
    //              out_range = typeval.typical_max()
    //              if in_range != out_range:
    //              scale = float(out_range)/in_range
    //              if scale is not None:
    //              contents = numpy.asarray(numpy.asarray(contents,'float')*float(scale), target_dtype)
    //              return _numpy_to_image(contents, target_dtype, C)
    //              elif isinstance(contents, ImageTypes+(ImageParamType,BufferType,Realization)):
    //              return C(contents)
    //              elif isinstance(contents, tuple) or isinstance(contents, list) or isinstance(contents, integer_types):
    //              if isinstance(contents, integer_types):
    //              contents = (contents,)
    //              if not all(isinstance(x, integer_types) for x in contents):
    //              raise ValueError('halide.Image constructor did not receive a tuple of ints for Image size')
    //              return C(*contents)
    //              else:
    //              raise ValueError('unknown Image constructor contents %r' % contents)

    //              def show(self, maxval=None):
    //              """
    //              Shows an Image instance on the screen, by converting through numpy and PIL.

    //              If maxval is not None then rescales so that bright white is equal to maxval.
    //              """

    //              def save(self, filename, maxval=None):
    //              """
    //              Save an Image (converted via PIL).

    //              If maxval is not None then rescales so that bright white is equal to maxval.
    //              """

    //              def to_pil(self):
    //              """
    //              Convert to PIL (Python Imaging Library) image.

    //              The fromarray() constructor in PIL does not work due to ignoring array stride so this is a workaround.
    //              """

    //              def tostring(self):
    //              """
    //              Convert to str.
    //              """

    //              def set(self, contents):
    //              """
    //              Sets contents of Image or ImageParam::

    //              set(Image, Buffer)
    //              set(ImageParam, Buffer|Image)
    //              """



    return;
}


