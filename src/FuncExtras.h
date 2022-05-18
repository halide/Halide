#ifndef HALIDE_FUNC_EXTRAS_H
#define HALIDE_FUNC_EXTRAS_H

#include "Func.h"
#include "Lambda.h"

namespace Halide {

namespace Internal {

inline const Func &func_like_to_func(const Func &func) {
    return func;
}

template<typename T>
inline HALIDE_NO_USER_CODE_INLINE Func func_like_to_func(const T &func_like) {
    return lambda(_, func_like(_));
}

}  // namespace Internal

}  // namespace Halide

#endif
