#include <stdio.h>
#include <string>
#include <vector>
#include <Halide.h>

using namespace Halide;

std::vector<std::string> messages;

extern "C" void halide_print(void *user_context, const char *message) {
    printf("%s", message);
    messages.push_back(message);
}


int main(int argc, char **argv) {
    #ifdef _WIN32
    printf("Skipping test because use of varags on windows under older llvms (e.g. pnacl) crashes.");
    return 0;
    #endif

    Var x("x");
    Func f("f"), g("g");

    f(x) = print(x * x, "the answer is", 42.0f, "unsigned", cast<uint32_t>(145));
    f.set_custom_print(halide_print);
    Image<int32_t> result = f.realize(10);

    for (int32_t i = 0; i < 10; i++) {
        if (result(i) != i * i) {
            return -1;
        }
    }

    assert(messages.size() == 10);
    for (size_t i = 0; i < messages.size(); i++) {
        long long square;
        float forty_two;
        unsigned long long one_forty_five;

        int scan_count = sscanf(messages[i].c_str(), "%lld the answer is %f unsigned %llu",
                                &square, &forty_two, &one_forty_five);
        assert(scan_count == 3);
        assert(square == i * i);
        assert(forty_two == 42.0f);
        assert(one_forty_five == 145);
    }

    messages.clear();

    Param<void *> random_handle;
    random_handle.set((void *)127);

    // Test a string containing a printf format specifier.
    g(x) = print_when(x == 3, x * x, "g", 42.0f, "%s", random_handle);
    g.set_custom_print(halide_print);
    result = g.realize(10);

    for (int32_t i = 0; i < 10; i++) {
        if (result(i) != i * i) {
            return -1;
        }
    }

    assert(messages.size() == 1);
    long long nine;
    float forty_two;
    void *ptr;

    int scan_count = sscanf(messages[0].c_str(), "%lld g %f %%s %p",
                            &nine, &forty_two, &ptr);
    assert(scan_count == 3);
    assert(nine == 9);
    assert(forty_two == 42.0f);
    assert(ptr == (void *)127);

    printf("Success!\n");
    return 0;
}
