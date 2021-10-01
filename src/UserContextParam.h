#ifndef HALIDE_USER_CONTEXT_PARAM_H
#define HALIDE_USER_CONTEXT_PARAM_H

#include "Param.h"
#include "Parameter.h"

/** \file
 *
 * Class for passing custom user context to pipeline realization
 */

namespace Halide {

class UserContextParam {
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;
public:
    UserContextParam() : param(type_of<void*>(), false, 0, "__user_context") {}

    explicit UserContextParam(void *user_context) : param(type_of<void*>(), false, 0, "__user_context") {
        param.set_scalar<void*>(user_context);
    }

    const Internal::Parameter &parameter() const {
        return param;
    }

    Internal::Parameter &parameter() {
        return param;
    }
};

}  // namespace Halide

#endif
