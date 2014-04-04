#include <Halide.h>
#include <stdio.h>
#include "extended_buffer_t_common.h"

using std::vector;

using namespace Halide;

// The extern call that gets the extra_field from a fancy_buffer_t
HalideExtern_1(int, fancy_buffer_t_get_extra_field, fancy_buffer_t *);

class FancyImageParam : public ImageParam {
public:
    FancyImageParam(Type t, int dims) :
        ImageParam(t, dims) {}

    Expr extra_field() {
        // It's possible to get a buffer_t pointer from an ImageParam
        // using a specially-named variable. If these sort of uses become
        // widespread we can add an accessor to ImageParam to get at it.
        Expr buffer_t_pointer = Internal::Variable::make(Handle(), name() + ".buffer", param);

        // Note that this extern call implicitly casts the buffer_t* to a fancy_buffer_t*:
        return fancy_buffer_t_get_extra_field(buffer_t_pointer);
    }
};

int main(int argc, char **argv) {
    FancyImageParam input(Float(32), 2);

    Func output;
    Var x, y;
    output(x, y) = input(x, y) + input.extra_field();

    output.compile_to_file("extended_buffer_t", input);
    return 0;
}
