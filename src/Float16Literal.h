#ifndef HALIDE_FLOAT16_LITERAL_H
#define HALIDE_FLOAT16_LITERAL_H
// FIXME: Halide's header file hack that creates a difference between internal
// and external (public) headers prevents us from doing the include below
// #include "Float16.h"
//
// This means clients of this header file will need to have included the right
// header file before including this one. Internally Float16.h or a header that
// includes it (Expr.h) can be used and externally Halide.h must be used

// User defined literal for float16_t This is not in Float16.h because the
// operator is at the global scope so we don't want to pollute the user's
// namespace unless they really want this feature.
Halide::float16_t operator"" _fp16(const char *);

#endif
