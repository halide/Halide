#include <Halide.h>
#include <stdio.h>

using namespace Halide;

HalideExtern_1(int, strlen, const char *);

int main(int argc, char **argv) {
    const char *c_message = "Hello, world!";

    Param<const char *> message;
    message.set(c_message);

    int result = evaluate<int>(strlen(message));

    int correct = strlen(c_message);
    if (result != correct) {
        printf("strlen(%s) -> %d instead of %d\n",
               c_message, result, correct);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
