#include "Func_gpu.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

namespace h = Halide;
namespace p = boost::python;

void defineFuncGpuMethods(p::class_<h::Func> &func_class) {
    using namespace func_and_stage_implementation_details;

    // defineFuncOrStageGpuMethods is defined in the header file
    defineFuncOrStageGpuMethods<h::Func>(func_class);
    return;
}
