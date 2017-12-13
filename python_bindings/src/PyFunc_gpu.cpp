#include "PyFunc_gpu.h"

#include <boost/python.hpp>

#include "Halide.h"

namespace h = Halide;
namespace p = boost::python;

void define_func_gpu_methods(p::class_<h::Func> &func_class) {
    using namespace func_and_stage_implementation_details;

    define_func_or_stage_gpu_methods<h::Func>(func_class);
}
