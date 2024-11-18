#include <Halide.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

std::atomic<bool> suppress_abort{true};

auto handler = ([]() {
#ifdef HALIDE_WITH_EXCEPTIONS
    std::set_terminate([]() {  //
        try {
            if (const auto e = std::current_exception()) {
                std::rethrow_exception(e);
            }
        } catch (const Halide::InternalError &e) {
            std::cerr << e.what() << "\n"
                      << std::flush;
            suppress_abort = false;
            std::abort();  // We should never EXPECT an internal error
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n"
                      << std::flush;
        } catch (...) {}
        std::_Exit(EXIT_FAILURE);
    });
#endif

    // If exceptions are disabled, we just hope for the best.
    return std::signal(SIGABRT, [](int) {
        if (suppress_abort) {
            std::_Exit(EXIT_FAILURE);
        }
    });
})();
