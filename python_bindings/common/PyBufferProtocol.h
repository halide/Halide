#ifndef HALIDE_PYTHON_BUFFER_PROTOCOL_H
#define HALIDE_PYTHON_BUFFER_PROTOCOL_H

#include "HalideRuntime.h"
#include <nanobind/ndarray.h>

#include <limits>
#include <vector>

using PythonArray = nanobind::ndarray<nanobind::ro, nanobind::device::cpu>;
using PythonMutableArray = nanobind::ndarray<nanobind::device::cpu>;

inline bool python_buffer_check(PyObject *object) {
    return PyObject_CheckBuffer(object);
}

class PythonBuffer : public nanobind::object {
public:
    NB_OBJECT(PythonBuffer, object, "Buffer", python_buffer_check)

    PythonArray request(bool writable = false) const {
        // Keep a buffer export alive, even when the source also supports DLPack.
        auto view = nanobind::steal(nanobind::handle(PyMemoryView_FromObject(ptr())));
        if (!view) {
            throw nanobind::python_error();
        }
        if (writable && nanobind::cast<bool>(view.attr("readonly"))) {
            throw nanobind::buffer_error("Buffer is not writable.");
        }
        try {
            return nanobind::cast<PythonArray>(view);
        } catch (const nanobind::cast_error &) {
            throw nanobind::value_error("Unsupported Buffer<> type or strides. Non-native byte order is not supported.");
        }
    }
};

inline halide_type_t python_dtype_to_halide_type(nanobind::dlpack::dtype dtype) {
    if (dtype.lanes == 1) {
        switch (static_cast<nanobind::dlpack::dtype_code>(dtype.code)) {
        case nanobind::dlpack::dtype_code::Bool:
            if (dtype.bits == 8) {
                return {halide_type_uint, 1};
            }
            break;
        case nanobind::dlpack::dtype_code::Int:
        case nanobind::dlpack::dtype_code::UInt:
            if (dtype.bits == 8 || dtype.bits == 16 || dtype.bits == 32 || dtype.bits == 64) {
                return {static_cast<halide_type_code_t>(dtype.code), dtype.bits};
            }
            break;
        case nanobind::dlpack::dtype_code::Float:
            if (dtype.bits == 16 || dtype.bits == 32 || dtype.bits == 64) {
                return {halide_type_float, dtype.bits};
            }
            break;
        default:
            break;
        }
    }
    throw nanobind::value_error("Unsupported Buffer<> type.");
}

inline nanobind::dlpack::dtype python_dtype_from_halide_type(halide_type_t type) {
    nanobind::dlpack::dtype dtype{type.code, type.bits, 1};
    if (type == halide_type_t(halide_type_uint, 1)) {
        dtype = nanobind::dtype<bool>();
    }
    if (python_dtype_to_halide_type(dtype) != type) {
        throw nanobind::value_error("Unsupported Buffer<> type.");
    }
    return dtype;
}

inline void python_array_dimensions(const PythonArray &array, halide_dimension_t *dims, bool reverse_axes) {
    for (size_t i = 0; i < array.ndim(); ++i) {
        const auto stride = array.stride(i);
        if (array.shape(i) > INT32_MAX || stride < INT32_MIN || stride > INT32_MAX) {
            throw nanobind::value_error("Out of range dimensions in buffer conversion.");
        }
        const size_t dst = reverse_axes ? array.ndim() - i - 1 : i;
        dims[dst] = {0, static_cast<int32_t>(array.shape(i)), static_cast<int32_t>(stride)};
    }
}

inline PythonMutableArray python_buffer_array(const halide_buffer_t &buffer, nanobind::handle owner, bool reverse_axes) {
    if (!buffer.host) {
        throw nanobind::value_error("Cannot convert a Buffer<> with null host ptr to a Python buffer.");
    }
    const auto dtype = python_dtype_from_halide_type(buffer.type);
    std::vector<size_t> shape(buffer.dimensions);
    std::vector<int64_t> strides(buffer.dimensions);
    Py_ssize_t length = buffer.type.bytes();
    for (int i = 0; i < buffer.dimensions; ++i) {
        const auto extent = buffer.dim[i].extent;
        if (extent < 0 || (extent && length > std::numeric_limits<Py_ssize_t>::max() / extent)) {
            throw nanobind::buffer_error("Buffer size is out of range.");
        }
        length *= extent;
        const int dst = reverse_axes ? buffer.dimensions - i - 1 : i;
        shape[dst] = extent;
        strides[dst] = buffer.dim[i].stride;
    }
    return PythonMutableArray(buffer.host, shape.size(), shape.data(), owner, strides.data(), dtype);
}

template<typename T, PythonMutableArray (*GetArray)(T &, nanobind::handle, bool)>
int python_getbuffer(PyObject *self, Py_buffer *view, int flags) noexcept {
    try {
        auto array = GetArray(nanobind::cast<T &>(nanobind::handle(self)), nanobind::handle(self), true);
        auto exported = nanobind::cast(nanobind::ndarray<nanobind::memview>(array), nanobind::rv_policy::reference);
        // memoryview validates the requested flags and owns the export's metadata.
        return PyObject_GetBuffer(exported.ptr(), view, flags);
    } catch (nanobind::python_error &e) {
        e.restore();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_BufferError, e.what());
    }
    return -1;
}

template<typename T, PythonMutableArray (*GetArray)(T &, nanobind::handle, bool)>
inline PyType_Slot python_buffer_slots[] = {
    {Py_bf_getbuffer, reinterpret_cast<void *>(python_getbuffer<T, GetArray>)},
    {0, nullptr},
};

inline nanobind::object python_numpy_view(const PythonMutableArray &array) {
    return nanobind::cast(nanobind::ndarray<nanobind::numpy>(array), nanobind::rv_policy::reference);
}

#endif
