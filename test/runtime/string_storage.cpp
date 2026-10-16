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

    // test StringUtils::copy_up_to bounds
    {
        // The destination is exactly max_chars bytes; a guard byte sits
        // immediately after it. copy_up_to must never write past max_chars,
        // even when the source is longer, and must terminate in bounds.
        constexpr size_t max_chars = 8;
        char storage[max_chars + 1];
        const char guard = (char)0x7f;
        storage[max_chars] = guard;

        const char *long_src = "abcdefghijkl";  // longer than max_chars
        size_t copied = StringUtils::copy_up_to(storage, long_src, max_chars);
        HALIDE_CHECK(user_context, storage[max_chars] == guard);
        HALIDE_CHECK(user_context, copied == max_chars - 1);
        HALIDE_CHECK(user_context, storage[copied] == '\0');

        // A source that fits is copied verbatim and terminated in bounds.
        const char *short_src = "abc";
        size_t copied_short = StringUtils::copy_up_to(storage, short_src, max_chars);
        HALIDE_CHECK(user_context, copied_short == strlen(short_src));
        HALIDE_CHECK(user_context, storage[copied_short] == '\0');
        HALIDE_CHECK(user_context, storage[max_chars] == guard);
    }

    print(user_context) << "Success!\n";
    return 0;
}
