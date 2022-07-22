#include "common.h"

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
        halide_abort_if_false(user_context, st1.size() == 0);

        st1.fill(user_context, data, data_size);
        halide_abort_if_false(user_context, st1.size() == data_size);
        halide_abort_if_false(user_context, strncmp(st1[0], data[0], strlen(data[0])) == 0);
        halide_abort_if_false(user_context, strncmp(st1[1], data[1], strlen(data[1])) == 0);
        halide_abort_if_false(user_context, strncmp(st1[2], data[2], strlen(data[2])) == 0);
        halide_abort_if_false(user_context, strncmp(st1[3], data[3], strlen(data[3])) == 0);
        halide_abort_if_false(user_context, st1.contains(data[0]));
        halide_abort_if_false(user_context, st1.contains(data[1]));
        halide_abort_if_false(user_context, st1.contains(data[2]));
        halide_abort_if_false(user_context, st1.contains(data[3]));

        st1.clear(user_context);
        halide_abort_if_false(user_context, st1.size() == 0);

        size_t entry_count = st1.parse(user_context, "one:two:three:four", ":");
        halide_abort_if_false(user_context, entry_count == data_size);
        halide_abort_if_false(user_context, st1.size() == data_size);
        halide_abort_if_false(user_context, st1.contains(data[0]));
        halide_abort_if_false(user_context, st1.contains(data[1]));
        halide_abort_if_false(user_context, st1.contains(data[2]));
        halide_abort_if_false(user_context, st1.contains(data[3]));
    }

    print(user_context) << "Success!\n";
    return 0;
}
