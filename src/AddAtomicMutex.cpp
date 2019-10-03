#include "AddAtomicMutex.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::set;
using std::string;

namespace {

class FindLoad : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;

    set<string> symbols;
    bool found = false;

    void visit(const Load *op) override {
        if (symbols.find(op->name) != symbols.end()) {
            found = true;
        }
        include(op->predicate);
        include(op->index);
    }

    bool find_load(Expr e, const set<string> &symbols) {
        found = false;
        this->symbols = symbols;
        include(e);
        return found;
    }
};

class FindAtomicLetBindings : public IRGraphVisitor {
public:
    using IRVisitor::visit;

    FindAtomicLetBindings(const set<string> &store_names)
        : store_names(store_names) {}

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
        if (inside_store) {
            if (let_bindings.contains(op->name)) {
                Expr e = let_bindings.get(op->name);
                FindLoad finder;
                if (finder.find_load(e, store_names)) {
                    found = true;
                }
            }
        }
    }

    void visit(const Store *op) override {
        include(op->predicate);
        inside_store = true;
        include(op->value);
        inside_store = false;
        include(op->index);
    }

    bool inside_store;
    set<string> store_names;
    Scope<Expr> let_bindings;
    bool found = false;
};

class CollectStoreNames : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;
    
    void visit(const Store *op) override {
        include(op->predicate);
        include(op->value);
        include(op->index);
        store_names.insert(op->name);
    }

    set<string> store_names;
};

class RemoveUnnecessaryMutexUse : public IRMutator {
public:
    using IRMutator::visit;

    Stmt visit(const Atomic *op) override {
        // Collect the names of all Store nodes inside.
        CollectStoreNames collector;
        op->body.accept(&collector);
        // Search for let bindings that access the producers.
        FindAtomicLetBindings finder(collector.store_names);
        op->body.accept(&finder);
        if (finder.found) {
            // Can't remove mutex lock. Leave the Stmt as is.
            return IRMutator::visit(op);
        } else {
            remove_mutex_lock_names.insert(op->mutex_name);
            Stmt body = mutate(op->body);
            return Atomic::make(op->producer_name,
                                "",
                                std::move(body));
        }
    }

    std::set<string> remove_mutex_lock_names;
};

class FindStoreInAtomicMutex : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;

    FindStoreInAtomicMutex(const std::set<std::string> &store_names)
        : store_names(store_names) {}

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
    const std::set<std::string> &store_names;
    bool found = false;
    std::string producer_name;
    std::string mutex_name;
};

class FindStoreIndex : public IRGraphVisitor {
public:
    using IRGraphVisitor::visit;

    void visit(const Store *op) override {
        // Ideally we want to insert equal() checks here for different stores,
        // but the indices of them actually are different in the case of tuples,
        // since they usually refer to the strides/min/extents of their own tuple
        // buffers. However, different elements in a tuple would have the same
        // strides/min/extents so we are fine.
        if (index.defined()) {
            return;
        }
    }

    std::string producer_name;
    Expr index;
};

class AddAtomicMutex : public IRMutator {
public:
    using IRMutator::visit;

    const std::map<std::string, Function> &env;
    // The set of producers that have allocated a mutex buffer
    std::set<string> allocated_mutexes;

    AddAtomicMutex(const std::map<std::string, Function> &env)
        : env(env) {}

    Stmt allocate_mutex(const std::string &mutex_name, Expr extent, Stmt body) {
        Expr mutex_array = Call::make(type_of<halide_mutex_array *>(),
                                      "halide_mutex_array_create",
                                      {extent},
                                      Call::Extern);
        // Allocate a scalar of halide_mutex_array.
        // This generate halide_mutex_array mutex[1];
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
        FindStoreInAtomicMutex finder({op->name});
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

        const std::string &mutex_name = finder.mutex_name;
        Stmt body = mutate(op->body);
        Expr extent = Expr(1);
        for (Expr e : op->extents) {
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
                              op->free_function);
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
        const Function &f = func_it->second;
        internal_assert(f.output_buffers().size() > 0) <<
            "Found a producer node that contains an atomic node that requires mutex lock, "
            "but does not have an Allocate node and is not an output function. This is not supported.\n";

        std::set<std::string> store_names;
        for (auto buffer : f.output_buffers()) {
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

        // We assume all output buffers have the same extent
        Parameter output_buffer = f.output_buffers()[0];
        Expr extent = Expr(1);
        for (int i = 0; i < output_buffer.dimensions(); i++) {
            // TODO: cleanup this by adding access methods to Parameter class to extract
            //       the extent variable.
            string extent_name = output_buffer.name() + ".extent." + std::to_string(i);
            extent = extent * Variable::make(Int(32), extent_name);
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
        FindStoreIndex find;
        op->body.accept(&find);

        Expr index = find.index;
        if (!index.defined()) {
            // scalar output
            index = Expr(0);
        }
        Stmt body = op->body;
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

        return Atomic::make(op->producer_name,
                            op->mutex_name,
                            std::move(body));
    }
};

}  // namespace

Stmt add_atomic_mutex(Stmt s, const std::map<std::string, Function> &env) {
    s = RemoveUnnecessaryMutexUse().mutate(s);
    s = AddAtomicMutex(env).mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
