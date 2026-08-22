/* This file is the single source of truth for the buffer-protocol <->
 * halide_buffer_t marshalling used to invoke AOT-compiled Halide pipelines from
 * Python. It is used two ways:
 *
 *   1. It is embedded (via binary2cpp, as halide_c_template_PythonExtensionRuntime)
 *      into libHalide and emitted verbatim into each generated ".py.cpp" Python
 *      extension by PythonExtensionGen, so those extensions remain self-contained
 *      and depend on nothing beyond <Python.h> and HalideRuntime.h.
 *
 *   2. It is compiled directly into the standalone `halide.runtime` module, which
 *      can load and call precompiled AOT kernels without depending on libHalide.
 *
 * Because it is emitted into generated code that already includes <Python.h>,
 * <string>, and "HalideRuntime.h", the includes below are include-guard no-ops
 * in that context; they are present so this file is a valid translation unit on
 * its own. `unpack_buffer` is `inline` so that emitting its definition into every
 * generated ".py.cpp" (including the multi-library case) does not violate the ODR.
 */
#include <Python.h>

#include <climits>
#include <cstdint>
#include <cstring>

#include "HalideRuntime.h"

namespace Halide::PythonRuntime {

inline bool unpack_buffer(PyObject *py_obj,
                          int py_getbuffer_flags,
                          const char *name,
                          int dimensions,
                          Py_buffer &py_buf,
                          halide_dimension_t *halide_dim,
                          halide_buffer_t &halide_buf,
                          bool &py_buf_valid,
                          bool &needs_device_free) {
    py_buf_valid = false;
    needs_device_free = false;

    memset(&py_buf, 0, sizeof(py_buf));
    if (PyObject_GetBuffer(py_obj, &py_buf, PyBUF_FORMAT | PyBUF_STRIDED_RO | py_getbuffer_flags) < 0) {
        // Preserve the buffer exporter's exception (notably, the specific error
        // reported when a writable view was requested from a read-only object).
        return false;
    }
    py_buf_valid = true;

    if (dimensions && py_buf.ndim != dimensions) {
        PyErr_Format(PyExc_ValueError, "Invalid argument %s: Expected %d dimensions, got %d", name, dimensions, py_buf.ndim);
        return false;
    }
    if (py_buf.itemsize <= 0) {
        PyErr_Format(PyExc_ValueError, "Invalid argument %s: Buffer item size must be positive", name);
        return false;
    }
    // Always reverse axes.
    // TODO(jiawen): Can probably consolidate this with similar code in PyCallable.cpp and
    // pybufferinfo_to_halidebuffer() in PyBuffer.h.
    for (int i = 0; i < py_buf.ndim; ++i) {
        const int j = py_buf.ndim - 1 - i;  // numpy axis j maps to Halide dim i
        if (py_buf.shape[j] < 0 || py_buf.shape[j] > INT32_MAX) {
            PyErr_Format(PyExc_ValueError, "Invalid argument %s: Buffer extent is out of range", name);
            return false;
        }
        if (py_buf.strides[j] % py_buf.itemsize != 0) {
            PyErr_Format(PyExc_ValueError, "Invalid argument %s: Buffer byte stride is not a multiple of its item size", name);
            return false;
        }
        const Py_ssize_t element_stride = py_buf.strides[j] / py_buf.itemsize;
        if (element_stride < INT32_MIN || element_stride > INT32_MAX) {
            PyErr_Format(PyExc_ValueError, "Invalid argument %s: Buffer stride is out of range", name);
            return false;
        }
        halide_dim[i].min = 0;
        halide_dim[i].stride = static_cast<int32_t>(element_stride);
        halide_dim[i].extent = static_cast<int32_t>(py_buf.shape[j]);
        halide_dim[i].flags = 0;
        if (py_buf.suboffsets && py_buf.suboffsets[j] >= 0) {
            // Halide doesn't support arrays of pointers. But we should never see this
            // anyway, since we did not specify PyBUF_INDIRECT.
            PyErr_Format(PyExc_ValueError, "Invalid buffer: suboffsets not supported");
            return false;
        }
    }

    halide_buf = {};
    if (!py_buf.format) {
        if (py_buf.itemsize != 1) {
            PyErr_Format(PyExc_ValueError, "Invalid data type for %s: Missing format for a multi-byte buffer", name);
            return false;
        }
        halide_buf.type.code = halide_type_uint;
        halide_buf.type.bits = 8;
    } else {
        /* Convert struct type code. See
         * https://docs.python.org/2/library/struct.html#module-struct */
        const char *p = py_buf.format;
        char byte_order = '@';
        if (strchr("@<>!=", *p)) {
            byte_order = *p++;
        }
        const uint16_t endian_test = 1;
        const bool native_is_little_endian = *reinterpret_cast<const uint8_t *>(&endian_test) == 1;
        const bool non_native_byte_order =
            (native_is_little_endian && (byte_order == '>' || byte_order == '!')) ||
            (!native_is_little_endian && byte_order == '<');
        if (non_native_byte_order && py_buf.itemsize > 1) {
            PyErr_Format(PyExc_ValueError, "Invalid data type for %s: Non-native byte order %s is not supported", name, py_buf.format);
            return false;
        }
        if (*p == 'f' || *p == 'd' || *p == 'e') {
            // 'f', 'd', and 'e' are float, double, and half, respectively.
            halide_buf.type.code = halide_type_float;
        } else if (*p >= 'a' && *p <= 'z') {
            // lowercase is signed int.
            halide_buf.type.code = halide_type_int;
        } else {
            // uppercase is unsigned int.
            halide_buf.type.code = halide_type_uint;
        }
        const char *type_codes = "bBhHiIlLqQfde";  // integers and floats
        if (*p == '?' && p[1] == '\0' && py_buf.itemsize == 1) {
            // Special-case bool, so that it is a distinct type vs uint8_t
            // (even though the memory layout is identical)
            halide_buf.type.bits = 1;
        } else if (strchr(type_codes, *p) && p[1] == '\0' &&
                   (py_buf.itemsize == 1 || py_buf.itemsize == 2 ||
                    py_buf.itemsize == 4 || py_buf.itemsize == 8)) {
            halide_buf.type.bits = static_cast<uint8_t>(py_buf.itemsize * 8);
        } else {
            // We don't handle 's' and 'p' (char[]) and 'P' (void*)
            PyErr_Format(PyExc_ValueError, "Invalid data type for %s: %s", name, py_buf.format);
            return false;
        }
    }
    halide_buf.dimensions = py_buf.ndim;
    halide_buf.dim = halide_dim;
    halide_buf.host = static_cast<uint8_t *>(py_buf.buf);
    needs_device_free = true;

    return true;
}

}  // namespace Halide::PythonRuntime

namespace {

#define HALIDE_PYTHON_STRINGIFY_IMPL(x) #x
#define HALIDE_PYTHON_STRINGIFY(x) HALIDE_PYTHON_STRINGIFY_IMPL(x)

template<int dimensions>
struct PyHalideBuffer {
    // Must allocate at least 1, even if d=0
    static constexpr int dims_to_allocate = (dimensions < 1) ? 1 : dimensions;
    static constexpr const char *get_halide_buffer_capsule_fn = "_get_halide_buffer_t_capsule";
    static constexpr const char *halide_buffer_capsule_name =
        "halide.halide_buffer_t.v" HALIDE_PYTHON_STRINGIFY(HALIDE_VERSION_MAJOR);

    Py_buffer py_buf;
    halide_buffer_t *halide_buf = nullptr;
    PyObject *halide_buf_capsule = nullptr;
    bool py_buf_needs_release = false;
    bool needs_device_free = false;

    bool unpack_from_halide_buffer(PyObject *py_obj) {
        PyObject *get_capsule = PyObject_GetAttrString(py_obj, get_halide_buffer_capsule_fn);
        if (!get_capsule) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
            }
            return false;
        }

        PyObject *capsule = PyObject_CallObject(get_capsule, nullptr);
        Py_DECREF(get_capsule);
        if (!capsule) {
            return false;
        }

        if (!PyCapsule_CheckExact(capsule)) {
            PyErr_Format(PyExc_TypeError, "%s() must return a PyCapsule", get_halide_buffer_capsule_fn);
            Py_DECREF(capsule);
            return false;
        }

        void *ptr = PyCapsule_GetPointer(capsule, halide_buffer_capsule_name);
        if (!ptr) {
            Py_DECREF(capsule);
            return false;
        }

        // Keep the capsule alive for as long as we use the pointer it contains.
        halide_buf_capsule = capsule;
        halide_buf = static_cast<halide_buffer_t *>(ptr);
        return true;
    }

    bool unpack(PyObject *py_obj, int py_getbuffer_flags, const char *name) {
        if (unpack_from_halide_buffer(py_obj)) {
            return true;
        }
        if (PyErr_Occurred()) {
            return false;
        }
        if (Halide::PythonRuntime::unpack_buffer(
                py_obj, py_getbuffer_flags, name, dimensions, py_buf,
                unpacked_dim, unpacked_buf, py_buf_needs_release,
                needs_device_free)) {
            halide_buf = &unpacked_buf;
            return true;
        }
        return false;
    }

    ~PyHalideBuffer() {
        if (needs_device_free) {
            halide_device_free(nullptr, halide_buf);
        }
        if (py_buf_needs_release) {
            PyBuffer_Release(&py_buf);
        }
        Py_XDECREF(halide_buf_capsule);
    }

    PyHalideBuffer() = default;
    PyHalideBuffer(const PyHalideBuffer &other) = delete;
    PyHalideBuffer &operator=(const PyHalideBuffer &other) = delete;
    PyHalideBuffer(PyHalideBuffer &&other) = delete;
    PyHalideBuffer &operator=(PyHalideBuffer &&other) = delete;

private:
    halide_dimension_t unpacked_dim[dims_to_allocate];
    halide_buffer_t unpacked_buf;
};

#undef HALIDE_PYTHON_STRINGIFY
#undef HALIDE_PYTHON_STRINGIFY_IMPL

}  // namespace
