#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = u8_sat(f16(x) / f16(2.5f));

    // Make sure target has no float16 native support
    Target t = get_host_target();
    for (auto &feature : {Target::F16C, Target::ARMFp16}) {
        t = t.without_feature(feature);
    }

    f.compile_to_llvm_assembly("/dev/null", f.infer_arguments(), "f", t);

    return 0;
}
