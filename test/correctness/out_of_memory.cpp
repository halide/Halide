#include <Halide.h>
#include <stdio.h>
#include <sys/resource.h>

using namespace Halide;

bool error_occurred  = false;
extern "C" void handler(const char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    const int big = 1 << 26;
    Var x;
    std::vector<Func> funcs;
    funcs.push_back(lambda(x, cast<uint8_t>(x)));
    for (size_t i = 0; i < 10; i++) {
        Func f;
        f(x) = funcs[i](0) + funcs[i](big);
        funcs[i].compute_at(f, x);
        funcs.push_back(f);
    }

    // Limit ourselves to two stages worth of address space
    struct rlimit lim = {big << 1, big << 1};
    setrlimit(RLIMIT_AS, &lim);

    funcs[funcs.size()-1].set_error_handler(&handler);
    funcs[funcs.size()-1].realize(1);

    if (!error_occurred) {
        printf("There should have been an error\n");
        return -1;
    }

    printf("Success!\n");
    return 0;

}
