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
    } else if (const Min *m = e.as<Min>()) {
        Expr la = is_linear(m->a, linear);
        Expr lb = is_linear(m->b, linear);
        if (is_const_zero(la) && is_const_zero(lb)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Max *m = e.as<Max>()) {
        Expr la = is_linear(m->a, linear);
        Expr lb = is_linear(m->b, linear);
        if (is_const_zero(la) && is_const_zero(lb)) {
            return la;
        } else {
            return Expr();
        }
    } else {
        return Expr();
    }
}

namespace {
// The maximum total number of DMA channels allowed.
const int kMaxNumberOfDmaChannels = 4;
// We want to use a separate channel(s) for the output copies, so it can be
// overlapped with input copies and the rest of the processing.
const int kNumberOfChannelsForOutputs = 1;
// Start channel indexing for input copies from this channel.
const int kOffsetOfChannelForInputs = kNumberOfChannelsForOutputs;
// Use remaining channels for input copies.
const int kNumberOfChannelsForInputs = kMaxNumberOfDmaChannels - kNumberOfChannelsForOutputs;
}  // namespace

// Replace indirect loads with dma_transfer intrinsics where
// possible.
class InjectDmaTransferIntoProducer : public IRMutator {
    using IRMutator::visit;

    struct LoopVar {
        std::string name;
        Expr min;
        Expr extent;
        bool body_is_also_loop;
    };

    std::string producer_name;
    std::vector<LoopVar> loop_vars;
    std::set<std::string> loops_to_be_removed;
    std::map<string, Expr> containing_lets;
    // Index of the current DMA channel.
    int index;

    Stmt visit(const For *op) override {
        debug(3) << "InjectDmaTransfer::for " << op->name << "\n";
        // Check if the body is also a loop.
        bool is_body_a_single_for_loop = op->body.as<For>() != nullptr;
        // Maybe a loop, but with lets in front of it.
        if (const LetStmt *let = op->body.as<LetStmt>()) {
            Stmt let_body = let->body;
            while (let_body.node_type() == IRNodeType::LetStmt) {
                let_body = let_body.as<LetStmt>()->body;
            }
            is_body_a_single_for_loop = let_body.as<For>() != nullptr;
        }
        loop_vars.push_back({op->name, op->min, op->extent, is_body_a_single_for_loop});
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
            Expr is_linear_store = is_linear(op_index, local_scope);
            Expr is_linear_value = is_linear(value_index, local_scope);
            debug(3) << "is_linear (stride) store: " << v.name << " " << is_linear_store << "\n";
            debug(3) << "is_linear (stride) load: " << v.name << " " << is_linear_value << "\n";
            store_strides.push_back(is_linear_store);
            value_strides.push_back(is_linear_value);
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
        if ((loop_vars.size() > 1) && store_strides[loop_vars.size() - 2].defined() && value_strides[loop_vars.size() - 2].defined() && loop_vars[loop_vars.size() - 2].body_is_also_loop) {
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
            dma_extents.emplace_back(1);
            store_stride = 1;
            value_stride = 1;
        }

        // Try to simplify the base adresses after substitions.
        store_base = simplify(store_base);
        value_base = simplify(value_base);
        debug(3) << ">>> " << store_base << "\n>>> "
                 << value_base << "\n>>>" << v_inner.extent << "\n";

        Expr copy_call = Call::make(Int(32), "halide_xtensa_copy_2d",
                                    {is_output_dma ?
                                         (index % kNumberOfChannelsForOutputs) :
                                         ((index % kNumberOfChannelsForInputs) + kOffsetOfChannelForInputs),
                                     Variable::make(type_of<void *>(), op->name), store_base, store_stride,
                                     Variable::make(type_of<void *>(), maybe_load->name), value_base, value_stride,
                                     dma_extents[0], dma_extents[1], op->value.type().bytes()},
                                    Call::Intrinsic);

        if (is_output_dma) {
            source_name = maybe_load->name;
        } else {
            source_name = op->name;
        }

        // Store id of DMA transaction, so we can later wait on it.
        Stmt call_result_assert = Store::make(source_name + ".ring_buffer.dma_id",
                                              copy_call, ring_buffer_index,
                                              Parameter(), const_true(),
                                              ModulusRemainder());

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

    Expr ring_buffer_index = 0;
};

class InjectDmaTransfer : public IRMutator {
    using IRMutator::visit;
    const std::map<std::string, Function> &env;
    // Index to track current DMA channel to use.
    int index = 0;
    // Mapping from the function name to the assigned DMA channel.
    std::map<std::string, int> function_name_to_index;
    // Mapping from the allocation name to the loop level index.
    std::map<std::string, int> allocation_to_loop_index;
    // A structure to hold loop information.
    struct Loop {
        string name;
        Expr min;
        Expr extent;

        Loop(const string &name, const Expr &min, const Expr &extent)
            : name(name), min(min), extent(extent) {
        }
    };

    std::vector<Loop> loops;
    std::vector<std::vector<std::pair<string, Expr>>> lets_in_loops;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            auto it = env.find(op->name);
            if (it != env.end()) {
                Function f = it->second;
                if (f.schedule().dma()) {
                    Stmt producer_body = mutate(op->body);
                    // Assign a separate DMA channel for each of the buffers.
                    if (function_name_to_index.find(op->name) == function_name_to_index.end()) {
                        function_name_to_index[op->name] = index;
                        index++;
                    }
                    Stmt body;
                    Expr dma_id_index;
                    auto injector = InjectDmaTransferIntoProducer(op->name, function_name_to_index[op->name]);
                    // If ring_buffer is defined, we can unroll one iteration
                    // to do double-buffering DMA.
                    if (f.schedule().ring_buffer().defined() && ((int)loops.size() > allocation_to_loop_index[op->name])) {
                        user_assert((loops.size() - allocation_to_loop_index[op->name]) == 1)
                            << "There can only be one loop level between compute_at and hoist_storage loop levels for ring_buffer() to work correctly with DMA.";
                        // Find a variable to do double-buffering over.
                        Expr index_var = Variable::make(Int(32), loops.back().name);
                        Expr first_index = loops.back().min;
                        Expr last_index = loops.back().min + loops.back().extent - 1;

                        dma_id_index = index_var % f.schedule().ring_buffer();
                        injector.ring_buffer_index = dma_id_index;
                        producer_body = injector.mutate(producer_body);

                        auto &lets = lets_in_loops.back();
                        // We want to find all Let-s which depend on the loop variable which we use
                        // to double-buffer.
                        Scope<void> dependant_lets_scope;
                        for (const auto &let : lets) {
                            if (expr_uses_var(let.second, loops.back().name) || expr_uses_vars(let.second, dependant_lets_scope)) {
                                debug(3) << "Let " << let.first << " uses var " << loops.back().name << "\n"
                                         << let.second << "\n";
                                dependant_lets_scope.push(let.first);
                            }
                        }

                        Stmt next_producer_body = producer_body;
                        debug(3) << "0: Next producer body: \n"
                                 << next_producer_body << "\n";

                        // Create a copy of all Let's which depend on the loop variable.
                        std::map<string, Expr> replacements;
                        for (int ix = lets.size() - 1; ix >= 0; ix--) {
                            if (dependant_lets_scope.contains(lets[ix].first)) {
                                next_producer_body = LetStmt::make(lets[ix].first + ".next_index", lets[ix].second, next_producer_body);
                                replacements.insert({lets[ix].first, Variable::make(Int(32), lets[ix].first + ".next_index")});
                            }
                        }
                        // Replace all dependant variables by their clones.
                        next_producer_body = substitute(replacements, next_producer_body);
                        debug(3) << "1: Next producer body: \n"
                                 << next_producer_body << "\n";

                        // Advance loop variable by one in this producer body.
                        next_producer_body = substitute(
                            loops.back().name, Variable::make(Int(32), loops.back().name) + 1,
                            next_producer_body);
                        debug(3) << "2: Next producer body: \n"
                                 << next_producer_body << "\n";

                        Expr is_last_iteration = LT::make(index_var, last_index);
                        body = IfThenElse::make(is_last_iteration, next_producer_body);

                        Expr is_first_iteration = EQ::make(index_var, first_index);
                        Stmt first_copy = IfThenElse::make(is_first_iteration, producer_body);
                        body = Block::make(first_copy, body);
                    } else {
                        dma_id_index = 0;
                        body = injector.mutate(producer_body);
                    }

                    if (!injector.is_output_dma) {
                        // Add a wait in the *end* of the producer node for the
                        // case when there any outstanding DMA transactions.
                        Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy_with_id",
                                                      {(function_name_to_index[op->name] % kNumberOfChannelsForInputs) + kOffsetOfChannelForInputs,
                                                       Load::make(Int(32), op->name + ".ring_buffer.dma_id", dma_id_index, Buffer<>(), Parameter(), const_true(), ModulusRemainder())},
                                                      Call::Intrinsic);
                        Stmt wait_is_done = Evaluate::make(wait_result);
                        body = Block::make(body, wait_is_done);
                    } else {
                        // For the output nodes collect all of the corresponding
                        // producers, so we can add required waits in a separate
                        // pass later.
                        DelayedWaitInfo info(function_name_to_index[op->name] % kNumberOfChannelsForOutputs,
                                             dma_id_index, f.schedule().ring_buffer());
                        producers_to_wait.insert({injector.source_name, info});
                    }
                    return ProducerConsumer::make_produce(op->name, body);
                }
            }
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Allocate *op) override {
        allocation_to_loop_index[op->name] = loops.size();
        Stmt mutated = IRMutator::visit(op);

        auto it = env.find(op->name);
        if (it != env.end()) {
            Function f = it->second;
            // Allocate memory for DMA transaction ID(s).
            if (f.schedule().dma()) {
                std::vector<Expr> extents;
                if (f.schedule().ring_buffer().defined()) {
                    extents.push_back(f.schedule().ring_buffer());
                }
                mutated = Allocate::make(op->name + ".ring_buffer.dma_id", Int(32), MemoryType::Stack, extents, const_true(), mutated);
            }
        }

        allocation_to_loop_index.erase(op->name);
        return mutated;
    }

    Stmt visit(const LetStmt *op) override {
        if (!lets_in_loops.empty()) {
            lets_in_loops.back().emplace_back(op->name, op->value);
            Stmt mutated = IRMutator::visit(op);
            lets_in_loops.back().pop_back();
            return mutated;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const For *op) override {
        lets_in_loops.emplace_back();
        loops.emplace_back(op->name, op->min, op->extent);
        Stmt mutated = IRMutator::visit(op);
        loops.pop_back();
        lets_in_loops.pop_back();
        return mutated;
    }

public:
    InjectDmaTransfer(const std::map<std::string, Function> &e)
        : env(e) {
    }

    struct DelayedWaitInfo {
        int channel_index;
        Expr dma_id_index;
        Expr ring_buffer_extent;

        DelayedWaitInfo(int channel_index, const Expr &dma_id_index, const Expr &ring_buffer_extent)
            : channel_index(channel_index),
              dma_id_index(dma_id_index),
              ring_buffer_extent(ring_buffer_extent) {
        }
    };

    std::map<string, DelayedWaitInfo> producers_to_wait;
};

class InjectWaitsInProducers : public IRMutator {
    using IRMutator::visit;
    const std::map<string, InjectDmaTransfer::DelayedWaitInfo> &producers_to_wait;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            auto it = producers_to_wait.find(op->name);
            if (it != producers_to_wait.end()) {
                // Add a wait in the *beginning* of the producer node to make
                // sure that everything is copied before starting production of
                // the new lines.
                Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy_with_id",
                                              {it->second.channel_index,
                                               Load::make(Int(32), op->name + ".ring_buffer.dma_id", it->second.dma_id_index, Buffer<>(), Parameter(), const_true(), ModulusRemainder())},
                                              Call::Intrinsic);

                Stmt wait_is_done = Evaluate::make(wait_result);
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
            Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy", {it->second.channel_index}, Call::Intrinsic);
            Stmt wait_is_done = Evaluate::make(wait_result);
            Stmt body = mutate(op->body);
            body = Block::make(body, wait_is_done);

            body = Allocate::make(op->name, op->type, op->memory_type,
                                  op->extents, op->condition, body,
                                  op->new_expr, op->free_function);

            std::vector<Expr> extents;
            if (it->second.ring_buffer_extent.defined()) {
                extents.push_back(it->second.ring_buffer_extent);
            }
            body = Allocate::make(op->name + ".ring_buffer.dma_id", Int(32),
                                  MemoryType::Stack, extents, const_true(), body);

            return body;
        }

        return IRMutator::visit(op);
    }

public:
    InjectWaitsInProducers(const std::map<string, InjectDmaTransfer::DelayedWaitInfo> &pr)
        : producers_to_wait(pr) {
    }
};

Stmt inject_dma_transfer(Stmt s, const std::map<std::string, Function> &env) {
    auto inject_dma = InjectDmaTransfer(env);
    s = inject_dma.mutate(s);
    s = InjectWaitsInProducers(inject_dma.producers_to_wait).mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
