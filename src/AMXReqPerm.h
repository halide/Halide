#ifndef HALIDE_AMX_REQ_PERM_H
#define HALIDE_AMX_REQ_PERM_H

#include "Expr.h"

namespace Halide {
namespace Internal {
class AMXReqPerm {
    bool requires_amx_{false};

public:
    AMXReqPerm() = default;

    void enable_amx();

    Stmt inject_request_amx(Stmt s);
};
}  // namespace Internal
}  // namespace Halide

#endif
