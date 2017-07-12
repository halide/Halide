# Before reading this file, see lesson_10_aot_compilation_generate.cpp

# This is the code that actually uses the Halide pipeline we've
# compiled. It does not depend on libHalide, so we won't be including
# Halide.h.
#
# Instead, it depends on the header file that lesson_10_generate
# produced when we ran it:
#include "lesson_10_halide.h"

#include <stdio.h>

from halide import *
import numpy as np
import ctypes
import platform

def main():
    # Have a look in the header file above (it won't exist until you've run
    # lesson_10_generate).

    # It starts with a definition of a buffer_t:
    #
    # typedef struct buffer_t {
    #     uint64_t dev
    #     uint8_t* host
    #     int32_t extent[4]
    #     int32_t stride[4]
    #     int32_t min[4]
    #     int32_t elem_size
    #     bool host_dirty
    #     bool dev_dirty
    # } buffer_t
    #
    # This is how Halide represents input and output images in
    # pre-compiled pipelines. There's a 'host' pointer that points to the
    # start of the image data, some fields that describe how to access
    # pixels, and some fields related to using the GPU that we'll ignore
    # for now (dev, host_dirty, dev_dirty).

    # Let's make some input data to test with:
    input = np.empty((640, 480), dtype=np.uint8, order='F')
    for y in range(480):
        for x in range(640):
            input[x, y] = x ^ (y + 1)


    # And the memory where we want to write our output:
    output = np.empty((640,480), dtype=np.uint8, order='F')


    if False:
        # In AOT-compiled mode, Halide doesn't manage this memory for
        # you. You should use whatever image data type makes sense for
        # your application. Halide just needs pointers to it.

        # Now we make a buffer_t to represent our input and output. It's
        # important to zero-initialize them so you don't end up with
        # garbage fields that confuse Halide.
        input_buf, output_buf = buffer_t(), buffer_t()
        #input_buf.stride, input_buf.extent = [0]*4, [0]*4
        #output_buf.stride, output_buf.extent = [0]*4, [0]*4
        for i in range(4):
            input_buf.stride[i] = 0
            input_buf.extent[i] = 0
            output_buf.stride[i] = 0
            output_buf.extent[i] = 0

        # The host pointers point to the start of the image data:
        #input_buf.host  = &input[0]
        #output_buf.host = &output[0]
        # the following line will fail, but we do not need it
        # (see code below "else:")
        input_buf.host  = input.ctypes.data
        output_buf.host = output.ctypes.data

        # To access pixel (x, y) in a two-dimensional buffer_t, Halide
        # looks at memory address:

        # host + elem_size * ((x - min[0])*stride[0] + (y - min[1])*stride[1])

        # The stride in a dimension represents the number of elements in
        # memory between adjacent entries in that dimension. We have a
        # grayscale image stored in scanline order, so stride[0] is 1,
        # because pixels that are adjacent in x are next to each other in
        # memory.
        input_buf.stride[0] = output_buf.stride[0] = 1

        # stride[1] is the width of the image, because pixels that are
        # adjacent in y are separated by a scanline's worth of pixels in
        # memory.
        input_buf.stride[1] = output_buf.stride[1] = 640

        # The extent tells us how large the image is in each dimension.
        input_buf.extent[0] = output_buf.extent[0] = 640
        input_buf.extent[1] = output_buf.extent[1] = 480

        # We'll leave the mins as zero. This is what they typically
        # are. The host pointer points to the memory location of the min
        # coordinate (not the origin!).  See lesson 6 for more detail
        # about the mins.

        # The elem_size field tells us how many bytes each element
        # uses. For the 8-bit image we use in this test it's one.
        input_buf.elem_size = output_buf.elem_size = 1
    else:
        input_buf = Buffer(input).buffer()
        output_buf = Buffer(output).buffer()


    # To avoid repeating all the boilerplate above, We recommend you
    # make a helper function that populates a buffer_t given whatever
    # image type you're using.

    # Now that we've setup our input and output buffers, we can call
    # our function. Looking in the header file, it's signature is:

    # int lesson_10_halide(buffer_t *_input, const int32_t _offset, buffer_t *_brighter)

    # The return value is an error code. It's zero on success.

    if platform.system() == "Linux":
        #lesson10 = ctypes.cdll.LoadLibrary("lesson_10_halide.o")
        # Python can only load dynamic libraries,
        # the created object needs to be converted.
        # .so created via `gcc -shared -o lesson_10_halide.so lesson_10_halide.o`
        lesson10 = ctypes.cdll.LoadLibrary("../build/lesson_10_halide.so")
    elif platform.system() == "Windows":
        lesson10 = ctypes.windll.LoadLibrary("lesson_10_halide.dll")
    elif platform.system() == "Darwin":
        lesson10 = ctypes.cdll.LoadLibrary("lesson_10_halide.o")
    else:
        raise Exception("unknown platform")

    assert lesson10 != None


    class BufferStruct(ctypes.Structure):
        _fields_ = [
            #A device-handle for e.g. GPU memory used to back this buffer.
            ("dev", ctypes.c_uint64),

            #A pointer to the start of the data in main memory.
            ("host", ctypes.POINTER(ctypes.c_uint8)),

            #The size of the buffer in each dimension.
            ("extent", ctypes.c_int32 * 4),

            #Gives the spacing in memory between adjacent elements in the
            #given dimension.  The correct memory address for a load from
            #this buffer at position x, y, z, w is:
            #host + (x * stride[0] + y * stride[1] + z * stride[2] + w * stride[3]) * elem_size
            #By manipulating the strides and extents you can lazily crop,
            #transpose, and even flip buffers without modifying the data.
            ("stride", ctypes.c_int32 * 4),

            #Buffers often represent evaluation of a Func over some
            #domain. The min field encodes the top left corner of the
            #domain.
            ("min", ctypes.c_int32 * 4),

            #How many bytes does each buffer element take. This may be
            #replaced with a more general type code in the future. */
            ("elem_size", ctypes.c_int32),

            #This should be true if there is an existing device allocation
            #mirroring this buffer, and the data has been modified on the
            #host side.
            ("host_dirty", ctypes.c_bool),

            #This should be true if there is an existing device allocation
            #mirroring this buffer, and the data has been modified on the
            #device side.
            ("dev_dirty", ctypes.c_bool),
        ]


    def buffer_t_to_buffer_struct(buffer):
        assert type(buffer) == Buffer
        b = buffer.raw_buffer()
        bb = BufferStruct()

        uint8_p_t = ctypes.POINTER(ctypes.c_ubyte)
        # host_p0 is the complicated way...
        #host_p0 = buffer_to_ndarray(Buffer(UInt(8), b)).ctypes.data
        # host_ptr_as_int is the easy way
        host_p = buffer.host_ptr_as_int()
        bb.host = ctypes.cast(host_p, uint8_p_t)
        #print("host_p", host_p0, host_p, bb.host)
        bb.dev = b.dev
        bb.elem_size = b.elem_size
        bb.host_dirty = b.host_dirty
        bb.dev_dirty = b.dev_dirty
        for i in range(4):
            bb.extent[i] = b.extent[i]
            bb.stride[i] = b.stride[i]
            bb.min[i] = b.min[i]
        return bb

    input_buf_struct = buffer_t_to_buffer_struct(input_buf)
    output_buf_struct = buffer_t_to_buffer_struct(output_buf)

    input_buf_struct_p = ctypes.pointer(input_buf_struct)
    output_buf_struct_p = ctypes.pointer(output_buf_struct)

    for i in range(15):
        # check that we map the right data
        assert input_buf_struct_p[0].host[i] == input[i, 0]


    #print("lesson10.lesson_10_halide", lesson10.lesson_10_halide)

    offset_value = 5
    offset = ctypes.c_int(offset_value)
    error = lesson10.lesson_10_halide(input_buf_struct_p, offset, output_buf_struct_p)

    if error:
        print("Halide returned an error: ", error)
        return -1


    # Now let's check the filter performed as advertised. It was
    # supposed to add the offset to every input pixel.
    correct_val = np.empty((1), dtype=np.uint8)
    for y in range(480):
        for x in range(640):
            input_val = input[x, y]
            output_val = output[x, y]
            correct_val[0] = input_val
            # we add over a uint8 value (will properly model overflow)
            correct_val[0] += offset_value
            if output_val != correct_val[0]:
                raise Exception("output(%d, %d) was %d instead of %d" % (
                       x, y, output_val, correct_val))
                #return -1

    # Everything worked!
    print("Success!")
    return 0



if __name__ == "__main__":
    main()
