#include "Halide.h"

namespace {

// Each sub-target of the multitarget library writes a marker saying which one
// it is, so the test can tell which one was dispatched to.
class ArmI8MMMultitarget : public Halide::Generator<ArmI8MMMultitarget> {
public:
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        Var x;
        int marker = 0;
        if (get_target().has_feature(Target::ARMv86a)) {
            marker = 86;
        } else if (get_target().has_feature(Target::ARMI8MM)) {
            marker = 8;
        }
        output(x) = marker;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ArmI8MMMultitarget, arm_i8mm_multitarget)
