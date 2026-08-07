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

    // test growing past the initial capacity
    {
        // The table is backed by PointerTable, which reallocates once the
        // entry count passes its default capacity.
        const size_t entry_count = 2 * PointerTable::default_capacity;
        const char *entry = "extension_name";

        // Uses the default allocator on purpose: allocate_system() pads every
        // block, which would hide a read past the end of one.
        StringTable st2(user_context, 0);
        for (size_t i = 0; i < entry_count; ++i) {
            st2.append(user_context, entry);
        }
        HALIDE_CHECK(user_context, st2.size() == entry_count);
        HALIDE_CHECK(user_context, st2.contains(entry));
        for (size_t i = 0; i < entry_count; ++i) {
            HALIDE_CHECK(user_context, strncmp(st2[i], entry, strlen(entry)) == 0);
        }
    }

    print(user_context) << "Success!\n";
    return 0;
}
