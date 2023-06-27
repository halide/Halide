#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Make a custom strlen so that it always returns a 32-bit int,
// instead of switching based on bit-width.
extern "C" HALIDE_EXPORT_SYMBOL int my_strlen(const char *c) {
    int l = 0;
    while (*c) {
        c++;
        l++;
    }
    return l;
}

HalideExtern_1(int, my_strlen, const char *);

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support Param<> for pointer types.\n");
        return 0;
    }

    // Check we can pass a Handle through to an extern function.
    {
        const char *c_message = "Hello, world!";

        Param<const char *> message;
        message.set(c_message);

        int result = evaluate<int>(my_strlen(message));

        int correct = my_strlen(c_message);
        if (result != correct) {
            printf("strlen(%s) -> %d instead of %d\n",
                   c_message, result, correct);
            return 1;
        }
    }

    // Check that storing and loading handles acts like uint64_t
    {
        std::string msg = "hello!\n";
        Func f, g, h;
        Var x;
        f(x) = cast<char *>(msg);
        f.compute_root().vectorize(x, 4);
        g(x) = f(x);
        g.compute_root();
        h(x) = g(x);

        Buffer<char *> im = h.realize({100});

        uint64_t handle = (uint64_t)(im(0));
        if (sizeof(char *) == 4) {
            // On 32-bit systems, the upper four bytes should be zero
            if (handle >> 32) {
                printf("The upper four bytes of a handle should have been zero on a 32-bit system\n");
            }
        }
        // As another sanity check, the internal pointer to the string constant should be aligned.
        if (handle & 0x3) {
            printf("Got handle: %llx. A handle should be aligned to at least four bytes\n", (long long)handle);
            return 1;
        }

        for (int i = 0; i < im.width(); i++) {
            if (im(i) != im(0)) {
                printf("im(%d) = %p instead of %p\n",
                       i, im(i), im(0));
                return 1;
            }
            if (std::string(im(i)) != msg) {
                printf("Handle was %s instead of %s\n", im(i), msg.c_str());
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
