#ifndef HALIDE_JAVASCRIPT_EXECUTOR_H
#define HALIDE_JAVASCRIPT_EXECUTOR_H

#include "Parameter.h"
#include "Type.h"

namespace Halide { namespace Internal {

EXPORT int run_javascript(const std::string &source, const std::string &fn_name, std::vector<Parameter> args);

}}

#endif
