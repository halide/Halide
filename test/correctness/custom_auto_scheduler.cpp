#include "Halide.h"

using namespace Halide;

int call_count = 0;

void inline_everything(Pipeline,
                       const Target &,
                       const MachineParams &,
                       AutoSchedulerResults *) {
    call_count++;
    // Inlining everything is really easy.
}

int main(int argc, char **argv) {

    const char *kSchedulerName = "inline_everything";

    // Add a very simple 'autoscheduler'
    Pipeline::add_autoscheduler(kSchedulerName, inline_everything);

    Func f;
    Var x;
    f(x) = 3;
    Pipeline(f).auto_schedule(kSchedulerName, Target("host"));

    Pipeline::set_default_autoscheduler_name(kSchedulerName);

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
