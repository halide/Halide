#include "Halide.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Halide;

namespace {

int min_f0_load = INT_MAX;
int max_f0_load = INT_MIN;

int trace_f0_loads(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (e->event == halide_trace_load && std::string(e->func).find("f0") == 0) {
        for (int lane = 0; lane < e->lanes; lane++) {
            const int x = e->coordinates[lane];
            min_f0_load = std::min(min_f0_load, x);
            max_f0_load = std::max(max_f0_load, x);
            if (x < -6 || x > 62) {
                fprintf(stderr, "f0 load at %d is outside the live region [-6, 62]\n", x);
                exit(1);
            }
        }
    }
    return 0;
}

void run_case(const std::string &suffix, bool unroll, bool partition_never) {
    Var y{"y"}, yo{"yo"}, yi{"yi"};
    Func f0{"f0" + suffix}, f1{"f1" + suffix}, f2{"f2" + suffix}, f3{"f3" + suffix};

    f0(y) = cast<uint8_t>(y);
    f1(y) = f0(y - 2) + f0(y + 2);
    f2(y) = f1(y * 2);
    f3(y) = f2(y * 2 - 2) + f2(y * 2 + 2);

    f3.bound(y, 0, 15);
    f3.split(y, yo, yi, 4, TailStrategy::PredicateStores);
    f0.compute_root();
    f1.store_root().compute_at(f2, Var::outermost());
    f2.hoist_storage(f3, Var::outermost()).compute_at(f3, yi);

    if (partition_never) {
        f3.partition(yo, Partition::Never).partition(yi, Partition::Never);
    }
    if (unroll) {
        f0.unroll(y);
        f1.unroll(y);
        f2.unroll(y);
        f3.unroll(yo).unroll(yi);
    }

    f0.trace_loads();
    f3.jit_handlers().custom_trace = &trace_f0_loads;

    min_f0_load = INT_MAX;
    max_f0_load = INT_MIN;
    Buffer<uint8_t> out{15};
    out.fill(255);
    f3.realize(out);

    for (int i = 0; i < out.width(); i++) {
        const uint8_t expected = (uint8_t)(16 * i);
        if (out(i) != expected) {
            fprintf(stderr, "%s: out(%d) = %d instead of %d\n",
                    suffix.c_str(), i, out(i), expected);
            exit(1);
        }
    }

    if (min_f0_load == INT_MAX || max_f0_load == INT_MIN) {
        fprintf(stderr, "%s: did not see any f0 loads\n", suffix.c_str());
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    run_case("_unrolled", true, false);
    run_case("_nopartition", false, true);

    printf("Success!\n");
    return 0;
}
