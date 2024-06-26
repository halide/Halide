#include <exception>

namespace Halide::Internal {

extern void unhandled_exception_handler();

struct hl_set_terminate_handler {
    hl_set_terminate_handler() noexcept {
#ifdef __cpp_exceptions
        std::set_terminate(::Halide::Internal::unhandled_exception_handler);
#endif
    }
};

hl_set_terminate_handler _terminate_handler{};

}  // namespace Halide::Internal
