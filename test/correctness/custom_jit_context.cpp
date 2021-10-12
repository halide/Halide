#include "Halide.h"

using namespace Halide;

struct MyJITContext : Halide::JITUserContext {
    int which_handler = 0;
};

void my_print_handler_1(JITUserContext *u, const char *msg) {
    ((MyJITContext *)u)->which_handler = 1;
}

void my_print_handler_2(JITUserContext *u, const char *msg) {
    ((MyJITContext *)u)->which_handler = 2;
}

void my_print_handler_3(JITUserContext *u, const char *msg) {
    ((MyJITContext *)u)->which_handler = 3;
}

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = print(x);

    MyJITContext ctx1, ctx2;
    ctx1.handlers.custom_print = my_print_handler_1;
    ctx2.handlers.custom_print = my_print_handler_2;
    f.jit_handlers().custom_print = my_print_handler_3;

    ctx1.which_handler = 0;
    f.realize(&ctx1, {100});
    if (ctx1.which_handler != 1) {
        printf("Fail to call per-call custom print handler 1: %d\n", ctx1.which_handler);
        return -1;
    }

    ctx2.which_handler = 0;
    f.realize(&ctx2, {100});
    if (ctx2.which_handler != 2) {
        printf("Fail to call per-call custom print handler 2: %d\n", ctx2.which_handler);
        return -1;
    }

    ctx1.handlers.custom_print = nullptr;
    f.realize(&ctx1, {100});
    if (ctx1.which_handler != 3) {
        printf("Fail to call per-Pipeline custom print handler: %d\n", ctx1.which_handler);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
