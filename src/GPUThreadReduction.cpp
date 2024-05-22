#include "GPUThreadReduction.h"

#include "CodeGen_GPU_Dev.h"
#include "DeviceAPI.h"
#include "Error.h"
#include "Expr.h"
#include "IR.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {

bool has_thread_reduction_inner(const For *op) {
    struct DetectInnerReduction : public IRMutator {
        using IRMutator::visit;
        bool has_inner_reduction = false;

        Stmt visit(const For *op) override {
            if (op->for_type == ForType::GPUThreadReduction) {
                has_inner_reduction = true;
            }
            return IRMutator::visit(op);
        }
    };

    DetectInnerReduction dir({});
    dir.mutate(op);

    return dir.has_inner_reduction;
}

class GPUThreadReduction : public IRMutator {
private:
    using IRMutator::visit;

    bool inner_reduction = false;
    Expr block_var, block_min, block_extend;
    Expr thread_var, thread_extend;
    Expr log_step_var;
    std::string reduce_provider_name, input_name;
    std::string intermediate_buffer_name;

    Stmt visit(const For *op) override {
        if (op->for_type != ForType::GPUThreadReduction) {
            if (op->for_type == ForType::GPUBlock) {
                block_var = Variable::make(Int(32), op->name);
                block_min = op->min;
                block_extend = op->extent;

                if (has_thread_reduction_inner(op)) {
                    Stmt stmt = mutate(op->body);

                    // allocate intermediate buffer
                    stmt = Allocate::make(intermediate_buffer_name, Int(32), MemoryType::GPUShared, {thread_extend},
                                          const_true(), stmt);

                    // HACK(xylonx): hack way to specify min, stride and extend
                    stmt = LetStmt::make(intermediate_buffer_name + ".stride.0", 1, stmt);
                    stmt = LetStmt::make(intermediate_buffer_name + ".extend.0", op->extent, stmt);
                    stmt = LetStmt::make(intermediate_buffer_name + ".min.0", 0, stmt);

                    return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api,
                                     stmt);
                }
                return IRMutator::visit(op);
            }

            if (op->for_type == ForType::GPUThread) {
                thread_var = Variable::make(Int(32), op->name);
                thread_extend = op->extent;
            }

            return IRMutator::visit(op);
        }

        inner_reduction = true;

        log_step_var = Variable::make(Int(32), op->name);

        // TODO(xylonx): check the log base
        // const Expr log_extend = Cast::make(Int(32), log(op->extent));
        // const Expr exp2_step = pow(2, log_step_var);

        const IntImm *extent = op->extent.as<IntImm>();
        user_assert(extent != nullptr) << "For with non-integer extent\n";

        Stmt gpu_sync =
            Evaluate::make(Call::make(Int(32), Call::gpu_thread_barrier, {IntImm::make(Int(32), CodeGen_GPU_Dev::MemoryFenceType::Shared)}, Call::Intrinsic));

        const Stmt body = mutate(op->body);

        Stmt stmt = body;
        stmt = IfThenElse::make((thread_var % (2 * pow(2, log_step_var))) == 0, stmt);
        stmt = Block::make({
            // assign values from
            stmt,
            gpu_sync,
        });
        stmt = For::make(op->name, 0, IntImm::make(Int(32), (long long)log2(extent->value)), ForType::Serial,
                         op->partition_policy, op->device_api, stmt);

        stmt = Block::make({
            Provide::make(intermediate_buffer_name, {Call::make(Int(32), input_name, {block_var * thread_extend + thread_var}, Call::Halide)},
                          {thread_var}, const_true()),
            gpu_sync,

            stmt,

            IfThenElse::make(thread_var == 0,
                             Provide::make(reduce_provider_name, {Call::make(Int(32), intermediate_buffer_name, {}, Call::CallType::Halide)}, {block_var}, const_true())),

        });

        return stmt;
    }

    Stmt visit(const Provide *op) override {
        if (!inner_reduction) {
            return IRMutator::visit(op);
        }

        reduce_provider_name = op->name;
        intermediate_buffer_name = op->name + "_intermediate";

        // original value
        const std::vector<Expr> old_values = op->values;

        std::vector<Expr> values;
        values.reserve(old_values.size());
        for (const auto &val : old_values) {
            values.emplace_back(mutate(val));
        }

        return Provide::make(intermediate_buffer_name, values, {thread_var}, op->predicate);
    }

    Expr visit(const Call *op) override {
        if (!inner_reduction) {
            return IRMutator::visit(op);
        }

        if (op->name != reduce_provider_name) {
            input_name = op->name;

            std::vector<Expr> args = {};
            args.reserve(op->args.size());
            for (const auto &arg : op->args) {
                // if arg contains thread_idx
                args.emplace_back(Cast::make(Int(32), thread_var + pow(2, log_step_var)));
            }

            return Call::make(op->type, intermediate_buffer_name, args, op->call_type, op->func, op->value_index, op->image, op->param);
        }

        return Call::make(op->type, intermediate_buffer_name, {thread_var}, op->call_type, op->func, op->value_index,
                          op->image, op->param);
    }
};

}  // namespace

Stmt gpu_thread_reduction(Stmt s) {
    s = GPUThreadReduction().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide