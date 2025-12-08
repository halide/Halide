#include "AddAtomicMutex.h"
#include "ExprUsesVar.h"
#include "Func.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "OutputImageParam.h"
#include <utility>

namespace Halide {
namespace Internal {

namespace {

/** Collect names of all stores matching the producer name inside a statement. */
Scope<void> collect_producer_store_names(Stmt s, const std::string &producer_name) {
    Scope<void> store_names;
    visit_with(s, [&](auto *self, const Store *op) {
        self->visit_base(op);
        if (op->name == producer_name || starts_with(op->name, producer_name + ".")) {
            // This is a Store for the designated Producer.
            store_names.push(op->name);
        }
    });
    return store_names;
}

/** Find Store inside of an Atomic node for the designated producer
 *  and return their indices. */
Expr find_producer_store_index(Stmt s, const std::string &producer_name) {
    Expr index;
    visit_with(
        s,
        // Need to also extract the let bindings of a Store index.
        [&](auto *self, const Let *op) {
            self->visit_base(op);  // Make sure we visit the Store first.
            if (index.defined()) {
                if (expr_uses_var(index, op->name)) {
                    index = Let::make(op->name, op->value, index);
                }
            }  //
        },
        [&](auto *self, const LetStmt *op) {
            self->visit_base(op);  // Make sure we visit the Store first.
            if (index.defined()) {
                if (expr_uses_var(index, op->name)) {
                    index = Let::make(op->name, op->value, index);
                }
            }  //
        },
        [&](auto *self, const Store *op) {
            self->visit_base(op);
            if (op->name == producer_name || starts_with(op->name, producer_name + ".")) {
                // This is a Store for the designated producer.

                // Ideally we want to insert equal() checks here for different stores,
                // but the indices of them actually are different in the case of tuples,
                // since they usually refer to the strides/min/extents of their own tuple
                // buffers. However, different elements in a tuple would have the same
                // strides/min/extents so we are fine.
                if (index.defined()) {
                    return;
                }
                index = op->index;
            }  //
        });
    return index;
}

/** Throws an assertion for cases where the indexing on left-hand-side of
 *  an atomic update references to itself.
 *  e.g. f(clamp(f(r), 0, 100)) = f(r) + 1 should be rejected. */
bool check_atomic_validity(Stmt s) {
    bool any_atomic = false;
    visit_with(s, [&](auto *self, const Atomic *op) {
        any_atomic = true;

        // Collect the names of all Store nodes inside.
        Scope<void> store_names = collect_producer_store_names(op->body, op->producer_name);

        // Find the indices from the Store nodes inside the body.
        Expr index = find_producer_store_index(op->body, op->producer_name);
        if (index.defined()) {
            user_assert(!expr_uses_vars(index, store_names))
                << "Can't use atomic() on an update where the index written "
                << "to depends on the current value of the Func\n";
        }

        op->body.accept(self);
    });
    return any_atomic;
}

/** Search if the value of a Store node has a variable pointing to a let binding,
 *  where the let binding contains the Store location. Use for checking whether
 *  we need a mutex lock for Atomic since some lowering pass before lifted a let
 *  binding from the Store node (currently only SplitTuple would do this). */
bool find_atomic_let_bindings(Stmt s, const Scope<void> &store_names) {
    bool found = false;
    std::string inside_store;
    Scope<Expr> let_bindings;
    visit_with(
        s,
        [&](auto *self, const Let *op) {
            op->value.accept(self);
            {
                ScopedBinding<Expr> bind(let_bindings, op->name, op->value);
                op->body.accept(self);
            }  //
        },
        [&](auto *self, const LetStmt *op) {
            op->value.accept(self);
            {
                ScopedBinding<Expr> bind(let_bindings, op->name, op->value);
                op->body.accept(self);
            }  //
        },
        [&](auto *self, const Variable *op) {
            if (!inside_store.empty()) {
                // If this Variable inside the store value is an expression
                // that depends on one of the store_names, we found a lifted let.
                if (expr_uses_vars(op, store_names, let_bindings)) {
                    found = true;
                }
            }  //
        },
        [&](auto *self, const Store *op) {
            op->predicate.accept(self);
            op->index.accept(self);
            if (store_names.contains(op->name)) {
                // If we are in a designated store and op->value has a let binding
                // that uses one of the store_names, we found a lifted let.
                ScopedValue<std::string> old_inside_store(inside_store, op->name);
                op->value.accept(self);
            } else {
                op->value.accept(self);
            }  //
        });
    return found;
}

/** Clear out the Atomic node's mutex usages if it doesn't need one. */
Stmt remove_unnecessary_mutex_use(const Stmt &s) {
    std::set<std::string> remove_mutex_lock_names;
    return mutate_with(s, [&](auto *self, const Atomic *op) {
        // Collect the names of all Store nodes inside.
        Scope<void> store_names = collect_producer_store_names(op->body, op->producer_name);
        // Search for let bindings that access the producers.
        // Each individual Store that remains can be done as a CAS
        // loop or an actual atomic RMW of some form.
        if (find_atomic_let_bindings(op->body, store_names)) {
            // Can't remove mutex lock. Leave the Stmt as is.
            return self->visit_base(op);
        } else {
            remove_mutex_lock_names.insert(op->mutex_name);
            Stmt body = self->mutate(op->body);
            return Atomic::make(op->producer_name,
                                std::string{},
                                std::move(body));
        }
    });
}

/** Replace the indices in the Store nodes with the specified variable. */
Stmt replace_store_index_with_var(Stmt s, const std::string &producer_name, Expr var) {
    return mutate_with(s, [&](auto *self, const Store *op) {
        if (op->name == producer_name || starts_with(op->name, producer_name + ".")) {
            return Store::make(op->name, op->value, var, op->param, op->predicate, op->alignment);
        }
        return self->visit_base(op);
    });
}

Stmt allocate_mutex(const std::string &mutex_name, Expr extent, const Stmt &body) {
    Expr mutex_array = Call::make(type_of<halide_mutex_array *>(),
                                  "halide_mutex_array_create",
                                  {std::move(extent)},
                                  Call::Extern);

    // Allocate a scalar of halide_mutex_array.
    // This generates halide_mutex_array mutex[1];
    return Allocate::make(mutex_name,
                          type_of<halide_mutex *>(),
                          MemoryType::Stack,
                          {},
                          const_true(),
                          body,
                          mutex_array,
                          "halide_mutex_array_destroy");
}

/** Add mutex allocation & lock & unlock if required. */
Stmt inject_atomic_mutex(Stmt s, const std::vector<Function> &o) {
    // Maps from a producer name to a mutex name, for all encountered atomic
    // nodes.
    Scope<std::string> needs_mutex_allocation;

    // Pipeline outputs
    std::map<std::string, Function> outputs;

    for (const Function &f : o) {
        outputs.emplace(f.name(), f);
    }

    return mutate_with(
        s,
        [&](auto *self, const Allocate *op) -> Stmt {
            // If this Allocate node is allocating a buffer for a producer,
            // and there is a Store node inside of an Atomic node requiring mutex lock
            // matching the name of the Allocate, allocate a mutex lock.

            Stmt body = self->mutate(op->body);

            std::string producer_name;
            if (ends_with(op->name, ".0")) {
                producer_name = op->name.substr(0, op->name.size() - 2);
            } else {
                producer_name = op->name;
            }

            if (const std::string *mutex_name = needs_mutex_allocation.find(producer_name)) {
                Expr extent = cast<uint64_t>(1);  // uint64_t to handle LargeBuffers
                for (const Expr &e : op->extents) {
                    extent = extent * e;
                }

                body = allocate_mutex(*mutex_name, extent, body);

                // At this stage in lowering it should be impossible to have an
                // allocation that shadows the name of an outer allocation, but may as
                // well handle it anyway by using a scope and popping at each allocate
                // node.
                needs_mutex_allocation.pop(producer_name);
            }

            if (body.same_as(op->body)) {
                return op;
            } else {
                return Allocate::make(op->name,
                                      op->type,
                                      op->memory_type,
                                      op->extents,
                                      op->condition,
                                      std::move(body),
                                      op->new_expr,
                                      op->free_function,
                                      op->padding);
            }  //
        },
        [&](auto *self, const ProducerConsumer *op) -> Stmt {
            // Usually we allocate the mutex buffer at the Allocate node,
            // but outputs don't have Allocate. For those we allocate the mutex
            // buffer at the producer node.

            if (!op->is_producer) {
                // This is a consumer
                return self->visit_base(op);
            }

            auto it = outputs.find(op->name);
            if (it == outputs.end()) {
                // Not an output
                return self->visit_base(op);
            }

            Function f = it->second;

            Stmt body = self->mutate(op->body);

            if (const std::string *mutex_name = needs_mutex_allocation.find(it->first)) {
                // All output buffers in a Tuple have the same extent.
                OutputImageParam output_buffer = Func(f).output_buffers()[0];
                Expr extent = cast<uint64_t>(1);  // uint64_t to handle LargeBuffers
                for (int i = 0; i < output_buffer.dimensions(); i++) {
                    extent *= output_buffer.dim(i).extent();
                }
                body = allocate_mutex(*mutex_name, extent, body);
            }

            if (body.same_as(op->body)) {
                return op;
            } else {
                return ProducerConsumer::make(op->name, op->is_producer, std::move(body));
            }
        },
        [&](auto *self, const Atomic *op) {
            if (op->mutex_name.empty()) {
                return self->visit_base(op);
            }

            // Lock the mutexes using the indices from the Store nodes inside the body.
            Stmt body = op->body;
            Expr index = find_producer_store_index(body, op->producer_name);
            Expr index_let;  // If defined, represents the value of the lifted let binding.
            if (!index.defined()) {
                // Scalar output.
                index = Expr(0);
            } else {
                // Lift the index outside of the atomic node.
                // This is for avoiding side-effects inside those expressions
                // being evaluated twice.
                std::string name = unique_name('t');
                index_let = index;
                index = Variable::make(index.type(), name);
                body = replace_store_index_with_var(body, op->producer_name, index);
            }
            // This generates a pointer to the mutex array
            Expr mutex_array = Variable::make(
                type_of<halide_mutex_array *>(), op->mutex_name);
            // Add mutex locks & unlocks
            // If a thread locks the mutex and throws an exception,
            // halide_mutex_array_destroy will be called and cleanup the mutex locks.
            body = Block::make(
                Evaluate::make(Call::make(type_of<int>(),
                                          "halide_mutex_array_lock",
                                          {mutex_array, index},
                                          Call::CallType::Extern)),
                Block::make(std::move(body),
                            Evaluate::make(Call::make(type_of<int>(),
                                                      "halide_mutex_array_unlock",
                                                      {mutex_array, index},
                                                      Call::CallType::Extern))));
            Stmt ret = Atomic::make(op->producer_name,
                                    op->mutex_name,
                                    std::move(body));

            if (index_let.defined()) {
                // Attach the let binding outside of the atomic node.
                internal_assert(index.as<Variable>() != nullptr);
                ret = LetStmt::make(index.as<Variable>()->name, index_let, ret);
            }
            needs_mutex_allocation.push(op->producer_name, op->mutex_name);

            return ret;
        });
};

}  // namespace

Stmt add_atomic_mutex(Stmt s, const std::vector<Function> &outputs) {
    if (check_atomic_validity(s)) {
        s = remove_unnecessary_mutex_use(s);
        s = inject_atomic_mutex(s, outputs);
    }
    return s;
}

}  // namespace Internal
}  // namespace Halide
