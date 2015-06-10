#include "Buffer.h"


// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "make_array.h"

#include "../../src/Buffer.h"

#include <string>


/// helper function to access &(Buffer::operator Argument)
Halide::Argument buffer_to_argument(Halide::Buffer &that)
{
    return that;
}

void defineBuffer()
{

    using Halide::Buffer;
    namespace h = Halide;
    namespace p = boost::python;
    using p::self;


    p::class_<buffer_t>("buffer_t",
                        " The raw representation of an image passed around by generated "
                        "Halide code. It includes some stuff to track whether the image is "
                        "not actually in main memory, but instead on a device (like a GPU).",
                        p::init<>())
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
                           "mirroring this buffer, and the data has been modified on the device side.");


    p::class_<Buffer>("Buffer",
                      "The internal representation of an image, or other dense array "
                      "data. The Image type provides a typed view onto a buffer for the "
                      "purposes of direct manipulation. A buffer may be stored in main "
                      "memory, or some other memory space (e.g. a gpu). If you want to use "
                      "this as an Image, see the Image class. Casting a Buffer to an Image "
                      "will do any appropriate copy-back. This class is a fairly thin "
                      "wrapper on a buffer_t, which is the C-style type Halide uses for "
                      "passing buffers around.",
                      p::init<>())
            .def(p::init<h::Type, int, int, int, int, uint8_t*, std::string>(
                     (p::arg("type"),
                      p::arg("x_size")=0, p::arg("y_size")=0, p::arg("z_size")=0, p::arg("w_size")=0,
                      p::arg("data")=NULL, p::arg("name")="")))
            .def(p::init<h::Type, std::vector<int32_t>, uint8_t*, std::string>(
                     (p::arg("type"), p::arg("sizes"), p::arg("data")=NULL, p::arg("name")="")))
            .def(p::init<h::Type, buffer_t *, std::string>(
                     (p::arg("type"), p::arg("buf"), p::arg("name")="")))

            .def("host_ptr", &Buffer::host_ptr,
                 //p::return_internal_reference<1>(),
                 p::return_value_policy< p::return_opaque_pointer >(), // not sure this will do what we want
                 "Get a pointer to the host-side memory.")

            .def("raw_buffer", &Buffer::raw_buffer,
                 p::return_internal_reference<1>(),
                 "Get a pointer to the raw buffer_t struct that this class wraps.")

            .def("device_handle", &Buffer::device_handle,
                 "Get the device-side pointer/handle for this buffer. Will be "
                 "zero if no device was involved in the creation of this buffer.")

            .def("host_dirty", &Buffer::host_dirty,
                 "Has this buffer been modified on the cpu since last copied to a "
                 "device. Not meaningful unless there's a device involved.")
            .def("set_host_dirty", &Buffer::set_host_dirty,
                 p::args("dirty") = true,
                 "Let Halide know that the host-side memory backing this buffer "
                 "has been externally modified. You shouldn't normally need to "
                 "call this, because it is done for you when you cast a Buffer to "
                 "an Image in order to modify it.")

            .def("device_dirty", &Buffer::device_dirty,
                 "Has this buffer been modified on device since last copied to "
                 "the cpu. Not meaninful unless there's a device involved.")
            .def("set_device_dirty", &Buffer::set_device_dirty,
                 p::args("dirty") = true,
                 "Let Halide know that the device-side memory backing this "
                 "buffer has been externally modified, and so the cpu-side memory "
                 "is invalid. A copy-back will occur the next time you cast this "
                 "Buffer to an Image, or the next time this buffer is accessed on "
                 "the host in a halide pipeline.")

            .def("dimensions", &Buffer::dimensions,
                 "Get the dimensionality of this buffer. Uses the convention "
                 "that the extent field of a buffer_t should contain zero when "
                 "the dimensions end.")
            .def("extent", &Buffer::extent, p::arg("dim"),
                 "Get the extent of this buffer in the given dimension.")
            .def("stride", &Buffer::stride, p::arg("dim"),
                 "Get the number of bytes between adjacent elements of this buffer along the given dimension.")
            .def("min", &Buffer::min, p::arg("dim"),
                 "Get the coordinate in the function that this buffer represents "
                 "that corresponds to the base address of the buffer.")
            .def("set_min", &Buffer::set_min,
                 (p::arg("m0"), p::arg("m1")=0, p::arg("m2")=0, p::arg("m3")=0),
                 "Set the coordinate in the function that this buffer represents "
                 "that corresponds to the base address of the buffer.")
            .def("type", &Buffer::type,
                 "Get the Halide type of the contents of this buffer.")
            .def("same_as", &Buffer::same_as, p::arg("other"),
                 "Compare two buffers for identity (not equality of data).")
            .def("defined", &Buffer::defined,
                 "Check if this buffer handle actually points to data.")
            .def("name", &Buffer::name,
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get the runtime name of this buffer used for debugging.")

            .def("to_Argument", &buffer_to_argument, //&(Buffer::operator Argument),
                 "Convert this buffer to an argument to a halide pipeline.")

            .def("copy_to_host", &Buffer::copy_to_host,
                 "If this buffer was created *on-device* by a jit-compiled "
                 "realization, then copy it back to the cpu-side memory. "
                 "This is usually achieved by casting the Buffer to an Image.")
            .def("copy_to_device", &Buffer::copy_to_device,
                 "If this buffer was created by a jit-compiled realization on a "
                 "device-aware target (e.g. PTX), then copy the cpu-side data to "
                 "the device-side allocation. TODO: I believe this currently "
                 "aborts messily if no device-side allocation exists. You might "
                 "think you want to do this because you've modified the data "
                 "manually on the host before calling another Halide pipeline, "
                 "but what you actually want to do in that situation is set the "
                 "host_dirty bit so that Halide can manage the copy lazily for "
                 "you. Casting the Buffer to an Image sets the dirty bit for you.")
            .def("free_dev_buffer", &Buffer::free_dev_buffer,
                 "If this buffer was created by a jit-compiled realization on a "
                 "device-aware target (e.g. PTX), then free the device-side "
                 "allocation, if there is one. Done automatically when the last "
                 "reference to this buffer dies.")
            ;

    p::implicitly_convertible<Buffer, h::Argument>();
    return;
}
