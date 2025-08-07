#include "Halide.h"
using namespace Halide;

int main() {
    const Internal::Function func{};
    const Func f{func};  // internal_assert

    std::cout << f.name() << "\n";  // segfaults without above assert

    printf("Success!\n");
    return 0;
}
