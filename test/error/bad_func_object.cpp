#include "Halide.h"
using namespace Halide;

int main() {
#ifdef HALIDE_WITH_EXCEPTIONS
    try {
#endif
        const Internal::Function func{};
        const Func f{func};  // internal_assert

        std::cout << f.name() << "\n";  // segfaults without above assert
#ifdef HALIDE_WITH_EXCEPTIONS
    } catch (const InternalError &e) {
        // The harness rejects internal errors as they should typically not be
        // _expected_. However, we are directly testing a constructor invariant
        // check here, so an internal error is both expected and appropriate.
        throw std::runtime_error(e.what());
    }
#endif

    printf("Success!\n");
    return 0;
}
