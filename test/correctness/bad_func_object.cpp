#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

int main() {
    return error_test("bad_func_object", "Can't construct Func from undefined Function", []() {
        const Internal::Function func{};
        const Func f{func};  // internal_assert

        std::cout << f.name() << "\n";  // segfaults without above assert
    });
}
