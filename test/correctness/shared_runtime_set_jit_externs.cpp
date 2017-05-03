#include "Halide.h"
#include <stdio.h>

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

const int kSize = 10;

namespace {

enum {
    expect_none,
    expect_set_custom_print,
    expect_set_default_handlers_print,
    expect_set_jit_externs_print,
} expected_print_func = expect_none;

std::vector<std::string> messages;

void my_print(void *user_context, const char *message) {
    printf("%d: %s", expected_print_func, message);
    messages.push_back(message);
}

}  // namespace

extern "C" DLLEXPORT void set_custom_print(void *user_context, const char *message) {
    assert(expected_print_func == expect_set_custom_print);
    my_print(user_context, message);
}

extern "C" DLLEXPORT void set_default_handlers_print(void *user_context, const char *message) {
    assert(expected_print_func == expect_set_default_handlers_print);
    my_print(user_context, message);
}

extern "C" DLLEXPORT void set_jit_externs_print(void *user_context, const char *message) {
    assert(expected_print_func == expect_set_jit_externs_print);
    my_print(user_context, message);
}

void check_results(const Buffer<int32_t> &result) {
    for (int32_t i = 0; i < kSize; i++) {
        if (result(i) != i * i) {
            fprintf(stderr, "Wrong answer\n");
            exit(-1);
        }
    }
    assert(messages.size() == kSize);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().has_feature(Target::Profile)) {
        // The profiler adds lots of extra prints, so counting the
        // number of prints is not useful.
        printf("Skipping test because profiler is active\n");
        return 0;
    }

    Var x, y;
    {
        messages.clear();
        expected_print_func = expect_none;

        Func f;
        f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
        Buffer<int32_t> result = f.realize(kSize);
        assert(messages.empty());
    }
    {
        messages.clear();
        expected_print_func = expect_set_default_handlers_print;

        Internal::JITHandlers handlers;
        handlers.custom_print = set_default_handlers_print;
        Internal::JITSharedRuntime::set_default_handlers(handlers);

        Func f;
        f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
        Buffer<int32_t> result = f.realize(kSize);
        check_results(result);
    }
    {
        messages.clear();
        expected_print_func = expect_set_jit_externs_print;

        Internal::JITSharedRuntime::set_jit_externs({
            { "halide_print", set_jit_externs_print },
        });
        Internal::JITSharedRuntime::release_all();

        Func f;
        f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
        Buffer<int32_t> result = f.realize(kSize);
        check_results(result);
    }
    {
        Internal::JITSharedRuntime::set_jit_externs({});
        Internal::JITSharedRuntime::release_all();
        messages.clear();
        expected_print_func = expect_set_custom_print;

        Func f;
        f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
        f.set_custom_print(set_custom_print);
        Buffer<int32_t> result = f.realize(kSize);
        check_results(result);
    }

    printf("Success!\n");
    return 0;
}
