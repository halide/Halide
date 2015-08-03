#include "Buffer.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "make_array.h"
#include "no_compare_indexing_suite.h"

#include <boost/format.hpp>

#include "../../src/Buffer.h"
#include "Type.h" // for the repr function

#include <vector>
#include <string>

namespace h = Halide;

/// helper function to access &(Buffer::operator Argument)
h::Argument buffer_to_argument(h::Buffer &that)
{
    return that;
}

size_t host_ptr_as_int(h::Buffer &that)
{
    return reinterpret_cast<size_t>(that.host_ptr());
}

std::string buffer_t_repr(const buffer_t &that)
{
    boost::format format("<buffer_t [host 0x%X (dirty %i)] [dev 0x%X (dirty %i)] elem_size %i "
                         "extent (%i %i %i %i) min (%i %i %i %i) stride (%i %i %i %i)>");
    std::string repr =
            boost::str(format
                       % reinterpret_cast<size_t>(that.host) % that.host_dirty
                       % that.dev % that.dev_dirty
                       % that.elem_size
                       % that.extent[0] % that.extent[1] % that.extent[2] % that.extent[3]
            % that.min[0] % that.min[1] % that.min[2] % that.min[3]
            % that.stride[0] % that.stride[1] % that.stride[2] % that.stride[3]
            );
    return repr;
}


void define_buffer_t()
{
    namespace h = Halide;
    namespace p = boost::python;

    p::class_<buffer_t>("buffer_t",
                        " The raw representation of an image passed around by generated "
                        "Halide code. It includes some stuff to track whether the image is "
                        "not actually in main memory, but instead on a device (like a GPU).",
                        p::init<>(p::arg("self")))
            .def_readwrite("dev", &buffer_t::dev, "A device-handle for e.g. GPU memory used to back this buffer.")
            .def_readwrite("host", &buffer_t::host, "A pointer to the start of the data in main memory.")
            .add_property("extent", make_array(&buffer_t::extent), "The size of the buffer in each dimension. int32_t[4]")
            .add_property("stride", make_array(&buffer_t::stride),
                          "Gives the spacing in memory between adjacent elements in the "
                          "given dimension.  The correct memory address for a load from "
                          "this buffer at position x, y, z, w is:\n"
                          "host + (x * stride[0] + y * stride[1] + z * stride[2] + w * stride[3]) * elem_size\n"
                          "By manipulating the strides and extents you can lazily crop, "
                          "transpose, and even flip buffers without modifying the data.")
            .add_property("min", make_array(&buffer_t::min),
                          "Buffers often represent evaluation of a Func over some domain. "
                          "The min field encodes the top left corner of the domain.")
            .def_readwrite("elem_size", &buffer_t::elem_size,
                           "How many bytes does each buffer element take. This may be "
                           "replaced with a more general type code in the future.")
            .def_readwrite("host_dirty", &buffer_t::host_dirty,
                           "This should be true if there is an existing device allocation "
                           "mirroring this buffer, and the data has been modified on the host side.")
            .def_readwrite("dev_dirty", &buffer_t::dev_dirty,
                           "This should be true if there is an existing device allocation "
                           "mirroring this buffer, and the data has been modified on the device side.")
            .def("__repr__", &buffer_t_repr, p::arg("self"),
                 "Return a string containing a printable representation of a buffer_t object.")
            ;


    return;
}


h::Buffer *buffer_constructor0(h::Type t, int x_size, int y_size, int z_size, int w_size,
                               const std::string name)
{
    return new h::Buffer(t, x_size, y_size, z_size, w_size, NULL, name);
}


std::string buffer_repr(const h::Buffer &that)
{
    std::string repr;
    if(that.defined())
    {
        const h::Type &t = that.type();
        boost::format format("<halide.Buffer named '%s' of type %s(%i) containing %s>");

        repr = boost::str(format % that.name()
                          % type_code_to_string(t) % t.bits
                          % buffer_t_repr(*that.raw_buffer()));
    }
    else
    {
        boost::format format("<halide.Buffer named '%s' (data not yet defined)>");
        repr = boost::str(format % that.name());
    }

    return repr;
}


void defineBuffer()
{

    using Halide::Buffer;
    namespace p = boost::python;

    define_buffer_t();

    p::class_<Buffer>("Buffer",
                      "The internal representation of an image, or other dense array "
                      "data. The Image type provides a typed view onto a buffer for the "
                      "purposes of direct manipulation. A buffer may be stored in main "
                      "memory, or some other memory space (e.g. a gpu). If you want to use "
                      "this as an Image, see the Image class. Casting a Buffer to an Image "
                      "will do any appropriate copy-back. This class is a fairly thin "
                      "wrapper on a buffer_t, which is the C-style type Halide uses for "
                      "passing buffers around.",
                      p::init<>(p::arg("self")))
            .def("__init__", p::make_constructor(&buffer_constructor0, p::default_call_policies(),
                                                 (/*p::arg("self"),*/ p::arg("type"),
                                                  p::arg("x_size")=0, p::arg("y_size")=0, p::arg("z_size")=0, p::arg("w_size")=0,
                                                  p::arg("name")="")))
            .def(p::init<h::Type, int, int, int, int, uint8_t*, std::string>(
                     (p::arg("self"), p::arg("type"),
                      p::arg("x_size")=0, p::arg("y_size")=0, p::arg("z_size")=0, p::arg("w_size")=0,
                      p::arg("data")=NULL, p::arg("name")="")))
            .def(p::init<h::Type, std::vector<int32_t>, uint8_t*, std::string>(
                     (p::arg("self"), p::arg("type"), p::arg("sizes"), p::arg("data")=NULL, p::arg("name")="")))
            .def(p::init<h::Type, buffer_t *, std::string>(
                     (p::arg("self"), p::arg("type"), p::arg("buf"), p::arg("name")="")))

            .def("host_ptr", &Buffer::host_ptr, p::arg("self"),
                 //p::return_internal_reference<1>(),
                 p::return_value_policy< p::return_opaque_pointer >(), // not sure this will do what we want
                 "Get a pointer to the host-side memory.")
            .def("host_ptr_as_int", &host_ptr_as_int, p::arg("self"),
                 "Get a pointer to the host-side memory. Use with care.")
            .def("raw_buffer", &Buffer::raw_buffer, p::arg("self"),
                 p::return_internal_reference<1>(),
                 "Get a pointer to the raw buffer_t struct that this class wraps.")

            .def("device_handle", &Buffer::device_handle, p::arg("self"),
                 "Get the device-side pointer/handle for this buffer. Will be "
                 "zero if no device was involved in the creation of this buffer.")

            .def("host_dirty", &Buffer::host_dirty, p::arg("self"),
                 "Has this buffer been modified on the cpu since last copied to a "
                 "device. Not meaningful unless there's a device involved.")
            .def("set_host_dirty", &Buffer::set_host_dirty, (p::arg("self"), p::arg("dirty") = true),
                 "Let Halide know that the host-side memory backing this buffer "
                 "has been externally modified. You shouldn't normally need to "
                 "call this, because it is done for you when you cast a Buffer to "
                 "an Image in order to modify it.")

            .def("device_dirty", &Buffer::device_dirty, p::arg("self"),
                 "Has this buffer been modified on device since last copied to "
                 "the cpu. Not meaninful unless there's a device involved.")
            .def("set_device_dirty", &Buffer::set_device_dirty, (p::arg("self"), p::arg("dirty") = true),
                 "Let Halide know that the device-side memory backing this "
                 "buffer has been externally modified, and so the cpu-side memory "
                 "is invalid. A copy-back will occur the next time you cast this "
                 "Buffer to an Image, or the next time this buffer is accessed on "
                 "the host in a halide pipeline.")

            .def("dimensions", &Buffer::dimensions, p::arg("self"),
                 "Get the dimensionality of this buffer. Uses the convention "
                 "that the extent field of a buffer_t should contain zero when "
                 "the dimensions end.")
            .def("extent", &Buffer::extent, p::args("self", "dim"),
                 "Get the extent of this buffer in the given dimension.")
            .def("stride", &Buffer::stride, p::args("self", "dim"),
                 "Get the number of bytes between adjacent elements of this buffer along the given dimension.")
            .def("min", &Buffer::min, p::args("self", "dim"),
                 "Get the coordinate in the function that this buffer represents "
                 "that corresponds to the base address of the buffer.")
            .def("set_min", &Buffer::set_min,
                 (p::arg("self"), p::arg("m0"), p::arg("m1")=0, p::arg("m2")=0, p::arg("m3")=0),
                 "Set the coordinate in the function that this buffer represents "
                 "that corresponds to the base address of the buffer.")
            .def("type", &Buffer::type, p::arg("self"),
                 "Get the Halide type of the contents of this buffer.")
            .def("same_as", &Buffer::same_as, p::args("self", "other"),
                 "Compare two buffers for identity (not equality of data).")
            .def("defined", &Buffer::defined, p::arg("self"),
                 "Check if this buffer handle actually points to data.")
            .def("name", &Buffer::name, p::arg("self"),
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get the runtime name of this buffer used for debugging.")

            .def("to_Argument", &buffer_to_argument, p::arg("self"), //&(Buffer::operator Argument),
                 "Convert this buffer to an argument to a halide pipeline.")

            .def("copy_to_host", &Buffer::copy_to_host, p::arg("self"),
                 "If this buffer was created *on-device* by a jit-compiled "
                 "realization, then copy it back to the cpu-side memory. "
                 "This is usually achieved by casting the Buffer to an Image.")
            .def("copy_to_device", &Buffer::copy_to_device, p::arg("self"),
                 "If this buffer was created by a jit-compiled realization on a "
                 "device-aware target (e.g. PTX), then copy the cpu-side data to "
                 "the device-side allocation. TODO: I believe this currently "
                 "aborts messily if no device-side allocation exists. You might "
                 "think you want to do this because you've modified the data "
                 "manually on the host before calling another Halide pipeline, "
                 "but what you actually want to do in that situation is set the "
                 "host_dirty bit so that Halide can manage the copy lazily for "
                 "you. Casting the Buffer to an Image sets the dirty bit for you.")
            .def("free_dev_buffer", &Buffer::free_dev_buffer, p::arg("self"),
                 "If this buffer was created by a jit-compiled realization on a "
                 "device-aware target (e.g. PTX), then free the device-side "
                 "allocation, if there is one. Done automatically when the last "
                 "reference to this buffer dies.")

            .def("__repr__", &buffer_repr, p::arg("self"))
            ;

    p::implicitly_convertible<Buffer, h::Argument>();

    p::class_< std::vector<Buffer> >("BuffersVector")
            .def( no_compare_indexing_suite< std::vector<Buffer> >() );

    return;
}
