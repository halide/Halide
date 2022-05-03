#include "AMXReqPerm.h"

#include "IR.h"

namespace Halide {
namespace Internal {
void AMXReqPerm::enable_amx() {
    requires_amx_ = true;
}

Stmt AMXReqPerm::inject_request_amx(Stmt s) {
    if (requires_amx_) {
        return Block::make({Evaluate::make(Call::make(type_of<int>(), "halide_amx_req_perm", {}, Call::Extern)),
                            s,
                            Evaluate::make(Call::make(type_of<int>(), "halide_amx_free_perm", {}, Call::Extern))});
    } else {
        return s;
    }
}
}  // namespace Internal
}  // namespace Halide