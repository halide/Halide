#include "Halide.h"
#include <stdio.h>

using namespace Halide;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// Make a custom strlen so that it always returns a 32-bit int,
// instead of switching based on bit-width.
extern "C" DLLEXPORT
int my_strlen(const char *c) {
    int l = 0;
    while (*c) {
        c++;
        l++;
    }
    return l;
}

HalideExtern_1(int, my_strlen, const char *);

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        printf("Skipping handle test for JavaScript.\n");
        return 0;
    }
    const char *c_message = "Hello, world!";

    Param<const char *> message;
    message.set(c_message);

    int result = evaluate<int>(my_strlen(message));

    int correct = my_strlen(c_message);
    if (result != correct) {
        printf("strlen(%s) -> %d instead of %d\n",
               c_message, result, correct);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
