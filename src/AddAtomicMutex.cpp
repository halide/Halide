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
class CollectProducerStoreNames : public IRVisitor {
public:
    CollectProducerStoreNames(const std::string &producer_name)
        : producer_name(producer_name) {
    }

    Scope<void> store_names;

protected:
    using IRVisitor::visit;

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        if (op->name == producer_name || starts_with(op->name, producer_name + ".")) {
            // This is a Store for the designated Producer.
            store_names.push(op->name);
        }
    }

    const std::string &producer_name;
};

/** Find Store inside of an Atomic node for the designated producer
 *  and return their indices. */
class FindProducerStoreIndex : public IRVisitor {
public:
    FindProducerStoreIndex(const std::string &producer_name)
        : producer_name(producer_name) {
    }

    Expr index;  // The returned index.

protected:
    using IRVisitor::visit;

    // Need to also extract the let bindings of a Store index.
    void visit(const Let *op) override {
        IRVisitor::visit(op);  // Make sure we visit the Store first.
        if (index.defined()) {
            if (expr_uses_var(index, op->name)) {
                index = Let::make(op->name, op->value, index);
            }
        }
    }
    void visit(const LetStmt *op) override {
        IRVisitor::visit(op);  // Make sure we visit the Store first.
        if (index.defined()) {
            if (expr_uses_var(index, op->name)) {
                index = Let::make(op->name, op->value, index);
            }
        }
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
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
        }
    }

    const std::string &producer_name;
};

/** Throws an assertion for cases where the indexing on left-hand-side of
 *  an atomic update references to itself.
 *  e.g. f(clamp(f(r), 0, 100)) = f(r) + 1 should be rejected. */
class CheckAtomicValidity : public IRVisitor {
protected:
    using IRVisitor::visit;

    void visit(const Atomic *op) override {
        any_atomic = true;

        // Collect the names of all Store nodes inside.
        CollectProducerStoreNames collector(op->producer_name);
        op->body.accept(&collector);

        // Find the indices from the Store nodes inside the body.
        FindProducerStoreIndex find(op->producer_name);
        op->body.accept(&find);

        Expr index = find.index;
        if (index.defined()) {
            user_assert(!expr_uses_vars(index, collector.store_names))
                << "Can't use atomic() on an update where the index written "
                << "to depends on the current value of the Func\n";
        }
        op->body.accept(this);
    }

public:
    bool any_atomic = false;
};

/** Search if the value of a Store node has a variable pointing to a let binding,
 *  where the let binding contains the Store location. Use for checking whether
 *  we need a mutex lock for Atomic since some lowering pass before lifted a let
 *  binding from the Store node (currently only SplitTuple would do this). */
class FindAtomicLetBindings : public IRVisitor {
public:
    FindAtomicLetBindings(const Scope<void> &store_names)
        : store_names(store_names) {
    }

    bool found = false;

protected:
    using IRVisitor::visit;

    void visit(const Let *op) override {
        op->value.accept(this);
        {
            ScopedBinding<Expr> bind(let_bindings, op->name, op->value);
            op->body.accept(this);
        }
    }

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        {
            ScopedBinding<Expr> bind(let_bindings, op->name, op->value);
            op->body.accept(this);
        }
    }

    void visit(const Variable *op) override {
        if (!inside_store.empty()) {
            // If this Variable inside the store value is an expression
            // that depends on one of the store_names, we found a lifted let.
            if (expr_uses_vars(op, store_names, let_bindings)) {
                found = true;
            }
        }
    }

    void visit(const Store *op) override {
        op->predicate.accept(this);
        op->index.accept(this);
        if (store_names.contains(op->name)) {
            // If we are in a designated store and op->value has a let binding
            // that uses one of the store_names, we found a lifted let.
            ScopedValue<std::string> old_inside_store(inside_store, op->name);
            op->value.accept(this);
        } else {
            op->value.accept(this);
        }
    }

    std::string inside_store;
    const Scope<void> &store_names;
    Scope<Expr> let_bindings;
};

/** Clear out the Atomic node's mutex usages if it doesn't need one. */
class RemoveUnnecessaryMutexUse : public IRMutator {
public:
    std::set<std::string> remove_mutex_lock_names;

protected:
    using IRMutator::visit;

    Stmt visit(const Atomic *op) override {
        // Collect the names of all Store nodes inside.
        CollectProducerStoreNames collector(op->producer_name);
        op->body.accept(&collector);
        // Search for let bindings that access the producers.
        FindAtomicLetBindings finder(collector.store_names);
        op->body.accept(&finder);
        // Each individual Store that remains can be done as a CAS
        // loop or an actual atomic RMW of some form.
        if (finder.found) {
            // Can't remove mutex lock. Leave the Stmt as is.
            return IRMutator::visit(op);
        } else {
            remove_mutex_lock_names.insert(op->mutex_name);
            Stmt body = mutate(op->body);
            return Atomic::make(op->producer_name,
                                std::string{},
                                std::move(body));
        }
    }
};

/** Find Store inside an Atomic that matches the provided store_names. */
class FindStoreInAtomicMutex : public IRVisitor {
public:
    using IRVisitor::visit;

    FindStoreInAtomicMutex(const std::set<std::string> &store_names)
        : store_names(store_names) {
    }

    bool found = false;
    std::string producer_name;
    std::string mutex_name;

protected:
    void visit(const Atomic *op) override {
        if (!found && !op->mutex_name.empty()) {
            ScopedValue<bool> old_in_atomic_mutex(in_atomic_mutex, true);
            op->body.accept(this);
            if (found) {
                // We found a Store inside Atomic with matching name,
                // record the mutex information.
                producer_name = op->producer_name;
                mutex_name = op->mutex_name;
            }
        } else {
            op->body.accept(this);
        }
    }

    void visit(const Store *op) override {
        if (in_atomic_mutex) {
            if (store_names.find(op->name) != store_names.end()) {
                found = true;
            }
        }
        IRVisitor::visit(op);
    }

    bool in_atomic_mutex = false;
    const std::set<std::string> &store_names;
};

/** Replace the indices in the Store nodes with the specified variable. */
class ReplaceStoreIndexWithVar : public IRMutator {
public:
    ReplaceStoreIndexWithVar(const std::string &producer_name, Expr var)
        : producer_name(producer_name), var(std::move(var)) {
    }

protected:
    using IRMutator::visit;

    Stmt visit(const Store *op) override {
        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        return Store::make(op->name,
                           std::move(value),
                           var,
                           op->param,
                           std::move(predicate),
                           op->alignment);
    }

    const std::string &producer_name;
    Expr var;
};

/** Add mutex allocation & lock & unlock if required. */
class AddAtomicMutex : public IRMutator {
public:
    AddAtomicMutex(const std::vector<Function> &o) {
        for (const Function &f : o) {
            outputs.emplace(f.name(), f);
        }
    }

protected:
    using IRMutator::visit;

    // Maps from a producer name to a mutex name, for all encountered atomic
    // nodes.
    Scope<std::string> needs_mutex_allocation;

    // Pipeline outputs
    std::map<std::string, Function> outputs;

    Stmt allocate_mutex(const std::string &mutex_name, Expr extent, Stmt body) {
        Expr mutex_array = Call::make(type_of<halide_mutex_array *>(),
                                      "halide_mutex_array_create",
                                      {std::move(extent)},
                                      Call::Extern);

        // Allocate a scalar of halide_mutex_array.
        // This generates halide_mutex_array mutex[1];
        body = Allocate::make(mutex_name,
                              type_of<halide_mutex *>(),
                              MemoryType::Stack,
                              {},
                              const_true(),
                              body,
                              mutex_array,
                              "halide_mutex_array_destroy");
        return body;
    }

    Stmt visit(const Allocate *op) override {
        // If this Allocate node is allocating a buffer for a producer,
        // and there is a Store node inside of an Atomic node requiring mutex lock
        // matching the name of the Allocate, allocate a mutex lock.

        Stmt body = mutate(op->body);

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
        }
    }

    Stmt visit(const ProducerConsumer *op) override {
        // Usually we allocate the mutex buffer at the Allocate node,
        // but outputs don't have Allocate. For those we allocate the mutex
        // buffer at the producer node.

        if (!op->is_producer) {
            // This is a consumer
            return IRMutator::visit(op);
        }

        auto it = outputs.find(op->name);
        if (it == outputs.end()) {
            // Not an output
            return IRMutator::visit(op);
        }

        Function f = it->second;

        Stmt body = mutate(op->body);

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
    }

    Stmt visit(const Atomic *op) override {
        if (op->mutex_name.empty()) {
            return IRMutator::visit(op);
        }

        // Lock the mutexes using the indices from the Store nodes inside the body.
        FindProducerStoreIndex find(op->producer_name);
        op->body.accept(&find);

        Stmt body = op->body;

        Expr index = find.index;
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
            body = ReplaceStoreIndexWithVar(op->producer_name, index).mutate(body);
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
    }
};

}  // namespace

Stmt add_atomic_mutex(Stmt s, const std::vector<Function> &outputs) {
    CheckAtomicValidity check;
    s.accept(&check);
    if (check.any_atomic) {
        s = RemoveUnnecessaryMutexUse().mutate(s);
        s = AddAtomicMutex(outputs).mutate(s);
    }
    return s;
}

}  // namespace Internal
}  // namespace Halide
