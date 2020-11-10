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
        if (is_zero(lb)) {
            return la;
        } else if (is_zero(la)) {
            return lb;
        } else if (la.defined() && lb.defined()) {
            return la + lb;
        } else {
            return Expr();
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        Expr la = is_linear(sub->a, linear);
        Expr lb = is_linear(sub->b, linear);
        if (is_zero(lb)) {
            return la;
        } else if (la.defined() && lb.defined()) {
            return la - lb;
        } else {
            return Expr();
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        Expr la = is_linear(mul->a, linear);
        Expr lb = is_linear(mul->b, linear);
        if (is_zero(la) && is_zero(lb)) {
            return la;
        } else if (is_zero(la) && lb.defined()) {
            return mul->a * lb;
        } else if (la.defined() && is_zero(lb)) {
            return la * mul->b;
        } else {
            return Expr();
        }
    } else if (const Div *div = e.as<Div>()) {
        Expr la = is_linear(div->a, linear);
        if (is_zero(la)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Mod *mod = e.as<Mod>()) {
        Expr la = is_linear(mod->a, linear);
        if (is_zero(la)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Ramp *r = e.as<Ramp>()) {
        Expr la = is_linear(r->base, linear);
        Expr lb = is_linear(r->stride, linear);
        if (is_zero(lb)) {
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

    Stmt visit(const For *op) override {
      debug(0) << "InjectDmaTransfer::for " << op->name << "\n";
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
        debug(0) << "InjectDmaTransfer::store " << op->name << "\n";
        debug(0) << loop_vars.size() << "\n";
        // Only 1D, 2D and 3D DMA transfers are supported
        // user_assert(!loop_vars.empty() && loop_vars.size() < 4);
        debug(0) << "[begin] InjectDmaTransfer::store\n";
        const Load* maybe_load = op->value.as<Load>();
        // Has to be direct load-to-store for now.
        user_assert(maybe_load);

        debug(0) << "InjectDmaTransfer::" << op->name << " " <<  maybe_load->name << "\n";
        debug(0) << op->index << "\n";
        debug(0) << maybe_load->index << "\n";
        Expr op_index = op->index;
        // TODO: Is it a good idea? Maybe not.
        op_index = substitute_in_all_lets(op_index);
        op_index = substitute(containing_lets, op_index);

        Expr value_index = maybe_load->index;
        value_index = substitute_in_all_lets(value_index);
        value_index = substitute(containing_lets, value_index);

        vector<Expr> store_strides;
        vector<Expr> value_strides;
        debug(0) << op->index << "\n" << op_index << "\n";
        debug(0) << maybe_load->index << "\n" << value_index << "\n";

        for (const auto& v: loop_vars) {
            Scope<Expr> local_scope;
            // local_scope.push(v.name, var);
            local_scope.push(v.name, 1);
            debug(0) << "is_linear (stride) store: " << v.name << " " << is_linear(op_index, local_scope) << "\n";
            debug(0) << "is_linear (stride) load: " << v.name << " " << is_linear(value_index, local_scope) << "\n";
            store_strides.push_back(is_linear(op_index, local_scope));
            value_strides.push_back(is_linear(value_index, local_scope));
            // user_assert(store_strides.back().defined());
            // user_assert(value_strides.back().defined());
        }
        Expr store_stride = store_strides.back();
        Expr value_stride = value_strides.back();

        // user_assert(is_one(store_stride));
        // user_assert(is_one(value_stride));
        debug(0) << "Went past is_one " << store_stride << " " << is_one(store_stride)
                  << " " << value_stride << " " << is_one(value_stride) << "\n";
        const auto& v = loop_vars.back();
        Expr var = Variable::make(op->index.type(), v.name);
        loops_to_be_removed.insert(v.name);
        Expr store_base = substitute(var, v.min, op_index);
        Expr value_base = substitute(var, v.min, value_index);

        store_base = simplify(store_base);
        value_base = simplify(value_base);
        debug(0) << ">>> " << store_base << "\n>>> "
                  << value_base << "\n>>>" << v.extent << "\n";

        Expr copy_call = Call::make(Int(32), "halide_xtensa_copy_1d", {op->name, store_base, maybe_load->name, value_base, v.extent, op->value.type().bytes()}, Call::PureExtern);
        // Expr var_copy = Variable::make(copy_call.type(), op->name + "copy_id");
        // Stmt was_copy_scheduled = AssertStmt::make(var_copy > 0, -1);
        // Stmt copy_let = LetStmt::make(op->name + "copy_id", copy_call, was_copy_scheduled);

        Expr wait_result = Call::make(Int(32), "halide_xtensa_wait_for_copy", {copy_call}, Call::PureExtern);
        Stmt wait_is_done = AssertStmt::make(wait_result == 0, -1);

        return wait_is_done;
    }

 public:
    InjectDmaTransferIntoProducer(const string& pn) : producer_name(pn) { }
};

// TODO(vksnk): move to separate file.
class InjectDmaTransfer : public IRMutator {
    using IRMutator::visit;
    const std::map<std::string, Function> &env;

    Stmt visit(const ProducerConsumer* op) override {
        if (op->is_producer) {
            auto it = env.find(op->name);
            if (it != env.end()) {
                Function f = it->second;
                if (f.schedule().dma()) {
                    Stmt body = mutate(op->body);
                    body = InjectDmaTransferIntoProducer(op->name).mutate(body);
                    return ProducerConsumer::make_produce(op->name, body);
                }
            }
        }
        return IRMutator::visit(op);
    }
public:
    InjectDmaTransfer(const std::map<std::string, Function> &e) : env(e) { }
};

Stmt inject_dma_transfer(Stmt s, const std::map<std::string, Function> &env) {
    s = InjectDmaTransfer(env).mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
