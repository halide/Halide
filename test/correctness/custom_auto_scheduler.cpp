#include "Halide.h"

using namespace Halide;

int call_count = 0;

std::string inline_everything(Pipeline,
                              const Target &,
                              const MachineParams &) {
    call_count++;
    // Inlining everything is really easy.
    return "";
}

int main(int argc, char **argv) {

    // All pipelines built within this process should use my autoscheduler
    Pipeline::set_custom_auto_scheduler(inline_everything);

    Func f;
    Var x;
    f(x) = 3;
    Pipeline(f).auto_schedule(Target("host"));

    Func g;
    g(x) = 3;
    Pipeline(g).auto_schedule(Target("host"));

    if (call_count != 2) {
        printf("Should have called the custom autoscheduler twice. Instead called it %d times\n", call_count);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
