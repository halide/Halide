#include <Halide.h>
#include <stdio.h>

using namespace Halide;

static void *context_pointer = (void *)0xf00dd00d;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

bool extern_error_called = false;
extern "C" DLLEXPORT
int extern_error(void *user_context, buffer_t *out) {
    assert(user_context == context_pointer);
    extern_error_called = true;
    return -1;
}

bool error_occurred = false;
extern "C" DLLEXPORT
void my_halide_error(void *user_context, const char *msg) {
    assert(user_context == context_pointer);
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    Param<void*> my_user_context = user_context_param();
    my_user_context.set(context_pointer);

    std::vector<ExternFuncArgument> args;
    args.push_back(my_user_context);

    Func f;
    f.define_extern("extern_error", args, Float(32), 1);

    f.set_error_handler(&my_halide_error);
    f.realize(100);

    if (!error_occurred || !extern_error_called) {
        printf("There was supposed to be an error\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
