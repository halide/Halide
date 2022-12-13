#include "InjectDmaTransfer.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "Function.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;

/** If an integer expression varies linearly with the variables in the
 * scope, return the linear term. Otherwise return an undefined
 * Expr. */
Expr is_linear(const Expr &e, const Scope<Expr> &linear) {
    if (e.type() != Int(32)) {
        return Expr();
    }
    if (const Variable *v = e.as<Variable>()) {
        if (linear.contains(v->name)) {
            return linear.get(v->name);
        } else {
            return make_zero(v->type);
        }
    } else if (const IntImm *op = e.as<IntImm>()) {
        return make_zero(op->type);
    } else if (const Add *add = e.as<Add>()) {
        Expr la = is_linear(add->a, linear);
        Expr lb = is_linear(add->b, linear);
        if (is_const_zero(lb)) {
            return la;
        } else if (is_const_zero(la)) {
            return lb;
        } else if (la.defined() && lb.defined()) {
            return la + lb;
        } else {
            return Expr();
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        Expr la = is_linear(sub->a, linear);
        Expr lb = is_linear(sub->b, linear);
        if (is_const_zero(lb)) {
            return la;
        } else if (la.defined() && lb.defined()) {
            return la - lb;
        } else {
            return Expr();
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        Expr la = is_linear(mul->a, linear);
        Expr lb = is_linear(mul->b, linear);
        if (is_const_zero(la) && is_const_zero(lb)) {
            return la;
        } else if (is_const_zero(la) && lb.defined()) {
            return mul->a * lb;
        } else if (la.defined() && is_const_zero(lb)) {
            return la * mul->b;
        } else {
            return Expr();
        }
    } else if (const Div *div = e.as<Div>()) {
        Expr la = is_linear(div->a, linear);
        if (is_const_zero(la)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Mod *mod = e.as<Mod>()) {
        Expr la = is_linear(mod->a, linear);
        if (is_const_zero(la)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Ramp *r = e.as<Ramp>()) {
        Expr la = is_linear(r->base, linear);
        Expr lb = is_linear(r->stride, linear);
        if (is_const_zero(lb)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return is_linear(b->value, linear);
    } else {
        return Expr();
    }
}

// Replace indirect loads with dma_transfer intrinsics where
// possible.
class InjectDmaTransferIntoProducer : public IRMutator {
    using IRMutator::visit;

    struct LoopVar {
        std::string name;
        Expr min;
        Expr extent;
    };

    std::string producer_name;
    std::vector<LoopVar> loop_vars;
    std::set<std::string> loops_to_be_removed;
    std::map<string, Expr> containing_lets;
    // Index of the current DMA channel.
    int index;

    Stmt visit(const For *op) override {
        debug(3) << "InjectDmaTransfer::for " << op->name << "\n";
        loop_vars.push_back({op->name, op->min, op->extent});
        Stmt mutated = IRMutator::visit(op);
        loop_vars.pop_back();
        if (loops_to_be_removed.count(op->name) > 0) {
            loops_to_be_removed.erase(op->name);
            return mutated.as<For>()->body;
        }
        return mutated;
    }

    Stmt visit(const LetStmt *op) override {
        // TODO: Not really correct, but probably want to skip lets which
        // don't depend on loop vars.
        if (loop_vars.empty()) {
            return IRMutator::visit(op);
        }
        containing_lets[op->name] = op->value;

        Stmt stmt;
        Stmt body = mutate(op->body);
        if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, op->value, body);
        }

        containing_lets.erase(op->name);
        return stmt;
    }

    Stmt visit(const Store *op) override {
        if (op->name != producer_name) {
            return IRMutator::visit(op);
        }

        // Check if the destination is an output buffer in which case we can
        // do a wait for completion later.
        is_output_dma = op->param.defined();
        debug(3) << "InjectDmaTransfer::store " << op->name << "\n";
        debug(3) << loop_vars.size() << "\n";

        const Load *maybe_load = op->value.as<Load>();
        if (const Call *maybe_call = op->value.as<Call>()) {
            if (maybe_call->is_intrinsic(Call::IntrinsicOp::strict_float)) {
                maybe_load = maybe_call->args[0].as<Load>();
            }
        }
        // Has to be a direct load-to-store for now.
        user_assert(maybe_load) << "Only direct load-to-stores are supported in dma()";

        debug(3) << "InjectDmaTransfer::" << op->name << " " << maybe_load->name << "\n";
        debug(3) << op->index << "\n";
        debug(3) << maybe_load->index << "\n";

        // Substitute in lets into indices of load and store to simplify a further
        // analysis.
        Expr op_index = op->index;
        op_index = substitute_in_all_lets(op_index);
        op_index = substitute(containing_lets, op_index);

        Expr value_index = maybe_load->index;
        value_index = substitute_in_all_lets(value_index);
        value_index = substitute(containing_lets, value_index);

        // A vector to hold DMA extents.
        std::vector<Expr> dma_extents;

        vector<Expr> store_strides;
        vector<Expr> value_strides;

        // Compute strides for each of the loop vars.
        for (const auto &v : loop_vars) {
            Scope<Expr> local_scope;
            local_scope.push(v.name, 1);
            debug(3) << "is_linear (stride) store: " << v.name << " " << is_linear(op_index, local_scope) << "\n";
            debug(3) << "is_linear (stride) load: " << v.name << " " << is_linear(value_index, local_scope) << "\n";
            store_strides.push_back(is_linear(op_index, local_scope));
            value_strides.push_back(is_linear(value_index, local_scope));
        }

        // Use innermost loop var first.
        const auto &v_inner = loop_vars.back();
        Expr var = Variable::make(op->index.type(), v_inner.name);
        // Use extent of the loop as one of the extents of DMA transactions.
        dma_extents.push_back(v_inner.extent);
        // This loop was replaced by DMA transfer, so remove the loop itself.
        loops_to_be_removed.insert(v_inner.name);
        // Substitute the min into the store/load base address.
        Expr store_base = substitute(var, v_inner.min, op_index);
        Expr value_base = substitute(var, v_inner.min, value_index);

        Expr store_stride;
        Expr value_stride;
        // Hardware supports 2D transactions, so try to see if we can replace
        // the next loop var. We only can do it if there are at least two loops
        // and we were able to find the strides for corresponding loop var.
        if ((loop_vars.size() > 1) && store_strides[loop_vars.size() - 2].defined() && value_strides[loop_vars.size() - 2].defined()) {
            const auto &v_outer = loop_vars[loop_vars.size() - 2];
            Expr var_outer = Variable::make(op->index.type(), v_outer.name);
            // Remove the second loop as well.
            loops_to_be_removed.insert(v_outer.name);

            // Substitute another min.
            store_base = substitute(var_outer, v_outer.min, store_base);
            value_base = substitute(var_outer, v_outer.min, value_base);

            dma_extents.push_back(v_outer.extent);

            // Use the strides we computed before.
            store_stride = store_strides[loop_vars.size() - 2];
            value_stride = value_strides[loop_vars.size() - 2];
        } else {
            // If we couldn't compute the strides, we still will do a 2D
            // transaction, but set one of the extents to 1. This simplifies
            // runtime a lot.
            dma_extents.push_back(1);
            store_stride = 1;
            value_stride = 1;
        }

        // Try to simplify the base adresses after substitions.
        store_base = simplify(store_base);
        value_base = simplify(value_base);
        debug(3) << ">>> " << store_base << "\n>>> "
                 << value_base << "\n>>>" << v_inner.extent << "\n";

        Expr copy_call = Call::make(Int(32), "halide_xtensa_copy_2d",
                                    {index,
                                     Variable::make(type_of<void *>(), op->name), store_base, store_stride,
                                     Variable::make(type_of<void *>(), maybe_load->name), value_base, value_stride,
                                     dma_extents[0], dma_extents[1], op->value.type().bytes()},
                                    Call::Intrinsic);

        if (is_output_dma) {
            source_name = maybe_load->name;
        }

        Stmt call_result_assert = AssertStmt::make(copy_call > 0, -1);

        return call_result_assert;
    }

public:
    InjectDmaTransferIntoProducer(const string &pn, int i)
        : producer_name(pn), index(i) {
    }

    // Are we writing to the output buffer?
    bool is_output_dma = false;
    // If yes store the name of the source.
    std::string source_name;
};

class InjectDmaTransfer : public IRMutator {
    using IRMutator::visit;
    const std::map<std::string, Function> &env;
    // Index to track current DMA channel to use.
    int index = 0;
    // Mapping from the function name to the assigned DMA channel.
    std::map<std::string, int> function_name_to_index;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            auto it = env.find(op->name);
            if (it != env.end()) {
                Function f = it->second;
                if (f.schedule().dma()) {
                    Stmt body = mutate(op->body);
                    // Assign a separate DMA channel for each of the buffers.
                    if (function_name_to_index.find(op->name) == function_name_to_index.end()) {
                        function_name_to_index[op->name] = index;
                        index++;
                    }
                    auto injector = InjectDmaTransferIntoProducer(op->name, function_name_to_index[op->name]);
                    body = injector.mutate(body);
                    if (!injector.is_output_dma) {
                        // Add a wait in the *end* of the producer node for the
                        // case when there any outstanding DMA transactions.
                        Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy",
                                                      {function_name_to_index[op->name]}, Call::Intrinsic);
                        Stmt wait_is_done = AssertStmt::make(wait_result == 0, -1);
                        body = Block::make(body, wait_is_done);
                    } else {
                        // For the output nodes collect all of the corresponding
                        // producers, so we can add required waits in a separate
                        // pass later.
                        producers_to_wait[injector.source_name] = function_name_to_index[op->name];
                    }
                    return ProducerConsumer::make_produce(op->name, body);
                }
            }
        }
        return IRMutator::visit(op);
    }

public:
    InjectDmaTransfer(const std::map<std::string, Function> &e)
        : env(e) {
    }

    std::map<std::string, int> producers_to_wait;
};

class InjectWaitsInProducers : public IRMutator {
    using IRMutator::visit;
    const std::map<std::string, int> &producers_to_wait;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            auto it = producers_to_wait.find(op->name);
            if (it != producers_to_wait.end()) {
                // Add a wait in the *beginning* of the producer node to make
                // sure that everything is copied before starting production of
                // the new lines.
                Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy", {it->second}, Call::Intrinsic);
                Stmt wait_is_done = AssertStmt::make(wait_result == 0, -1);
                Stmt body = mutate(op->body);
                body = Block::make(wait_is_done, body);

                return ProducerConsumer::make_produce(op->name, body);
            }
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Allocate *op) override {
        auto it = producers_to_wait.find(op->name);
        if (it != producers_to_wait.end()) {
            // Add a wait in the end of the allocate node to make sure that
            // everything is copied before de-allocation.
            Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy", {it->second}, Call::Intrinsic);
            Stmt wait_is_done = AssertStmt::make(wait_result == 0, -1);
            Stmt body = mutate(op->body);
            body = Block::make(body, wait_is_done);

            return Allocate::make(op->name, op->type, op->memory_type,
                                  op->extents, op->condition, body,
                                  op->new_expr, op->free_function);
        }

        return IRMutator::visit(op);
    }

public:
    InjectWaitsInProducers(const std::map<std::string, int> &pr)
        : producers_to_wait(pr){}

          ;
};

Stmt inject_dma_transfer(Stmt s, const std::map<std::string, Function> &env) {
    auto inject_dma = InjectDmaTransfer(env);
    s = inject_dma.mutate(s);
    s = InjectWaitsInProducers(inject_dma.producers_to_wait).mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
