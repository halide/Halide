#include "Halide.h"

using namespace Halide;

int call_count = 0;

void inline_everything(const Pipeline &,
                       const Target &,
                       const AutoschedulerParams &,
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

    Func g;
    g(x) = 3;

    Target t("host");

    AutoschedulerParams autoscheduler_params(kSchedulerName);
    Pipeline(f).apply_autoscheduler(t, autoscheduler_params);
    Pipeline(g).apply_autoscheduler(t, autoscheduler_params);

    if (call_count != 2) {
        printf("Should have called the custom autoscheduler twice. Instead called it %d times\n", call_count);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
