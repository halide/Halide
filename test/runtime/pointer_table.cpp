#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/pointer_table.h"

using namespace Halide::Runtime::Internal;

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    void *p[8];
    for (int i = 0; i < 8; ++i) {
        p[i] = (void *)(uintptr_t)(0x100 + i);
    }

    // test insert / prepend / remove ordering
    {
        PointerTable pt(user_context, 0);
        HALIDE_CHECK(user_context, pt.size() == 0);

        pt.append(user_context, p[0]);
        pt.append(user_context, p[1]);
        HALIDE_CHECK(user_context, pt.size() == 2);
        HALIDE_CHECK(user_context, pt[0] == p[0]);
        HALIDE_CHECK(user_context, pt[1] == p[1]);

        pt.insert(user_context, 1, p[2]);
        HALIDE_CHECK(user_context, pt.size() == 3);
        HALIDE_CHECK(user_context, pt[0] == p[0]);
        HALIDE_CHECK(user_context, pt[1] == p[2]);  // inserted here
        HALIDE_CHECK(user_context, pt[2] == p[1]);

        pt.prepend(user_context, p[3]);
        HALIDE_CHECK(user_context, pt.size() == 4);
        HALIDE_CHECK(user_context, pt[0] == p[3]);  // prepended here
        HALIDE_CHECK(user_context, pt[1] == p[0]);
        HALIDE_CHECK(user_context, pt[2] == p[2]);
        HALIDE_CHECK(user_context, pt[3] == p[1]);

        pt.pop_front(user_context);
        HALIDE_CHECK(user_context, pt.size() == 3);
        HALIDE_CHECK(user_context, pt[0] == p[0]);
        HALIDE_CHECK(user_context, pt[1] == p[2]);
        HALIDE_CHECK(user_context, pt[2] == p[1]);

        pt.remove(user_context, 1);
        HALIDE_CHECK(user_context, pt.size() == 2);
        HALIDE_CHECK(user_context, pt[0] == p[0]);
        HALIDE_CHECK(user_context, pt[1] == p[1]);
    }

    // test multi-element insert in the middle
    {
        PointerTable pt(user_context, 0);
        pt.append(user_context, p[0]);
        pt.append(user_context, p[1]);

        const void *arr[3] = {p[2], p[3], p[4]};
        pt.insert(user_context, 1, arr, 3);
        HALIDE_CHECK(user_context, pt.size() == 5);
        HALIDE_CHECK(user_context, pt[0] == p[0]);
        HALIDE_CHECK(user_context, pt[1] == p[2]);
        HALIDE_CHECK(user_context, pt[2] == p[3]);
        HALIDE_CHECK(user_context, pt[3] == p[4]);
        HALIDE_CHECK(user_context, pt[4] == p[1]);
    }

    // test growing past the initial capacity with a prepend, then pop_front
    {
        PointerTable pt(user_context, 0);
        const size_t entry_count = 2 * PointerTable::default_capacity;
        for (size_t i = 0; i < entry_count; ++i) {
            pt.append(user_context, (void *)(uintptr_t)(0x1000 + i));
        }
        HALIDE_CHECK(user_context, pt.size() == entry_count);

        void *marker = (void *)(uintptr_t)0xBEEF;
        pt.prepend(user_context, marker);
        HALIDE_CHECK(user_context, pt.size() == entry_count + 1);
        HALIDE_CHECK(user_context, pt[0] == marker);
        for (size_t i = 0; i < entry_count; ++i) {
            HALIDE_CHECK(user_context, pt[i + 1] == (void *)(uintptr_t)(0x1000 + i));
        }

        pt.pop_front(user_context);
        HALIDE_CHECK(user_context, pt.size() == entry_count);
        for (size_t i = 0; i < entry_count; ++i) {
            HALIDE_CHECK(user_context, pt[i] == (void *)(uintptr_t)(0x1000 + i));
        }
    }

    print(user_context) << "Success!\n";
    return 0;
}
