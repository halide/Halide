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

using std::map;
using std::set;
using std::string;

namespace {

/** Collect names of all stores matching the producer name inside a statement. */
class CollectProducerStoreNames : public IRGraphVisitor {
public:
    CollectProducerStoreNames(const std::string &producer_name)
        : producer_name(producer_name) {
    }

    Scope<void> store_names;

protected:
    using IRGraphVisitor::visit;

    void visit(const Store *op) override {
        IRGraphVisitor::visit(op);
        if (op->name == producer_name || starts_with(op->name, producer_name + ".")) {
            // This is a Store for the desginated Producer.
            store_names.push(op->name);
        }
    }

    const std::string &producer_name;
};

/** Find Store inside of an Atomic node for the designated producer
 *  and return their indices. */
class FindProducerStoreIndex : public IRGraphVisitor {
public:
    FindProducerStoreIndex(const std::string &producer_name)
        : producer_name(producer_name) {
    }

    Expr index;  // The returned index.

protected:
    using IRGraphVisitor::visit;

    // Need to also extract the let bindings of a Store index.
    void visit(const Let *op) override {
        IRGraphVisitor::visit(op);  // Make sure we visit the Store first.
        if (index.defined()) {
            if (expr_uses_var(index, op->name)) {
                index = Let::make(op->name, op->value, index);
            }
        }
    }
    void visit(const LetStmt *op) override {
        IRGraphVisitor::visit(op);  // Make sure we visit the Store first.
        if (index.defined()) {
            if (expr_uses_var(index, op->name)) {
                index = Let::make(op->name, op->value, index);
            }
        }
    }

    void visit(const Store *op) override {
        IRGraphVisitor::visit(op);
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
class CheckAtomicValidity : public IRGraphVisitor {
protected:
    using IRGraphVisitor::visit;

    void visit(const Atomic *op) override {
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
};

/** Search if the value of a Store node has a variable pointing to a let binding,
 *  where the let binding contains the Store location. Use for checking whether
 *  we need a mutex lock for Atomic since some lowering pass before lifted a let
 *  binding from the Store node (currently only SplitTuple would do this). */
class FindAtomicLetBindings : public IRGraphVisitor {
public:
    FindAtomicLetBindings(const Scope<void> &store_names)
        : store_names(store_names) {
    }

    bool found = false;

protected:
    using IRVisitor::visit;

    void visit(const Let *op) override {
        include(op->value);
        {
            ScopedBinding<Expr> bind(let_bindings, op->name, op->value);
            include(op->body);
        }
    }

    void visit(const LetStmt *op) override {
        include(op->value);
        {
            ScopedBinding<Expr> bind(let_bindings, op->name, op->value);
            include(op->body);
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
        include(op->predicate);
        if (store_names.contains(op->name)) {
            // If we are in a designated store and op->value has a let binding
            // that uses one of the store_names, we found a lifted let.
            ScopedValue<string> old_inside_store(inside_store, op->name);
            include(op->value);
        } else {
            include(op->value);
        }
        include(op->index);
    }

    string inside_store;
    const Scope<void> &store_names;
    Scope<Expr> let_bindings;
};

/** Clear out the Atomic node's mutex usages if it doesn't need one. */
class RemoveUnnecessaryMutexUse : public IRMutator {
public:
    set<string> remove_mutex_lock_names;

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
                                string(),
                                std::move(body));
        }
    }
};

/** Find Store inside an Atomic that matches the provided store_names. */
class FindStoreInAtomicMutex : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;

    FindStoreInAtomicMutex(const std::set<std::string> &store_names)
        : store_names(store_names) {
    }

    bool found = false;
    string producer_name;
    string mutex_name;

protected:
    void visit(const Atomic *op) override {
        if (!found && !op->mutex_name.empty()) {
            ScopedValue<bool> old_in_atomic_mutex(in_atomic_mutex, true);
            include(op->body);
            if (found) {
                // We found a Store inside Atomic with matching name,
                // record the mutex information.
                producer_name = op->producer_name;
                mutex_name = op->mutex_name;
            }
        } else {
            include(op->body);
        }
    }

    void visit(const Store *op) override {
        if (in_atomic_mutex) {
            if (store_names.find(op->name) != store_names.end()) {
                found = true;
            }
        }
        IRGraphVisitor::visit(op);
    }

    bool in_atomic_mutex = false;
    const set<string> &store_names;
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
    AddAtomicMutex(const map<string, Function> &env)
        : env(env) {
    }

protected:
    using IRMutator::visit;

    const map<string, Function> &env;
    // The set of producers that have allocated a mutex buffer
    set<string> allocated_mutexes;

    Stmt allocate_mutex(const string &mutex_name, Expr extent, Stmt body) {
        Expr mutex_array = Call::make(type_of<halide_mutex_array *>(),
                                      "halide_mutex_array_create",
                                      {std::move(extent)},
                                      Call::Extern);
        // Allocate a scalar of halide_mutex_array.
        // This generates halide_mutex_array mutex[1];
        body = Allocate::make(mutex_name,
                              Handle(),
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
        set<string> store_names{op->name};
        FindStoreInAtomicMutex finder(store_names);
        op->body.accept(&finder);
        if (!finder.found) {
            // No Atomic node that requires mutex lock from this node inside.
            return IRMutator::visit(op);
        }

        if (allocated_mutexes.find(finder.mutex_name) != allocated_mutexes.end()) {
            // We've already allocated a mutex.
            return IRMutator::visit(op);
        }

        allocated_mutexes.insert(finder.mutex_name);

        const string &mutex_name = finder.mutex_name;
        Stmt body = mutate(op->body);
        Expr extent = Expr(1);
        for (const Expr &e : op->extents) {
            extent = extent * e;
        }
        body = allocate_mutex(mutex_name, extent, body);
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

    Stmt visit(const ProducerConsumer *op) override {
        // Usually we allocate the mutex buffer at the Allocate node,
        // but outputs don't have Allocate. For those we allocate the mutex
        // buffer at the producer node.

        if (!op->is_producer) {
            // This is a consumer.
            return IRMutator::visit(op);
        }

        // Find the corresponding output.
        auto func_it = env.find(op->name);
        if (func_it == env.end()) {
            // Not an output.
            return IRMutator::visit(op);
        }
        Func f = Func(func_it->second);
        if (f.output_buffers().empty()) {
            // Not an output.
            return IRMutator::visit(op);
        }

        set<string> store_names;
        for (const auto &buffer : f.output_buffers()) {
            store_names.insert(buffer.name());
        }

        FindStoreInAtomicMutex finder(store_names);
        op->body.accept(&finder);
        if (!finder.found) {
            // No Atomic node that requires mutex lock from this node inside.
            return IRMutator::visit(op);
        }

        if (allocated_mutexes.find(finder.mutex_name) != allocated_mutexes.end()) {
            // We've already allocated a mutex.
            return IRMutator::visit(op);
        }

        allocated_mutexes.insert(finder.mutex_name);

        // We assume all output buffers in a Tuple have the same extent.
        OutputImageParam output_buffer = f.output_buffers()[0];
        Expr extent = Expr(1);
        for (int i = 0; i < output_buffer.dimensions(); i++) {
            extent = extent * output_buffer.dim(i).extent();
        }
        Stmt body = mutate(op->body);
        body = allocate_mutex(finder.mutex_name, extent, body);
        return ProducerConsumer::make(op->name, op->is_producer, std::move(body));
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
            string name = unique_name('t');
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
        return ret;
    }
};

}  // namespace

Stmt add_atomic_mutex(Stmt s, const map<string, Function> &env) {
    CheckAtomicValidity check;
    s.accept(&check);
    s = RemoveUnnecessaryMutexUse().mutate(s);
    s = AddAtomicMutex(env).mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
