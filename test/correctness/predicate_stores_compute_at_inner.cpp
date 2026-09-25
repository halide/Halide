#include "Halide.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Halide;

namespace {

bool f0_live = false;
int f0_min = 0, f0_max = -1;
int f0_loads = 0;

// Every load of f0 must fall inside f0's live realization.
int trace_f0(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (std::string(e->func).find("f0") != 0) {
        return 0;
    }
    if (e->event == halide_trace_begin_realization) {
        f0_live = true;
        f0_min = e->coordinates[0];
        f0_max = e->coordinates[0] + e->coordinates[1] - 1;
    } else if (e->event == halide_trace_end_realization) {
        f0_live = false;
    } else if (e->event == halide_trace_load) {
        for (int lane = 0; lane < e->lanes; lane++) {
            const int x = e->coordinates[lane];
            f0_loads++;
            if (!f0_live || x < f0_min || x > f0_max) {
                fprintf(stderr, "f0 load at %d is outside its realization [%d, %d]\n",
                        x, f0_min, f0_max);
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

    f0.trace_loads().trace_realizations();
    f3.jit_handlers().custom_trace = &trace_f0;

    f0_loads = 0;
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

    if (f0_loads == 0) {
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
