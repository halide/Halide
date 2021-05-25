#include <csignal>
#include <cstdlib>

// This is a hack to implement death tests in CTest.
extern "C" void hl_error_test_handle_abort(int) {
    std::_Exit(EXIT_FAILURE);
}

struct hl_override_abort {
    hl_override_abort() noexcept {
        std::signal(SIGABRT, hl_error_test_handle_abort);
    }
};

hl_override_abort handler{};
