#include "PyBuffer.h"

#include <utility>

#include "PyFunc.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

namespace {

#define HALIDE_PYTHON_STRINGIFY_IMPL(x) #x
#define HALIDE_PYTHON_STRINGIFY(x) HALIDE_PYTHON_STRINGIFY_IMPL(x)
constexpr char halide_buffer_capsule_name[] =
    "halide.halide_buffer_t.v" HALIDE_PYTHON_STRINGIFY(HALIDE_VERSION_MAJOR);
constexpr char runtime_buffer_capsule_name[] =
    "halide.runtime.Buffer.v" HALIDE_PYTHON_STRINGIFY(HALIDE_VERSION_MAJOR);
#undef HALIDE_PYTHON_STRINGIFY
#undef HALIDE_PYTHON_STRINGIFY_IMPL

// Standard stream output for halide_dimension_t
std::ostream &operator<<(std::ostream &stream, const halide_dimension_t &d) {
    stream << "[" << d.min << "," << d.extent << "," << d.stride << "]";
    return stream;
}

// Standard stream output for vector<halide_dimension_t>
std::ostream &operator<<(std::ostream &stream, const std::vector<halide_dimension_t> &shape) {
    stream << "[";
    bool need_comma = false;
    for (const auto &d : shape) {
        if (need_comma) {
            stream << ",";
        }
        stream << d;
        need_comma = true;
    }
    stream << "]";
    return stream;
}

// Given a Buffer<>, return its shape in the form of a vector<halide_dimension_t>.
// (Oddly, Buffer<> has no API to do this directly.)
std::vector<halide_dimension_t> get_buffer_shape(const Buffer<> &b) {
    if (!b.defined()) {
        // Return an empty vector if the buffer is not defined.
        return {};
    }
    std::vector<halide_dimension_t> s;
    s.reserve(b.dimensions());
    for (int i = 0; i < b.dimensions(); ++i) {
        s.push_back(b.raw_buffer()->dim[i]);
    }
    return s;
}

// Convert Halide's scalar float16_t through Python's float type.
template<typename T>
inline T value_cast(const py::object &value) {
    return py::cast<T>(value);
}

template<>
inline float16_t value_cast<float16_t>(const py::object &value) {
    return float16_t(py::cast<double>(value));
}

// TODO: https://github.com/halide/Halide/issues/6849
// DLPack supports bfloat16, but PEP 3118 and nanobind's memoryview export do not.
// template<>
// inline bfloat16_t value_cast<bfloat16_t>(const py::object &value) {
//     return bfloat16_t(py::cast<double>(value));
// }

void call_fill(Buffer<> &b, const py::object &value) {

#define HANDLE_BUFFER_TYPE(TYPE)                    \
    if (b.type() == type_of<TYPE>()) {              \
        b.as<TYPE>().fill(value_cast<TYPE>(value)); \
        return;                                     \
    }

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
}

bool call_all_equal(Buffer<> &b, const py::object &value) {

#define HANDLE_BUFFER_TYPE(TYPE)                                \
    if (b.type() == type_of<TYPE>()) {                          \
        return b.as<TYPE>().all_equal(value_cast<TYPE>(value)); \
    }

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return false;
}

void check_out_of_bounds(Buffer<> &buf, const std::vector<int> &pos) {
    const int d = buf.dimensions();
    if ((size_t)pos.size() != (size_t)d) {
        throw py::value_error("Incorrect number of dimensions.");
    }
    for (int i = 0; i < d; i++) {
        const auto &dim = buf.dim(i);
        if (pos[i] < dim.min() || pos[i] > dim.max()) {
            // Try to mimic the wording of similar errors in NumPy
            std::ostringstream o;
            o << "index " << pos[i] << " is out of bounds for axis " << i << " with min=" << dim.min() << ", extent=" << dim.extent();
            throw py::index_error(o.str().c_str());
        }
    }
}

// copy_from() reads the source at the destination's element size and crops
// both buffers along the destination's dimensions, so a mismatch in either
// reads (and crops) past the end of the source.
void check_copy_from(const Buffer<> &dst, const Buffer<> &src) {
    if (src.dimensions() != dst.dimensions()) {
        std::ostringstream o;
        o << "Cannot copy a buffer with " << src.dimensions() << " dimensions into a buffer with " << dst.dimensions() << " dimensions.";
        throw py::value_error(o.str().c_str());
    }
    if (src.type().bytes() != dst.type().bytes()) {
        std::ostringstream o;
        o << "Cannot copy a buffer of type " << halide_type_to_string(src.type()) << " into a buffer of type " << halide_type_to_string(dst.type()) << ".";
        throw py::value_error(o.str().c_str());
    }
}

}  // namespace

py::object buffer_getitem_operator(Buffer<> &buf, const std::vector<int> &pos) {
    check_out_of_bounds(buf, pos);

#define HANDLE_BUFFER_TYPE(TYPE)       \
    if (buf.type() == type_of<TYPE>()) \
        return py::cast(buf.as<TYPE>()(pos.data()));

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return py::object();
}

namespace {

py::object buffer_setitem_operator(Buffer<> &buf, const std::vector<int> &pos, const py::object &value) {
    check_out_of_bounds(buf, pos);

#define HANDLE_BUFFER_TYPE(TYPE)       \
    if (buf.type() == type_of<TYPE>()) \
        return py::cast(buf.as<TYPE>()(pos.data()) = value_cast<TYPE>(value));

    HANDLE_BUFFER_TYPE(bool)
    HANDLE_BUFFER_TYPE(uint8_t)
    HANDLE_BUFFER_TYPE(uint16_t)
    HANDLE_BUFFER_TYPE(uint32_t)
    HANDLE_BUFFER_TYPE(uint64_t)
    HANDLE_BUFFER_TYPE(int8_t)
    HANDLE_BUFFER_TYPE(int16_t)
    HANDLE_BUFFER_TYPE(int32_t)
    HANDLE_BUFFER_TYPE(int64_t)
    // TODO: https://github.com/halide/Halide/issues/6849
    // HANDLE_BUFFER_TYPE(bfloat16_t)
    HANDLE_BUFFER_TYPE(float16_t)
    HANDLE_BUFFER_TYPE(float)
    HANDLE_BUFFER_TYPE(double)

#undef HANDLE_BUFFER_TYPE

    throw py::value_error("Unsupported Buffer<> type.");
    return py::object();
}

// Retain the imported ndarray and its buffer export for the life of the Buffer.
class PyBuffer : public Buffer<> {
    PythonArray array;

public:
    PyBuffer()
        : Buffer<>() {
    }

    explicit PyBuffer(const Buffer<> &b)
        : Buffer<>(b) {
    }

    PyBuffer(const PythonBuffer &buffer, const std::string &name, bool reverse_axes)
        : Buffer<>() {
        py::object get_runtime_buffer = py::getattr(buffer, "_get_halide_runtime_buffer_capsule", py::none());
        if (!get_runtime_buffer.is_none()) {
            py::object defined = py::getattr(buffer, "defined", py::none());
            if (defined.is_none()) {
                throw py::type_error("Invalid halide.runtime.Buffer.");
            }
            if (!py::cast<bool>(defined())) {
                return;
            }
            const py::capsule capsule = py::cast<py::capsule>(get_runtime_buffer());
            const auto *runtime_buffer = static_cast<const Halide::Runtime::Buffer<> *>(
                PyCapsule_GetPointer(capsule.ptr(), runtime_buffer_capsule_name));
            if (!runtime_buffer) {
                throw py::python_error();
            }
            Buffer<>::operator=(Buffer<>(Halide::Runtime::Buffer<>(*runtime_buffer), name));
            return;
        }

        array = buffer.request(/*writable*/ true);
        Buffer<>::operator=(Buffer<>(pyarray_to_halidebuffer(array, reverse_axes), name));

        // Default to setting host-dirty on any PyBuffer we create from an existing PythonBuffer;
        // this allows (e.g.) code like
        //
        //     input = hl.Buffer(imageio.imread(image_path))
        //
        // to work as you'd expected when the buffer is used for input. (Without
        // host_dirty being set, copying to GPU can be skipped and produce surprising
        // results.)
        //
        // Note that this is a bit different from the C++ Buffer<> ctors that
        // take pointer-to-existing data (which do *not* set host dirty); a crucial
        // difference there is that those also do not take ownership, but here,
        // we always take (shared) ownership of the underlying buffer.
        this->set_host_dirty();
    }

    ~PyBuffer() override = default;
};

PythonMutableArray buffer_array(Buffer<> &b, py::handle owner, bool reverse_axes) {
    if (!b.defined()) {
        throw py::value_error("Undefined buffer");
    }
    return python_buffer_array(*b.raw_buffer(), owner, reverse_axes);
}

}  // namespace

void define_buffer(py::module_ &m) {
    using BufferDimension = Halide::Runtime::Buffer<>::Dimension;

    m.attr("BufferDimension") = py::module_::import_("halide.runtime").attr("BufferDimension");

    auto buffer_class =
        py::class_<Buffer<>, PyBuffer>(m, "Buffer", py::type_slots(python_buffer_slots<Buffer<>, buffer_array>))

            // Note that this allows us to convert a Buffer<> to any buffer-like object in Python;
            // most notably, we can convert to an ndarray by calling numpy.array(other_buffer). This
            // always reverses the axes.
            // Returns a NumPy array that is a view of (shares storage with) this Buffer.
            .def(
                "numpy_view", [](Buffer<> &self, bool reverse_axes) -> py::object {
                    return python_numpy_view(buffer_array(self, py::cast(&self, py::rv_policy::reference), reverse_axes));
                },
                py::arg("reverse_axes") = true, "Returns a NumPy array that is a view of this Buffer. By default the axes are "
                                                "reversed. Pass reverse_axes=False to preserve the original axis order.")

            .def(py::init<>())
            // keep_alive</*Nurse*/ 1, /*Patient*/ 2>: this copy shares the source's data pointer
            // without taking ownership (the source may itself wrap external memory, e.g. a numpy
            // array kept alive only by the source Buffer).
            //
            // In keep_alive<1, 2>, 1 refers to `this`, 2 refers to the argument `Buffer<> &` arg.
            .def(py::init<const Buffer<> &>(), py::keep_alive<1, 2>())

            // This py::init_alias allows us to use any buffer-like Python entity to create a
            // Buffer<> (most notably, an ndarray). A halide.runtime.Buffer is copied
            // directly, matching Halide::Buffer's C++ constructor and preserving its
            // axis order. reverse_axes = True by default for other buffer objects.
            //
            // ! Note that the copy/default constructors are registered *above, before* the
            // PythonBuffer one before. This is because hl.Buffer also satisfies the buffer protocol,
            // and we want hl.Buffer(other_buffer) to be a "direct" copy (no axis reversal) rather
            // than routing through the axis-reversing buffer-protocol path.
            .def(py::init<PythonBuffer, const std::string &, bool>(), py::arg("buffer"), py::arg("name") = "", py::arg("reverse_axes") = true, py::keep_alive<1, 2>())

            .def("__init__", [](Buffer<> *self, Type type, const std::vector<int> &sizes, const std::string &name) { new (self) Buffer<>(Buffer<>(type, sizes, name)); }, py::arg("type"), py::arg("sizes"), py::arg("name") = "")

            // The default storage order is [0, 1, 2, ...], meaning store the first axis densest.
            .def("__init__", [](Buffer<> *self, Type type, const std::vector<int> &sizes, const std::vector<int> &storage_order, const std::string &name) { new (self) Buffer<>(Buffer<>(type, sizes, storage_order, name)); }, py::arg("type"), py::arg("sizes"), py::arg("storage_order"), py::arg("name") = "")

            // Note that this exists solely to allow you to create a Buffer with a null host ptr;
            // this is necessary for some bounds-query operations (e.g. Func::infer_input_bounds).
            .def_static("make_bounds_query", [](Type type, const std::vector<int> &sizes, const std::string &name) -> Buffer<> { return Buffer<>(type, nullptr, sizes, name); }, py::arg("type"), py::arg("sizes"), py::arg("name") = "")

            .def_static("make_scalar", static_cast<Buffer<> (*)(Type, const std::string &)>(Buffer<>::make_scalar), py::arg("type"), py::arg("name") = "")
            .def_static("make_interleaved", static_cast<Buffer<> (*)(Type, int, int, int, const std::string &)>(Buffer<>::make_interleaved), py::arg("type"), py::arg("width"), py::arg("height"), py::arg("channels"), py::arg("name") = "")
            .def_static(  //
                "make_with_shape_of", [](const Buffer<> &buffer, const std::string &name) -> Buffer<> {
                    return Buffer<>::make_with_shape_of(buffer, nullptr, nullptr, name);  //
                },
                py::arg("src"), py::arg("name") = "")

            .def("set_name", &Buffer<>::set_name)
            .def("name", &Buffer<>::name)

            .def("same_as", static_cast<bool (Buffer<>::*)(const Buffer<> &other) const>(&Buffer<>::same_as), py::arg("other"))
            .def("defined", &Buffer<>::defined)

            .def("type", &Buffer<>::type)
            .def("channels", static_cast<int (Buffer<>::*)() const>(&Buffer<>::channels))
            .def("dimensions", static_cast<int (Buffer<>::*)() const>(&Buffer<>::dimensions))
            .def("width", static_cast<int (Buffer<>::*)() const>(&Buffer<>::width))
            .def("height", static_cast<int (Buffer<>::*)() const>(&Buffer<>::height))
            .def("top", static_cast<int (Buffer<>::*)() const>(&Buffer<>::top))
            .def("bottom", static_cast<int (Buffer<>::*)() const>(&Buffer<>::bottom))
            .def("left", static_cast<int (Buffer<>::*)() const>(&Buffer<>::left))
            .def("right", static_cast<int (Buffer<>::*)() const>(&Buffer<>::right))
            .def("number_of_elements", static_cast<size_t (Buffer<>::*)() const>(&Buffer<>::number_of_elements))
            .def("size_in_bytes", static_cast<size_t (Buffer<>::*)() const>(&Buffer<>::size_in_bytes))
            .def("has_device_allocation", static_cast<bool (Buffer<>::*)() const>(&Buffer<>::has_device_allocation))
            .def("host_dirty", static_cast<bool (Buffer<>::*)() const>(&Buffer<>::host_dirty))
            .def("device_dirty", static_cast<bool (Buffer<>::*)() const>(&Buffer<>::device_dirty))

            .def("set_host_dirty",  //
                 [](Buffer<> &b, bool dirty) -> void {
                     b.set_host_dirty(dirty);  //
                 },
                 py::arg("dirty") = true)
            .def("set_device_dirty",  //
                 [](Buffer<> &b, bool dirty) -> void {
                     b.set_device_dirty(dirty);  //
                 },
                 py::arg("dirty") = true)

            .def("copy", &Buffer<>::copy)
            .def("copy_from", [](Buffer<> &b, const Buffer<> &other) -> void {
                check_copy_from(b, other);
                b.copy_from(other);  //
            })
            .def("reverse_axes", [](Buffer<> &b) -> Buffer<> {
                const int d = b.dimensions();
                std::vector<int> order;
                order.reserve(b.dimensions());
                for (int i = 0; i < b.dimensions(); i++) {
                    order.push_back(d - i - 1);
                }
                return b.transposed(order);  //
            })

            .def("add_dimension", static_cast<void (Buffer<>::*)()>(&Buffer<>::add_dimension))

            .def("allocate", [](Buffer<> &b) -> void {
                b.allocate(nullptr, nullptr);  //
            })
            .def("deallocate", static_cast<void (Buffer<>::*)()>(&Buffer<>::deallocate))
            .def("device_deallocate", static_cast<void (Buffer<>::*)()>(&Buffer<>::device_deallocate))

            .def("crop",  //
                 [](Buffer<> &b, int d, int min, int extent) -> void {
                     b.crop(d, min, extent);  //
                 },
                 py::arg("dimension"), py::arg("min"), py::arg("extent"))
            .def("crop",  //
                 [](Buffer<> &b, const std::vector<std::pair<int, int>> &rect) -> void {
                     b.crop(rect);  //
                 },
                 py::arg("rect"))

            .def("cropped",  //
                 [](Buffer<> &b, int d, int min, int extent) -> Buffer<> {
                     return b.cropped(d, min, extent);  //
                 },
                 py::arg("dimension"), py::arg("min"), py::arg("extent"))
            .def("cropped",  //
                 [](Buffer<> &b, const std::vector<std::pair<int, int>> &rect) -> Buffer<> {
                     return b.cropped(rect);  //
                 },
                 py::arg("rect"))

            .def("embed",  //
                 [](Buffer<> &b, int d, int pos) -> void {
                     b.embed(d, pos);  //
                 },
                 py::arg("dimension"), py::arg("pos"))
            .def("embedded",  //
                 [](Buffer<> &b, int d, int pos) -> Buffer<> {
                     return b.embedded(d, pos);  //
                 },
                 py::arg("dimension"), py::arg("pos"))

            .def("embed",  //
                 [](Buffer<> &b, int d) -> void {
                     b.embed(d);  //
                 },
                 py::arg("dimension"))
            .def("embedded",  //
                 [](Buffer<> &b, int d) -> Buffer<> {
                     return b.embedded(d);  //
                 },
                 py::arg("dimension"))

            .def("slice",  //
                 [](Buffer<> &b, int d, int pos) -> void {
                     b.slice(d, pos);  //
                 },
                 py::arg("dimension"), py::arg("pos"))
            .def("sliced",  //
                 [](Buffer<> &b, int d, int pos) -> Buffer<> {
                     return b.sliced(d, pos);  //
                 },
                 py::arg("dimension"), py::arg("pos"))

            .def("slice",  //
                 [](Buffer<> &b, int d) -> void {
                     b.slice(d);  //
                 },
                 py::arg("dimension"))
            .def("sliced",  //
                 [](Buffer<> &b, int d) -> Buffer<> {
                     return b.sliced(d);  //
                 },
                 py::arg("dimension"))

            .def("translate",  //
                 [](Buffer<> &b, int d, int dx) -> void {
                     b.translate(d, dx);  //
                 },
                 py::arg("dimension"), py::arg("dx"))
            .def("translate",  //
                 [](Buffer<> &b, const std::vector<int> &delta) -> void {
                     b.translate(delta);  //
                 },
                 py::arg("delta"))

            .def("translated",  //
                 [](Buffer<> &b, int d, int dx) -> Buffer<> {
                     return b.translated(d, dx);  //
                 },
                 py::arg("dimension"), py::arg("dx"))
            .def("translated",  //
                 [](Buffer<> &b, const std::vector<int> &delta) -> Buffer<> {
                     return b.translated(delta);  //
                 },
                 py::arg("delta"))

            .def("transpose",  //
                 [](Buffer<> &b, int d1, int d2) -> void {
                     b.transpose(d1, d2);  //
                 },
                 py::arg("d1"), py::arg("d2"))

            .def("transposed",  //
                 [](Buffer<> &b, int d1, int d2) -> Buffer<> {
                     return b.transposed(d1, d2);  //
                 },
                 py::arg("d1"), py::arg("d2"))

            .def("transpose",  //
                 [](Buffer<> &b, const std::vector<int> &order) -> void {
                     b.transpose(order);  //
                 },
                 py::arg("order"))

            .def("transposed",  //
                 [](Buffer<> &b, const std::vector<int> &order) -> Buffer<> {
                     return b.transposed(order);  //
                 },
                 py::arg("order"))

            .def("dim",  //
                 [](Buffer<> &b, int dimension) -> BufferDimension {
                     return b.dim(dimension);  //
                 },
                 py::arg("dimension"), py::keep_alive<0, 1>()  // Keep Buffer<> alive while Dimension exists
                 )

            .def("for_each_element",  //
                 [](Buffer<> &b, py::callable f) -> void {
                     const int d = b.dimensions();
                     std::vector<int> pos_v(d, 0);
                     b.for_each_element([&f, &pos_v](const int *pos) -> void {
                         for (size_t i = 0; i < pos_v.size(); ++i) {
                             pos_v[i] = pos[i];
                         }
                         f(pos_v);
                     });  //
                 },
                 py::arg("f"))

            .def("fill", &call_fill, py::arg("value"))
            .def("all_equal", &call_all_equal, py::arg("value"))

            // TODO: for_each_value() needs to be rethought a bit for Python;
            // for C++ is passes a by-reference to each value (for mutation),
            // but Python doesn't allow mutable references to intrinsic types
            // like int. Leaving unimplemented for now.
            // .def("for_each_value", [](Buffer<> &b, py::args f, py::args other_buffers) -> void {
            // }, py::arg("f"))

            .def("copy_to_host", [](Buffer<> &b) -> int {  //
                return b.copy_to_host(nullptr);
            })
            .def("device_detach_native", [](Buffer<> &b) -> int {  //
                return b.device_detach_native(nullptr);
            })
            .def("device_free", [](Buffer<> &b) -> int {  //
                return b.device_free(nullptr);
            })
            .def("device_sync", [](Buffer<> &b) -> int {  //
                return b.device_sync(nullptr);
            })

            .def("copy_to_device", [](Buffer<> &b, const Target &target) -> int {  //
                return b.copy_to_device(to_jit_target(target));
            },
                 py::arg("target") = Target())

            .def("copy_to_device", [](Buffer<> &b, const DeviceAPI &d, const Target &target) -> int {  //
                return b.copy_to_device(d, to_jit_target(target));
            },
                 py::arg("device_api"), py::arg("target") = Target())
            .def("device_malloc", [](Buffer<> &b, const Target &target) -> int {  //
                return b.device_malloc(to_jit_target(target));
            },
                 py::arg("target") = Target())

            .def("device_malloc", [](Buffer<> &b, const DeviceAPI &d, const Target &target) -> int {  //
                return b.device_malloc(d, to_jit_target(target));
            },
                 py::arg("device_api"), py::arg("target") = Target())

            .def(           //
                "set_min",  //
                [](Buffer<> &b, const std::vector<int> &mins) -> void {
                    if (mins.size() > (size_t)b.dimensions()) {
                        throw py::value_error("Too many arguments");
                    }
                    b.set_min(mins);  //
                },
                py::arg("mins"))

            .def(  //
                "contains", [](Buffer<> &b, const std::vector<int> &coords) -> bool {
                    if (coords.size() > (size_t)b.dimensions()) {
                        throw py::value_error("Too many arguments");
                    }
                    return b.contains(coords);  //
                },
                py::arg("coords"))

            .def("__getitem__", [](Buffer<> &buf, const int &pos) -> py::object {  //
                return buffer_getitem_operator(buf, {pos});
            })
            .def("__getitem__", [](Buffer<> &buf, const std::vector<int> &pos) -> py::object {  //
                return buffer_getitem_operator(buf, pos);
            })

            .def("__getitem__", [](Buffer<> &buf, const Expr &pos) -> Expr {  //
                return buf(std::vector<Expr>{pos});
            })
            .def("__getitem__", [](Buffer<> &buf, const std::vector<Expr> &pos) -> Expr {  //
                return buf(pos);
            })

            .def("__setitem__", [](Buffer<> &buf, const int &pos, const py::object &value) -> py::object {  //
                return buffer_setitem_operator(buf, {pos}, value);
            })
            .def("__setitem__", [](Buffer<> &buf, const std::vector<int> &pos, const py::object &value) -> py::object {  //
                return buffer_setitem_operator(buf, pos, value);
            })

            .def("__repr__", [](const Buffer<> &b) -> std::string {
                std::ostringstream o;
                if (b.defined()) {
                    o << "<halide.Buffer of type " << halide_type_to_string(b.type()) << " shape:" << get_buffer_shape(b) << ">";
                } else {
                    o << "<undefined halide.Buffer>";
                }
                return o.str();  //
            })

            .def("get", [](const py::object &self) -> py::object {
                const auto &buffer = py::cast<const Buffer<> &>(self);
                py::object runtime_buffer = py::module_::import_("halide.runtime").attr("Buffer");
                if (!buffer.defined()) {
                    return runtime_buffer();
                }
                return runtime_buffer.attr("_from_borrowed")(
                    py::capsule(buffer.get(), runtime_buffer_capsule_name), self, buffer.name()); })

            .def("_get_halide_buffer_t_capsule", [](Buffer<> &b) -> py::capsule {
                if (!b.defined()) {
                    throw py::value_error("Buffer is undefined.");
                }
                return py::capsule(b.raw_buffer(), halide_buffer_capsule_name);  //
            });
}

}  // namespace PythonBindings
}  // namespace Halide
