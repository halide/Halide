#ifndef HALIDE_PYTHON_BUFFER_PROTOCOL_H
#define HALIDE_PYTHON_BUFFER_PROTOCOL_H

#include <nanobind/nanobind.h>

#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

struct PythonBufferInfo {
    void *ptr = nullptr;
    Py_ssize_t itemsize = 0;
    std::string format;
    int ndim = 0;
    std::vector<Py_ssize_t> shape, strides;
    std::shared_ptr<Py_buffer> export_owner;

    PythonBufferInfo() = default;
    PythonBufferInfo(void *ptr, Py_ssize_t itemsize, std::string format, int ndim,
                     std::vector<Py_ssize_t> shape, std::vector<Py_ssize_t> strides)
        : ptr(ptr), itemsize(itemsize), format(std::move(format)), ndim(ndim),
          shape(std::move(shape)), strides(std::move(strides)) {
    }

    static PythonBufferInfo request(nanobind::handle owner, bool writable) {
        auto view = std::shared_ptr<Py_buffer>(new Py_buffer{}, [](Py_buffer *view) {
            if (view->obj) {
                nanobind::gil_scoped_acquire acquire;
                PyBuffer_Release(view);
            }
            delete view;
        });
        if (PyObject_GetBuffer(owner.ptr(), view.get(),
                               PyBUF_STRIDES | PyBUF_FORMAT | (writable ? PyBUF_WRITABLE : 0)) < 0) {
            throw nanobind::python_error();
        }
        PythonBufferInfo result(view->buf, view->itemsize, view->format ? view->format : "B",
                                view->ndim, {}, {});
        if (view->ndim > 0) {
            result.shape.assign(view->shape, view->shape + view->ndim);
            result.strides.assign(view->strides, view->strides + view->ndim);
        }
        result.export_owner = std::move(view);
        return result;
    }
};

inline bool python_buffer_check(PyObject *object) {
    return PyObject_CheckBuffer(object);
}

class PythonBuffer : public nanobind::object {
public:
    NB_OBJECT(PythonBuffer, object, "Buffer", python_buffer_check)

    PythonBufferInfo request(bool writable = false) const {
        return PythonBufferInfo::request(*this, writable);
    }
};

template<typename T>
std::string python_format() {
    if constexpr (std::is_same_v<T, bool>) {
        return "?";
    } else if constexpr (std::is_integral_v<T>) {
        const char *formats = std::is_signed_v<T> ? "bhiq" : "BHIQ";
        return std::string(1, formats[sizeof(T) == 1 ? 0 : sizeof(T) == 2 ? 1 :
                                                       sizeof(T) == 4     ? 2 :
                                                                            3]);
    } else {
        return sizeof(T) == 4 ? "f" : "d";
    }
}

template<typename T, PythonBufferInfo (*GetInfo)(T &)>
int python_getbuffer(PyObject *self, Py_buffer *view, int flags) noexcept {
    try {
        auto info = std::make_unique<PythonBufferInfo>(GetInfo(nanobind::cast<T &>(nanobind::handle(self))));
        Py_ssize_t length = info->itemsize;
        for (auto extent : info->shape) {
            if (extent < 0 || (extent && length > std::numeric_limits<Py_ssize_t>::max() / extent)) {
                throw nanobind::buffer_error("Buffer size is out of range.");
            }
            length *= extent;
        }
        Py_buffer candidate{};
        candidate.buf = info->ptr;
        candidate.len = length;
        candidate.itemsize = info->itemsize;
        candidate.ndim = info->ndim;
        candidate.shape = info->shape.data();
        candidate.strides = info->strides.data();
        if (((flags & PyBUF_STRIDES) != PyBUF_STRIDES ||
             (flags & PyBUF_C_CONTIGUOUS) == PyBUF_C_CONTIGUOUS) &&
            !PyBuffer_IsContiguous(&candidate, 'C')) {
            throw nanobind::buffer_error("Buffer is not C-contiguous.");
        }
        if ((flags & PyBUF_F_CONTIGUOUS) == PyBUF_F_CONTIGUOUS && !PyBuffer_IsContiguous(&candidate, 'F')) {
            throw nanobind::buffer_error("Buffer is not Fortran-contiguous.");
        }
        if ((flags & PyBUF_ANY_CONTIGUOUS) == PyBUF_ANY_CONTIGUOUS && !PyBuffer_IsContiguous(&candidate, 'A')) {
            throw nanobind::buffer_error("Buffer is not contiguous.");
        }
        candidate.obj = Py_NewRef(self);
        candidate.format = (flags & PyBUF_FORMAT) ? info->format.data() : nullptr;
        candidate.shape = (flags & PyBUF_ND) ? info->shape.data() : nullptr;
        candidate.strides = (flags & PyBUF_STRIDES) == PyBUF_STRIDES ? info->strides.data() : nullptr;
        candidate.internal = info.release();
        *view = candidate;
        return 0;
    } catch (nanobind::python_error &e) {
        e.restore();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_BufferError, e.what());
    }
    return -1;
}

inline void python_releasebuffer(PyObject *, Py_buffer *view) noexcept {
    delete static_cast<PythonBufferInfo *>(view->internal);
}

template<typename T, PythonBufferInfo (*GetInfo)(T &)>
inline PyType_Slot python_buffer_slots[] = {
    {Py_bf_getbuffer, reinterpret_cast<void *>(python_getbuffer<T, GetInfo>)},
    {Py_bf_releasebuffer, reinterpret_cast<void *>(python_releasebuffer)},
    {0, nullptr},
};

inline nanobind::object python_numpy_view(nanobind::handle owner, bool reverse_axes) {
    auto result = nanobind::module_::import_("numpy").attr("asarray")(owner);
    return reverse_axes ? result : result.attr("transpose")();
}

#endif
