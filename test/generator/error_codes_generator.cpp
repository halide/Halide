#include <assert.h>
#include <stdint.h>
#include <memory>
#include <type_traits>
#include <vector>

#include "Halide.h"

namespace halide_register_generator {
struct halide_global_ns;
}  // namespace halide_register_generator

namespace {

class ErrorCodes : public Halide::Generator<ErrorCodes> {
public:
    Input<Buffer<int32_t>> input{"input", 2};
    Input<int> f_explicit_bound{"f_explicit_bound", 1, 0, 64};

    Output<Buffer<int32_t>> output{"output", 2};

    void generate() {
        assert(!get_target().has_feature(Target::LargeBuffers));
        Var x, y;

        output(x, y) = input(x, y);
        output.bound(x, 0, f_explicit_bound);

        add_requirement(input.dim(1).extent() == 123);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ErrorCodes, error_codes)
