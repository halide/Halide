#include "Buffer.h"
#include "IREquality.h"
#include "IROperator.h"
#include "Var.h"

namespace Halide {
namespace Internal {

template<>
RefCount &ref_count<BufferContents>(const BufferContents *c) {
    return c->ref_count;
}

template<>
void destroy<BufferContents>(const BufferContents *c) {
    delete c;
}

Expr buffer_accessor(const Buffer<> &buf, const std::vector<Expr> &args) {
    std::vector<Expr> int_args;
    for (Expr e : args) {
        user_assert(Int(32).can_represent(e.type()))
            << "Args to a call to an Image must be representable as 32-bit integers.\n";
        if (equal(e, _)) {
            // Expand the _ into the appropriate number of implicit vars.
            int missing_dimensions = buf.dimensions() - (int)args.size() + 1;
            for (int i = 0; i < missing_dimensions; i++) {
                int_args.push_back(Var::implicit(i));
            }
        } else if (e.type() == Int(32)) {
            int_args.push_back(e);
        } else {
            int_args.push_back(cast<int>(e));
        }
    }
    Expr c = Call::make(buf, int_args);
    user_assert(int_args.size() == (size_t)buf.dimensions())
        << "Dimensionality mismatch accessing Buffer " << buf.name()
        << ". There were " << int_args.size()
        << " arguments, but the Buffer has " << buf.dimensions() << " dimensions:\n"
        << "  " << c << "\n";
    return c;
}

}  // namespace Internal
}  // namespace Halide
