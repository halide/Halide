#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

struct struct_t {
    double a;
    int32_t b;
    int16_t c;
    const char *d;
};

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int check_struct(struct_t *s) {
    if (s->a != 3.0 ||
        s->b != 1234567 ||
        s->c != 1234 ||
        strcmp(s->d, "Test global string\n")) {
        printf("Unexpected struct values: %f %d %d %s\n", s->a, s->b, s->c, s->d);
        exit(-1);
    }
    return 0;
}

HalideExtern_1(int, check_struct, struct_t *);

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        printf("Skipping make_struct test for JavaScript as it uses a C extern function.\n");
        return 0;
    }

    // Check make_struct is working. make_struct is not intended to be
    // called from the front-end because the structs live on the stack
    // of the generated function. The generated structs should also
    // not be stored in Funcs. They're just pointers to a single stack
    // slot. There's also no way to extract fields from the struct
    // without an extern function. You can really only use them for
    // marshalling some arguments to immediately pass to an extern
    // call, and that's what they're used for in the runtime.

    Expr a = cast<double>(3.0f);
    Expr b = cast<int32_t>(1234567);
    Expr c = cast<int16_t>(1234);
    Expr d = std::string("Test global string\n");

    Expr s = Call::make(Handle(), Call::make_struct, {a, b, c, d}, Call::Intrinsic);

    Func g;
    g() = check_struct(s);

    g.realize();

    printf("Success!\n");

    return 0;
}
