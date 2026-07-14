#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func hist("hist");
    Var x;
    ImageParam indices(Int(32), 1, "indices");
    RDom r(0, 100);

    hist(x) = 0;
    hist(indices(r)) += 1;

    // The scatter above means Halide can't prove r's iterations don't
    // collide, so stream_stores() is illegal on this update.
    hist.update(0).stream_stores();

    printf("Success!\n");
    return 0;
}
