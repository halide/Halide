#include "Buffer.h"
#include "Var.h"
#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

template<>
EXPORT RefCount &ref_count<BufferContents>(const BufferContents *c) {
    return c->ref_count;
}

template<>
EXPORT void destroy<BufferContents>(const BufferContents *c) {
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
    return Call::make(buf, int_args);
}

}
}
