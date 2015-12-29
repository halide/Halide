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

std::string buffer_t_repr(const halide_buffer_t &that)
{

    std::ostringstream sstr;
    sstr << "<halide_buffer_t "
         << "[host " << (void *)(that.host) << "] "
         << "[device " << (void *)(that.device) << "] "
         << "[flags " << that.flags << "] "
         << "[type " << type_code_to_string(that.type) << "] "
         << "[dimensions " << that.dimensions << "] ";
    for (int i = 0; i < that.dimensions; i++) {
        sstr << "[dimension " << i
             << " min " << that.dim[i].min
             << " extent " << that.dim[i].extent
             << " stride " << that.dim[i].stride
             << "] ";
    }
    sstr << ">";
    return sstr.str();
}


void define_buffer_t()
{
    namespace h = Halide;
    namespace p = boost::python;

    p::class_<halide_buffer_t>("halide_buffer_t",
                        " The raw representation of an image passed around by generated "
                        "Halide code. It includes some stuff to track whether the image is "
                        "not actually in main memory, but instead on a device (like a GPU).",
                        p::init<>(p::arg("self")))
            .def_readwrite("device", &halide_buffer_t::device, "A device-handle for e.g. GPU memory used to back this buffer.")
            .def_readwrite("host", &halide_buffer_t::host, "A pointer to the start of the data in main memory.")
            .def("__repr__", &buffer_t_repr, p::arg("self"),
                 "Return a string containing a printable representation of a buffer_t object.")
            ;


    return;
}


h::Buffer *buffer_constructor0(h::Type t, int x_size, int y_size, int z_size, int w_size,
                               const std::string name)
{
    std::vector<int> size;
    if (x_size) size.push_back(x_size);
    if (y_size) size.push_back(y_size);
    if (z_size) size.push_back(z_size);
    if (w_size) size.push_back(w_size);
    return new h::Buffer(t, size, NULL, name);
}


std::string buffer_repr(const h::Buffer &that)
{
    std::string repr;
    if(that.defined())
    {
        const h::Type &t = that.type();
        boost::format format("<halide.Buffer named '%s' of type %s(%i) containing %s>");

        repr = boost::str(format % that.name()
                          % type_code_to_string(t) % t.bits()
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

    p::class_<Buffer::Dimension>("Buffer.Dimension",
                                 "Information about the shape of a single dimension of a Buffer",
                                 p::init<>(p::arg("self")))
        .def("min", &Buffer::Dimension::min, p::args("self"),
             "Get the coordinate in the function that this buffer represents "
             "that corresponds to the base address of the buffer.")
        .def("extent", &Buffer::Dimension::extent, p::args("self"),
             "Get the extent of this buffer in the given dimension.")
        .def("stride", &Buffer::Dimension::stride, p::args("self"),
             "Get the number of bytes between adjacent elements of this buffer along the given dimension.")
        .def("max", &Buffer::Dimension::max, p::args("self"),
             "Get the largest coordinate in this dimension.");

    p::class_<Buffer>("Buffer",
                      "The internal representation of an image, or other dense array "
                      "data. The Image type provides a typed view onto a buffer for the "
                      "purposes of direct manipulation. A buffer may be stored in main "
                      "memory, or some other memory space (e.g. a gpu). If you want to use "
                      "this as an Image, see the Image class. Casting a Buffer to an Image "
                      "will do any appropriate copy-back. This class is a fairly thin "
                      "wrapper on a halide_buffer_t, which is the C-style type Halide uses for "
                      "passing buffers around.",
                      p::init<>(p::arg("self")))
        .def("__init__", p::make_constructor(&buffer_constructor0, p::default_call_policies(),
                                             (/*p::arg("self"),*/ p::arg("type"),
                                              p::arg("x_size")=0, p::arg("y_size")=0, p::arg("z_size")=0, p::arg("w_size")=0,
                                              p::arg("name")="")))
        .def(p::init<h::Type, std::vector<int32_t>, uint8_t*, std::string>(
                 (p::arg("self"), p::arg("type"), p::arg("sizes"), p::arg("data")=NULL, p::arg("name")="")))
        .def(p::init<halide_buffer_t *, std::string>(
                 (p::arg("self"), p::arg("buf"), p::arg("name")="")))

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
             "Get the dimensionality of this buffer. ")

        .def("dim", &Buffer::dim, p::args("self", "dim"),
             "Get a handle on a given dimension of the buffer.")

        .def("set_min", &Buffer::set_min, p::args("self", "m"),
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
        .def("free_device_buffer", &Buffer::free_device_buffer, p::arg("self"),
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
