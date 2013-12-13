#include <Halide.h>
#include <stdio.h>

using namespace Halide;


#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT 
int extern_error(buffer_t *out) {
    return -1;
}

bool error_occurred = false;
extern "C" DLLEXPORT
void my_halide_error(void *user_context, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    Func f;
    f.define_extern("extern_error", std::vector<ExternFuncArgument>(),
                    Float(32), 1);

    f.set_error_handler(&my_halide_error);
    f.realize(100);

    if (!error_occurred) {
        printf("There was supposed to be an error\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
