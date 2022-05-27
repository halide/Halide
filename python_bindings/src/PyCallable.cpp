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

}  // namespace

class PyCallable {
public:
    // TODO: support kwargs here too.
    static void call_impl(Callable &c, const py::args &args) {
        const size_t argc = c.arguments().size();
        const Argument *c_args = c.arguments().data();

        // We want to keep call overhead as low as possible here,
        // so use alloca (rather than e.g. std::vector) for short-term
        // small allocations.
        const void **argv = TYPED_ALLOCA(const void *, argc);
        uintptr_t *scalar_storage = TYPED_ALLOCA(uintptr_t, argc);
        HBufArray buffers(argc, TYPED_ALLOCA(HalideBuffer, argc));

        _halide_user_assert(argv && scalar_storage && buffers.buffers) << "alloca failure";

        _halide_user_assert(args.size() == argc - 1) << "Expected exactly " << (argc - 1) << " arguments.";

        // args
        scalar_storage[0] = (uintptr_t)&Callable::empty_jit_user_context;
        argv[0] = &scalar_storage[0];

        for (size_t i = 1; i < argc; i++) {
            py::object a = args[i - 1];
            if (c_args[i].is_buffer()) {
                const bool writable = c_args[i].is_output();
                buffers.buffers[i] = pybuffer_to_halidebuffer<void, AnyDims, MaxFastDimensions>(a.cast<py::buffer>(), writable);
                argv[i] = buffers.buffers[i].raw_buffer();
            } else {
                argv[i] = &scalar_storage[i];

                // clang-format off

                #define HALIDE_HANDLE_TYPE_DISPATCH(CODE, BITS, TYPE) \
                    case halide_type_t(CODE, BITS).as_u32():          \
                        *(TYPE *)&scalar_storage[i] = a.cast<TYPE>(); \
                        break;

                switch (((halide_type_t)c_args[i].type).element_of().as_u32()) {
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 32, float)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 64, double)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 8, int8_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 16, int16_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 32, int32_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 64, int64_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 1, bool)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 8, uint8_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 16, uint16_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 32, uint32_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 64, uint64_t)
                    HALIDE_HANDLE_TYPE_DISPATCH(halide_type_handle, 64, uint64_t)  // Handle types are always uint64, regardless of pointer size
                default:
                    _halide_user_assert(0) << "Unsupported type in Callable argument list: " << c_args[i].type << "\n";
                }

                #undef HALIDE_HANDLE_TYPE_DISPATCH

                // clang-format on
            }
        }

        int result = c.call_argv(argc, argv);
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
