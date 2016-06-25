#include "Halide.h"
#include "benchmark.h"
#include "Schedule.h"

using namespace Halide;
using namespace Halide::Internal;

using std::vector;

void run_test(bool auto_schedule) {
    Var x("x"), y("y"), dx("dx"), dy("dy"), c("c");

    Func f("f");
    f(x, y, dx, dy) = x + y + dx + dy;

    int search_area = 7;
    RDom dom(-search_area/2, search_area, -search_area/2, search_area, "dom");

    Func r("r");
    r(x, y, c) += f(x, y+1, dom.x, dom.y) * f(x, y-1, dom.x, dom.y) * c;

    r.estimate(x, 0, 1024).estimate(y, 0, 1024).estimate(c, 0, 3);

    Target target = get_target_from_environment();
    Pipeline p(r);

    if (auto_schedule) {
        p.auto_schedule(target);

        // Inspect schedule for r's update definition the reduction
        // variables must be reordered and should not be inner most
        Stage r_update = r.update(0);
        const vector<Dim> &u_dims = r_update.get_schedule().dims();
        // TODO: Find a better way to make the comparison. Currently
        // relying on internal names.
        assert(u_dims[0].var != "dom$x" && u_dims[0].var != "dom$y");
        assert(u_dims[1].var != "dom$x" && u_dims[1].var != "dom$y");
    }

    r.print_loop_nest();
}

int main(int argc, char **argv) {
    run_test(true);
    return 0;
}
