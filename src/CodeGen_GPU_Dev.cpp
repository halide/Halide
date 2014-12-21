#include "CodeGen_GPU_Dev.h"
#include "CodeGen_PTX_Dev.h"
#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_OpenGL_Dev.h"

namespace Halide {
namespace Internal {

CodeGen_GPU_Dev::~CodeGen_GPU_Dev() {
}

CodeGen_GPU_Dev* CodeGen_GPU_Dev::new_for_target(Target t)
{
    if (t.has_feature(Target::CUDA)) {
        debug(1) << "Constructing CUDA device codegen\n";
        return new CodeGen_PTX_Dev(t);
    } else if (t.has_feature(Target::OpenCL)) {
        debug(1) << "Constructing OpenCL device codegen\n";
        return new CodeGen_OpenCL_Dev(t);
    } else if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Constructing OpenGL device codegen\n";
        return new CodeGen_OpenGL_Dev(t);
    } else {
        internal_error << "Requested unknown GPU target: " << t.to_string() << "\n";
        return NULL;
    }
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

namespace {
// Check to see if an expression is uniform within a block.
// This is done by checking to see if the expression depends on any GPU
// thread indices.
class IsBlockUniform : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            result = false;
        }
    }

public:
    bool result;

    IsBlockUniform() : result(true) {
    }
};
}

bool CodeGen_GPU_Dev::is_block_uniform(Expr expr) {
    IsBlockUniform v;
    expr.accept(&v);
    return v.result;
}

namespace {
// Check to see if a buffer is a candidate for constant memory storage.
// A buffer is a candidate for constant memory if it is never written to,
// and loads are uniform within the workgroup.
class IsBufferConstant : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Store *op) {
        if (op->name == buffer) {
            result = false;
        }
        if (result) {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) {
        if (op->name == buffer &&
            !CodeGen_GPU_Dev::is_block_uniform(op->index)) {
            result = false;
        }
        if (result) {
            IRVisitor::visit(op);
        }
    }

public:
    bool result;
    const std::string &buffer;

    IsBufferConstant(const std::string &b) : result(true), buffer(b) {
    }
};
}

bool CodeGen_GPU_Dev::is_buffer_constant(Stmt kernel,
                                         const std::string &buffer) {
    IsBufferConstant v(buffer);
    kernel.accept(&v);
    return v.result;
}

}}
