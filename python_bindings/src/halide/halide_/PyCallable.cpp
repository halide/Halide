#include "PyCallable.h"

#include "PyBuffer.h"

#define TYPED_ALLOCA(TYPE, COUNT) ((TYPE *)alloca(sizeof(TYPE) * (COUNT)))

namespace Halide {
namespace PythonBindings {

namespace {

// We avoid extra dynamic memory allocations for Buffers by preallocating enough
// space for 8 (rather than the default of 4) -- more is ok but slower, and > 8
// seems pretty unlikely for real world code.
constexpr int MaxFastDimensions = 8;
using HalideBuffer = Halide::Runtime::Buffer<void, AnyDims, MaxFastDimensions>;

struct HBufArray {
    const size_t count;
    HalideBuffer *buffers;

    explicit HBufArray(size_t count, void *storage)
        : count(count), buffers((HalideBuffer *)storage) {
        _halide_user_assert(storage);
        for (size_t i = 0; i < count; i++) {
            // placement new to get the ctors run
            new (&buffers[i]) HalideBuffer;
        }
    }

    ~HBufArray() {
        for (size_t i = 0; i < count; i++) {
            // Manually call the dtors
            buffers[i].~HalideBuffer();
        }
    }
};

template<typename T>
T cast_to(const py::handle &h) {
    // We want to ensure that the error thrown is one that will be translated
    // to `hl.HalideError` in Python.
    try {
        return h.cast<T>();
    } catch (const std::exception &e) {
        throw Halide::Error(e.what());
    }
}

}  // namespace

class PyCallable {
public:
    static void call_impl(Callable &c, const py::args &args, const py::kwargs &kwargs) {
        const size_t argc = c.arguments().size();
        _halide_user_assert(argc > 0);
        const Argument *c_args = c.arguments().data();

        // We want to keep call overhead as low as possible here,
        // so use alloca (rather than e.g. std::vector) for short-term
        // small allocations.
        const void **argv = TYPED_ALLOCA(const void *, argc);
        halide_scalar_value_t *scalar_storage = TYPED_ALLOCA(halide_scalar_value_t, argc);
        HBufArray buffers(argc, TYPED_ALLOCA(HalideBuffer, argc));
        Callable::QuickCallCheckInfo *cci = TYPED_ALLOCA(Callable::QuickCallCheckInfo, argc);

        _halide_user_assert(argv && scalar_storage && buffers.buffers && cci) << "alloca failure";

        // Clear argv to all zero so we can use it to validate that all fields are
        // set properly when using kwargs -- a well-formed call will never have any
        // of the fields left null, nor any set twice. (The other alloca stuff can
        // keep garbage in unused parts.)
        memset(argv, 0, sizeof(const void *) * argc);

        _halide_user_assert(args.size() <= argc - 1)
            << "Expected at most " << (argc - 1) << " positional arguments, but saw " << args.size() << ".";

        // args
        JITUserContext empty_jit_user_context;
        scalar_storage[0].u.u64 = (uintptr_t)&empty_jit_user_context;
        argv[0] = &scalar_storage[0];
        cci[0] = Callable::make_ucon_qcci();

        const auto define_one_arg = [&argv, &scalar_storage, &buffers, &cci](const Argument &c_arg, py::handle value, size_t slot) {
            if (c_arg.is_buffer()) {
                // If the argument is already a Halide Buffer of some sort,
                // skip pybuffer_to_halidebuffer entirely, since the latter requires
                // a non-null host ptr, but we might want such a buffer for bounds inference,
                // and we don't need the intermediate HalideBuffer wrapper anyway.
                if (py::isinstance<Halide::Buffer<>>(value)) {
                    auto b = cast_to<Halide::Buffer<>>(value);
                    argv[slot] = b.raw_buffer();
                } else {
                    const bool writable = c_arg.is_output();
                    const bool reverse_axes = true;
                    buffers.buffers[slot] =
                        pybuffer_to_halidebuffer<void, AnyDims, MaxFastDimensions>(
                            cast_to<py::buffer>(value), writable, reverse_axes);
                    argv[slot] = buffers.buffers[slot].raw_buffer();
                }
                cci[slot] = Callable::make_buffer_qcci();
            } else {
                argv[slot] = &scalar_storage[slot];

                // clang-format off

                #define HALIDE_HANDLE_TYPE_DISPATCH(CODE, BITS, TYPE, FIELD)                \
                    case halide_type_t(CODE, BITS).as_u32():                                \
                        scalar_storage[slot].u.FIELD = cast_to<TYPE>(value);                \
                        cci[slot] = Callable::make_scalar_qcci(halide_type_t(CODE, BITS));   \
                        break;

                switch (((halide_type_t)c_arg.type).element_of().as_u32()) {
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 32, float, f32)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 64, double, f64)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 8, int8_t, i8)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 16, int16_t, i16)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 32, int32_t, i32)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 64, int64_t, i64)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 1, bool, b)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 8, uint8_t, u8)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 16, uint16_t, u16)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 32, uint32_t, u32)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 64, uint64_t, u64)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_handle, 64, uint64_t, u64)  // Handle types are always uint64, regardless of pointer size
                default:
                    _halide_user_assert(0) << "Unsupported type in Callable argument list: " << c_arg.type << "\n";
                }

                #undef HALIDE_HANDLE_TYPE_DISPATCH

                // clang-format on
            }
        };

        for (size_t i = 0; i < args.size(); i++) {
            const auto &c_arg = c_args[i + 1];  // c_args[0] is the JITUserContext
            const size_t slot = i + 1;
            define_one_arg(c_arg, args[i], slot);
        }

        if (!kwargs.empty()) {
            std::string trimmed_name;

            // Also process kwargs.
            for (auto kw : kwargs) {
                const std::string name = cast_to<std::string>(kw.first);

                const py::handle value = kw.second;

                // Find the slot with this name.
                // Skip element 0, since that's always JITUserContext and not visible in Python.
                //
                // TODO: should we build an inverse map here? For small numbers
                // of arguments a linear search is probably faster.
                for (size_t slot = 1; slot < argc; slot++) {
                    const auto &c_arg = c_args[slot];
                    const std::string *c_arg_name = &c_arg.name;
                    // The names in Arguments might be uniquified due to Func reuse.
                    // Check and trim off any residue and match just the previous part.
                    const size_t pos = c_arg_name->find_first_of('$');
                    if (pos != std::string::npos) {
                        trimmed_name = c_arg_name->substr(0, pos);
                        c_arg_name = &trimmed_name;
                    }
                    if (*c_arg_name == name) {
                        _halide_user_assert(argv[slot] == nullptr) << "Argument " << name << " specified multiple times.";
                        define_one_arg(c_arg, value, slot);
                        goto found_kw_arg;
                    }
                }
                _halide_user_assert(0) << "Unknown argument '" << name << "' specified via keyword.";

            found_kw_arg:
                continue;
            }

            // Verify all slots were filled.
            for (size_t slot = 1; slot < argc; slot++) {
                _halide_user_assert(argv[slot] != nullptr) << "Argument " << c_args[slot].name << " was not specified by either positional or keyword argument.";
            }
        } else {
            // Everything should have been positional
            _halide_user_assert(args.size() == argc - 1)
                << "Expected exactly " << (argc - 1) << " positional arguments, but saw " << args.size() << ".";
        }

        int result = c.call_argv_checked(argc, argv, cci);
        _halide_user_assert(result == 0) << "Halide Runtime Error: " << result;
    }

#undef TYPED_ALLOCA
};

void define_callable(py::module &m) {
    // Not supported yet, because we want to think about how to expose runtime
    // overrides in Python (https://github.com/halide/Halide/issues/2790):
    // - JITUserContext

    auto callable_class =
        py::class_<Callable>(m, "Callable")
            .def("__call__", PyCallable::call_impl);
}

}  // namespace PythonBindings
}  // namespace Halide
