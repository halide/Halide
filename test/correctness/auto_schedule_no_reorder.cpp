#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }
    load_plugin(argv[1]);

    return error_test("auto_schedule_no_reorder", "AutoSchedule: cannot auto-schedule function \"f1\" since dim \"v0\" at stage 0 has been reordered", []() {
        Func f, g;
        Var x, y;
        RDom r(2, 18);

        f(x, y) = 1;
        f(r, y) = f(r - 2, y) + f(r - 1, y);

        g(x, y) = f(x + 10, y) + 2;

        // Provide estimates for pipeline output
        g.set_estimates({{0, 50}, {0, 50}});

        // Partially specify some schedules
        g.reorder(y, x);

        // Auto schedule the pipeline
        Target target = get_target_from_environment();
        Pipeline p(g);

        // This should throw an error since auto-scheduler does not currently
        // support partial schedules
        p.apply_autoscheduler(target, {"Mullapudi2016"});
    });
}
