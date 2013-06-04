#include "CodeGen_GPU_Dev.h"

namespace Halide {
namespace Internal {

CodeGen_GPU_Dev::~CodeGen_GPU_Dev() {
}

bool CodeGen_GPU_Dev::is_gpu_var(const std::string &name) {
    std::string n = base_name(name);

    bool result = (n == "threadidx" ||
                   n == "threadidy" ||
                   n == "threadidz" ||
                   n == "threadidw" ||
                   n == "blockidx" ||
                   n == "blockidy" ||
                   n == "blockidz" ||
                   n == "blockidw");

    return result;
}

}}
