#include "CodeGen_GPU_Dev.h"
#include "Bounds.h"
#include "Deinterleave.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

CodeGen_GPU_Dev::~CodeGen_GPU_Dev() = default;

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
    bool result = true;

    IsBlockUniform() = default;
};
}  // namespace

bool CodeGen_GPU_Dev::is_block_uniform(const Expr &expr) {
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

bool CodeGen_GPU_Dev::is_buffer_constant(const Stmt &kernel,
                                         const std::string &buffer) {
    IsBufferConstant v(buffer);
    kernel.accept(&v);
    return v.result;
}

namespace {

class ScalarizePredicatedLoadStore : public IRMutator {
public:
    using IRMutator::mutate;
    using IRMutator::visit;

protected:
    Stmt visit(const Store *s) override {
        if (!is_const_one(s->predicate)) {
            std::vector<Stmt> scalar_stmts;
            for (int ln = 0; ln < s->value.type().lanes(); ln++) {
                scalar_stmts.push_back(IfThenElse::make(
                    extract_lane(s->predicate, ln),
                    Store::make(s->name,
                                mutate(extract_lane(s->value, ln)),
                                mutate(extract_lane(s->index, ln)),
                                s->param,
                                const_true(),
                                // TODO: alignment needs to be changed
                                s->alignment)));
            }
            return Block::make(scalar_stmts);
        } else {
            return s;
        }
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            Expr load_expr = Load::make(op->type, op->name, op->index, op->image,
                                        op->param, const_true(op->type.lanes()), op->alignment);
            Expr pred_load = Call::make(load_expr.type(),
                                        Call::if_then_else,
                                        {op->predicate, load_expr},
                                        Internal::Call::PureIntrinsic);
            return pred_load;
        } else {
            return op;
        }
    }
};

}  // namespace

Stmt CodeGen_GPU_Dev::scalarize_predicated_loads_stores(Stmt &s) {
    ScalarizePredicatedLoadStore sps;
    return sps.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
