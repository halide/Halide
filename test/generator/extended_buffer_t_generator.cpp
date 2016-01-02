#include "Halide.h"
#include "extended_buffer_t_common.h"

using namespace Halide;

namespace {

// The extern call that gets the extra_field from a fancy_buffer_t
HalideExtern_1(int, fancy_buffer_t_get_extra_field, fancy_buffer_t *);

class FancyImageParam : public ImageParam {
public:
    FancyImageParam(Type t, int dims, const std::string &n) : ImageParam(t, dims, n) {}

    Expr extra_field() const {
        // It's possible to get a buffer_t pointer from an ImageParam
        // using a specially-named variable. If these sort of uses become
        // widespread we can add an accessor to ImageParam to get at it.
        Expr buffer_t_pointer = Internal::Variable::make(Handle(), name() + ".buffer", param);

        // Note that this extern call implicitly casts the buffer_t* to a fancy_buffer_t*:
        return fancy_buffer_t_get_extra_field(buffer_t_pointer);
    }
};

class ExtendedBufferT : public Generator<ExtendedBufferT> {
public:
    FancyImageParam input{ Float(32), 2, "input" };

    Func build() {
        Var x, y;
        Func output;
        output(x, y) = input(x, y) + input.extra_field();
        return output;
    }
};

RegisterGenerator<ExtendedBufferT> register_my_gen{"extended_buffer_t"};

}  // namespace
