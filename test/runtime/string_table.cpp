#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/string_table.h"

using namespace Halide::Runtime::Internal;

int main(int argc, char **argv) {
    void *user_context = (void *)1;
    SystemMemoryAllocatorFns test_allocator = {allocate_system, deallocate_system};

    // test class interface
    {
        size_t data_size = 4;
        const char *data[] = {
            "one", "two", "three", "four"};

        StringTable st1(user_context, 0, test_allocator);
        HALIDE_CHECK(user_context, st1.size() == 0);

        st1.fill(user_context, data, data_size);
        HALIDE_CHECK(user_context, st1.size() == data_size);
        HALIDE_CHECK(user_context, strncmp(st1[0], data[0], strlen(data[0])) == 0);
        HALIDE_CHECK(user_context, strncmp(st1[1], data[1], strlen(data[1])) == 0);
        HALIDE_CHECK(user_context, strncmp(st1[2], data[2], strlen(data[2])) == 0);
        HALIDE_CHECK(user_context, strncmp(st1[3], data[3], strlen(data[3])) == 0);
        HALIDE_CHECK(user_context, st1.contains(data[0]));
        HALIDE_CHECK(user_context, st1.contains(data[1]));
        HALIDE_CHECK(user_context, st1.contains(data[2]));
        HALIDE_CHECK(user_context, st1.contains(data[3]));

        st1.clear(user_context);
        HALIDE_CHECK(user_context, st1.size() == 0);

        size_t entry_count = st1.parse(user_context, "one:two:three:four", ":");
        HALIDE_CHECK(user_context, entry_count == data_size);
        HALIDE_CHECK(user_context, st1.size() == data_size);
        HALIDE_CHECK(user_context, st1.contains(data[0]));
        HALIDE_CHECK(user_context, st1.contains(data[1]));
        HALIDE_CHECK(user_context, st1.contains(data[2]));
        HALIDE_CHECK(user_context, st1.contains(data[3]));
    }

    print(user_context) << "Success!\n";
    return 0;
}
