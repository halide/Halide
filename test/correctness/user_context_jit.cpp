#include <stdio.h>
#include <Halide.h>

using namespace Halide;

namespace {

static void *context_pointer = (void *)0xf00dd00d;

static bool called_error = false;
static bool called_trace = false;
static bool called_malloc = false;
static bool called_free = false;

void my_error(void *context, const char *msg) {
    called_error = true;
    assert(context == context_pointer);
}

int32_t my_trace(void *context, const halide_trace_event *e) {
    called_trace = true;
    assert(context == context_pointer);
    return 0;
}

void *my_malloc(void *context, size_t sz) {
    assert(context == context_pointer);
    called_malloc = true;
    return malloc(sz);
}

void my_free(void *context, void *ptr) {
    assert(context == context_pointer);
    called_free = true;
    free(ptr);
}

}  // namespace

int main(int argc, char **argv) {
    Var x, y;

    Image<float> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y) = 1;
        }
    }

    Func g;
    g(x, y) = input(x, y) * 2;
    g.compute_root();

    Func f;
    f(x, y) = g(x, y);

    f.parallel(y);
    f.trace_stores();

    f.set_error_handler(my_error);
    f.set_custom_allocator(my_malloc, my_free);
    f.set_custom_trace(my_trace);
    f.set_custom_user_context(context_pointer);
    Image<float> output = f.realize(10, 10);

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            if (output(x, y) != 2) {
                printf("Failure\n");
                return -1;
            }
        }
    }

    assert(called_malloc);
    assert(called_free);
    assert(called_trace);
    assert(!called_error);

    printf("Success!\n");
    return 0;
}
