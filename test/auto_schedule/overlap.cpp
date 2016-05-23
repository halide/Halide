#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    ImageParam input(UInt(16), 2);
    Var x("x"), y("y"), xi("xi"), yi("yi");

    int num_levels = 10;

    std::vector<Func> down;
    for(int i = 0; i < num_levels; i ++) {
        Func d("down_" + std::to_string(i));
        down.push_back(d);
    }

    std::vector<Func> up;
    for(int i = 0; i < num_levels; i ++) {
        Func u("up_" + std::to_string(i));
        up.push_back(u);
    }

    down[0](x, y) = input(x, y);
    for (int i = 1; i < num_levels; i++) {
        down[i](x, y) = (down[i-1](2*x, y) + down[i-1](2*x + 1, y))/2;
    }

    up[0](x, y) = down[num_levels - 1](x, y);
    for (int i = 1; i < num_levels; i++) {
        up[i](x, y) = (up[i-1](x/2, y) + up[i-1]((x + 1)/2, y))/2;
    }

    // Provide esitmates for pipeline outputs
    //up[num_levels - 1].bound(x, 0, 4000).bound(y, 0, 2000);

    // Auto schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(up[num_levels - 1]);

    p.auto_schedule(target);

    // Inspect the schedule
    printf("Success!\n");

    return 0;
}
