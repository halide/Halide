#ifndef ERRORS_H
#define ERRORS_H

#include "Halide.h"

#define internal_error Halide::Internal::ErrorReport<Halide::InternalError>(__FILE__, __LINE__, nullptr)
#define user_error Halide::Internal::ErrorReport<Halide::CompileError>(__FILE__, __LINE__, nullptr)
#define user_warning Halide::Internal::WarningReport(__FILE__, __LINE__, nullptr)

#define user_assert(c) _halide_internal_assertion(c, Halide::CompileError)
#define internal_assert(c) _halide_internal_assertion(c, Halide::InternalError)

#endif
