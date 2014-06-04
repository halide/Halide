#include "CodeGen_GPU_Dev.h"

namespace Halide {
namespace Internal {

CodeGen_GPU_Dev::~CodeGen_GPU_Dev() {
}

bool CodeGen_GPU_Dev::is_gpu_var(const std::string &name) {
    return is_gpu_block_var(name) || is_gpu_thread_var(name);
}

bool CodeGen_GPU_Dev::is_gpu_block_var(const std::string &name) {
    return (ends_with(name, ".__block_id_x") ||
            ends_with(name, ".__block_id_y") ||
            ends_with(name, ".__block_id_z") ||
            ends_with(name, ".__block_id_w"));
}

bool CodeGen_GPU_Dev::is_gpu_thread_var(const std::string &name) {
    return (ends_with(name, ".__thread_id_x") ||
            ends_with(name, ".__thread_id_y") ||
            ends_with(name, ".__thread_id_z") ||
            ends_with(name, ".__thread_id_w"));
}

}}
