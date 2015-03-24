#ifndef HALIDE_JAVASCRIPT_EXECUTOR_H
#define HALIDE_JAVASCRIPT_EXECUTOR_H

#include "Parameter.h"
#include "Target.h"
#include "Type.h"


namespace Halide { namespace Internal {

EXPORT int run_javascript(const Target &target, const std::string &source, const std::string &fn_name,
                          const std::vector<std::pair<Argument, const void *> > &args);

}}

#endif
