#ifndef ERRORS_H
#define ERRORS_H

#include "Halide.h"

#ifndef user_error
#define user_error Halide::Internal::ErrorReport(__FILE__, __LINE__, nullptr, Halide::Internal::ErrorReport::User)
#endif

#ifndef user_warning
#define user_warning Halide::Internal::ErrorReport(__FILE__, __LINE__, nullptr, Halide::Internal::ErrorReport::User | Halide::Internal::ErrorReport::Warning)
#endif

#ifndef user_assert
#define user_assert(c) _halide_internal_assertion(c, Halide::Internal::ErrorReport::User)
#endif

#ifndef internal_assert
#define internal_assert(c) _halide_internal_assertion(c, 0)
#endif

#ifndef internal_error
#define internal_error Halide::Internal::ErrorReport(__FILE__, __LINE__, nullptr, 0)
#endif

#endif
