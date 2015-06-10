#include "Image.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Image.h"

#include <string>

namespace h = Halide;


template<typename T>
T image_call_operator0(h::Image<T> &that)
{
    return that();
}

template<typename T>
T image_call_operator1(h::Image<T> &that, int x)
{
    return that(x);
}

template<typename T>
T image_call_operator2(h::Image<T> &that, int x, int y)
{
    return that(x,y);
}

template<typename T>
T image_call_operator3(h::Image<T> &that, int x, int y, int z)
{
    return that(x,y,z);
}

template<typename T>
T image_call_operator4(h::Image<T> &that, int x, int y, int z, int w)
{
    return that(x,y,z,w);
}


template<typename T>
void defineImage_impl(const std::string suffix, const h::Type type)
{
    using h::Image;
    namespace p = boost::python;
    using p::self;


    p::class_< Image<T> >(("Image" + suffix).c_str(),
                          "A reference-counted handle on a dense multidimensional array "
                          "containing scalar values of type T. Can be directly accessed and "
                          "modified. May have up to four dimensions. Color images are "
                          "represented as three-dimensional, with the third dimension being "
                          "the color channel. In general we store color images in "
                          "color-planes, as opposed to packed RGB, because this tends to "
                          "vectorize more cleanly.",
                          p::init<>("Construct an undefined image handle"))

            .def(p::init<int, int, int, int,std::string>(
                     (p::arg("x"), p::arg("y")=0, p::arg("z")=0, p::arg("w")=0, p::arg("name")=""),
                     "Allocate an image with the given dimensions."))

            .def(p::init<h::Buffer &>(p::arg("buf"),
                                      "Wrap a buffer in an Image object, so that we can directly "
                                      "access its pixels in a type-safe way."))

            .def(p::init<h::Realization &>(p::arg("r"),
                                           "Wrap a single-element realization in an Image object."))

            .def(p::init<buffer_t *>((p::arg("b"), p::arg("name")=""),
                                     "Wrap a buffer_t in an Image object, so that we can access its pixels."))

            .def("data", &Image<T>::data,
                 p::return_value_policy< p::return_opaque_pointer >(), // not sure this will do what we want
                 "Get a pointer to the element at the min location.")

            // These are the ImageBase methods
            .def("copy_to_host", &Image<T>::copy_to_host,
                 "Manually copy-back data to the host, if it's on a device. This "
                 "is done for you if you construct an image from a buffer, but "
                 "you might need to call this if you realize a gpu kernel into an "
                 "existing image")
            .def("defined", &Image<T>::defined,
                 "Check if this image handle points to actual data")
            .def("set_host_dirty", &Image<T>::set_host_dirty,
                 p::args("dirty") = true,
                 "Mark the buffer as dirty-on-host.  is done for you if you "
                 "construct an image from a buffer, but you might need to call "
                 "this if you realize a gpu kernel into an existing image, or "
                 "modify the data via some other back-door.")
            .def_readonly("type", type,
                          "Return Type instance for the data type of the image.")
            .def("channels", &Image<T>::channels,
                 "Get the extent of dimension 2, which by convention we use as"
                 "the number of color channels (often 3). Unlike extent(2), "
                 "returns one if the buffer has fewer than three dimensions.")
            .def("dimensions", &Image<T>::dimensions,
                 "Get the dimensionality of the data. Typically two for grayscale images, and three for color images.")
            .def("stride", &Image<T>::stride, p::arg("dim"),
                 "Get the number of elements in the buffer between two adjacent "
                 "elements in the given dimension. For example, the stride in "
                 "dimension 0 is usually 1, and the stride in dimension 1 is "
                 "usually the extent of dimension 0. This is not necessarily true though.")
            .def("extent", &Image<T>::extent, p::arg("dim"),
                 "Get the size of a dimension.")
            .def("width", &Image<T>::width,
                 "Get the extent of dimension 0, which by convention we use as "
                 "the width of the image. Unlike extent(0), returns one if the "
                 "buffer is zero-dimensional.")
            .def("height", &Image<T>::height,
                 "Get the extent of dimension 1, which by convention we use as "
                 "the height of the image. Unlike extent(1), returns one if the "
                 "buffer has fewer than two dimensions.")
            .def("left", &Image<T>::left,
                 "Get the minimum coordinate in dimension 0, which by convention "
                 "is the coordinate of the left edge of the image. Returns zero "
                 "for zero-dimensional images.")
            .def("right", &Image<T>::right,
                 "Get the maximum coordinate in dimension 0, which by convention "
                 "is the coordinate of the right edge of the image. Returns zero "
                 "for zero-dimensional images.")
            .def("top", &Image<T>::top,
                 "Get the minimum coordinate in dimension 1, which by convention "
                 "is the top of the image. Returns zero for zero- or "
                 "one-dimensional images.")
            .def("bottom", &Image<T>::bottom,
                 "Get the maximum coordinate in dimension 1, which by convention "
                 "is the bottom of the image. Returns zero for zero- or "
                 "one-dimensional images.")
            .def("bottom", &Image<T>::bottom,
                 "Get the maximum coordinate in dimension 1, which by convention "
                 "is the bottom of the image. Returns zero for zero- or "
                 "one-dimensional images.")

            // Access operators


            // Note that for now we return copy values (not references like in the C++ API)
            //.def("__call__", &image_call_operator0<T>)


//            /** Construct an expression which loads from this image. The
//             * location is extended with enough implicit variables to match
//             * the dimensionality of the image (see \ref Var::implicit) */
//            // @{
//            EXPORT Expr operator()() const;
//            EXPORT Expr operator()(Expr x) const;
//            EXPORT Expr operator()(Expr x, Expr y) const;
//            EXPORT Expr operator()(Expr x, Expr y, Expr z) const;
//            EXPORT Expr operator()(Expr x, Expr y, Expr z, Expr w) const;
//            EXPORT Expr operator()(std::vector<Expr>) const;
//            EXPORT Expr operator()(std::vector<Var>) const;
//            // @}


            .def("__call__", &image_call_operator1<T>, p::args("self", "x"),
                 //p::return_internal_reference<1>(),
                 "Assuming this image is one-dimensional, get the value of the element at position x")

            .def("__call__", &image_call_operator2<T>, p::args("self", "x", "y"),
                 "Assuming this image is two-dimensional, get the value of the element at position (x, y)")

            .def("__call__", &image_call_operator3<T>, p::args("self", "x", "y", "z"),
                 "Assuming this image is three-dimensional, get the value of the element at position (x, y, z)")

            .def("__call__", &image_call_operator4<T>,
                 //p::return_internal_reference<1>(),
                 p::args("self", "x", "y", "z", "w"),
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

void defineImage()
{

    //defineImage_impl<uint8_t>("_uint8", h::UInt(8));
    //    defineImage_impl<uint16_t>("_uint16", h::UInt(16));
    //    defineImage_impl<uint32_t>("_uint32", h::UInt(32));

    //    defineImage_impl<int8_t>("_int8", h::Int(8));
    //    defineImage_impl<int16_t>("_int16", h::Int(16));
    defineImage_impl<int32_t>("_int32", h::Int(32));

    //    defineImage_impl<float>("_float32", h::Float(32));
    //    defineImage_impl<double>("_float64", h::Float(64));


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


