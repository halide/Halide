#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"
#include "internal/block_allocator.h"

using namespace Halide::Runtime::Internal;

int main(int argc, char **argv) {
    void* user_context = (void*)1;
    print(user_context) << "Success!\n";
    return 0;
}
