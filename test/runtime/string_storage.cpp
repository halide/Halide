#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/string_storage.h"

using namespace Halide::Runtime::Internal;

int main(int argc, char **argv) {
    void *user_context = (void *)1;
    SystemMemoryAllocatorFns test_allocator = {allocate_system, deallocate_system};

    // test class interface
    {
        StringStorage ss(user_context, 0, test_allocator);
        HALIDE_CHECK(user_context, ss.length() == 0);

        const char *ts1 = "Testing!";
        const size_t ts1_length = strlen(ts1);
        ss.assign(user_context, ts1);
        HALIDE_CHECK(user_context, ss.length() == ts1_length);
        HALIDE_CHECK(user_context, ss.contains(ts1));

        const char *ts2 = "More ";
        const size_t ts2_length = strlen(ts2);
        ss.prepend(user_context, ts2);
        HALIDE_CHECK(user_context, ss.length() == (ts1_length + ts2_length));
        HALIDE_CHECK(user_context, ss.contains(ts2));
        HALIDE_CHECK(user_context, ss.contains(ts1));

        ss.append(user_context, '!');
        HALIDE_CHECK(user_context, ss.length() == (ts1_length + ts2_length + 1));

        ss.clear(user_context);
        HALIDE_CHECK(user_context, ss.length() == 0);

        ss.destroy(user_context);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test copy and equality
    {
        const char *ts1 = "Test One!";
        const size_t ts1_length = strlen(ts1);

        const char *ts2 = "Test Two!";
        const size_t ts2_length = strlen(ts2);

        StringStorage ss1(user_context, 0, test_allocator);
        ss1.assign(user_context, ts1, ts1_length);

        StringStorage ss2(user_context, 0, test_allocator);
        ss2.assign(user_context, ts2, ts2_length);

        StringStorage ss3(ss1);

        HALIDE_CHECK(user_context, ss1.length() == (ts1_length));
        HALIDE_CHECK(user_context, ss2.length() == (ts2_length));
        HALIDE_CHECK(user_context, ss3.length() == ss1.length());

        HALIDE_CHECK(user_context, ss1 != ss2);
        HALIDE_CHECK(user_context, ss1 == ss3);

        ss2 = ss1;
        HALIDE_CHECK(user_context, ss1 == ss2);

        ss1.destroy(user_context);
        ss2.destroy(user_context);
        ss3.destroy(user_context);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }
    print(user_context) << "Success!\n";
    return 0;
}
