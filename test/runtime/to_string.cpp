#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

using namespace Halide::Runtime::Internal;

namespace {

bool int64_prints_as(int64_t value, const char *expected) {
    char buf[32];
    char *end = halide_int64_to_string(buf, buf + sizeof(buf), value, 1);
    return strcmp(buf, expected) == 0 && (size_t)(end - buf) == strlen(expected);
}

}  // namespace

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // test halide_int64_to_string across the full int64 range
    {
        const int64_t int64_max = 0x7fffffffffffffffLL;
        const int64_t int64_min = -int64_max - 1;
        HALIDE_CHECK(user_context, int64_prints_as(0, "0"));
        HALIDE_CHECK(user_context, int64_prints_as(42, "42"));
        HALIDE_CHECK(user_context, int64_prints_as(-42, "-42"));
        HALIDE_CHECK(user_context, int64_prints_as(int64_max, "9223372036854775807"));
        HALIDE_CHECK(user_context, int64_prints_as(-int64_max, "-9223372036854775807"));
        HALIDE_CHECK(user_context, int64_prints_as(int64_min, "-9223372036854775808"));
    }

    print(user_context) << "Success!\n";
    return 0;
}
