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

void my_error_handler(JITUserContext *u, const char *msg) {
    ((MyJITContext *)u)->which_handler = 4;
}

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = print(x);

    // Test the appropriate handler is called and the appropriate
    // context object is passed to it in a variety of contexts.

    MyJITContext ctx1, ctx2;
    ctx1.handlers.custom_print = my_print_handler_1;
    ctx2.handlers.custom_print = my_print_handler_2;
    f.jit_handlers().custom_print = my_print_handler_3;

    ctx1.which_handler = 0;
    f.realize(&ctx1, {100});
    if (ctx1.which_handler != 1) {
        printf("Fail to call per-call custom print handler 1: %d\n", ctx1.which_handler);
        return 1;
    }

    ctx2.which_handler = 0;
    f.realize(&ctx2, {100});
    if (ctx2.which_handler != 2) {
        printf("Fail to call per-call custom print handler 2: %d\n", ctx2.which_handler);
        return 1;
    }

    ctx1.handlers.custom_print = nullptr;
    f.realize(&ctx1, {100});
    if (ctx1.which_handler != 3) {
        printf("Fail to call per-Pipeline custom print handler: %d\n", ctx1.which_handler);
        return 1;
    }

    Target t = get_jit_target_from_environment();
    if (t.has_feature(Target::CUDA)) {
        ctx1.handlers.custom_error = my_error_handler;
        Buffer<float> bad_buf(100, 100);
        bad_buf.raw_buffer()->device_interface = get_device_interface_for_device_api(DeviceAPI::CUDA);
        bad_buf.raw_buffer()->set_host_dirty(true);
        bad_buf.raw_buffer()->set_device_dirty(true);
        // This should fail and call the hooked error handler, because
        // device_dirty is set but there's no device allocation.
        bad_buf.copy_to_host(&ctx1);
        if (ctx1.which_handler != 4) {
            printf("Fail to call custom error handler from context passed to copy_to_host: %d\n", ctx1.which_handler);
            return 1;
        }

        ctx1.which_handler = 0;
        // This should also fail
        bad_buf.copy_to_device(t, &ctx1);
        if (ctx1.which_handler != 4) {
            printf("Fail to call custom error handler from context passed to copy_to_device: %d\n", ctx1.which_handler);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
