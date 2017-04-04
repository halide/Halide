#include "HalideRuntime.h"

extern "C" int32_t gen_extern_tester(int32_t in) {
    return in + 42;
}
