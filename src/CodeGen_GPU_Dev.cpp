#include "CodeGen_GPU_Dev.h"
#include "Bounds.h"
#include "IRVisitor.h"

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

namespace {
// Check to see if an expression is uniform within a block.
// This is done by checking to see if the expression depends on any GPU
// thread indices.
class IsBlockUniform : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            result = false;
        }
    }

public:
    bool result;

    IsBlockUniform()
        : result(true) {
    }
};
}  // namespace

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

    void visit(const Store *op) override {
        if (op->name == buffer) {
            result = false;
        }
        if (result) {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) override {
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

    IsBufferConstant(const std::string &b)
        : result(true), buffer(b) {
    }
};
}  // namespace

bool CodeGen_GPU_Dev::is_buffer_constant(Stmt kernel,
                                         const std::string &buffer) {
    IsBufferConstant v(buffer);
    kernel.accept(&v);
    return v.result;
}

}  // namespace Internal
}  // namespace Halide
