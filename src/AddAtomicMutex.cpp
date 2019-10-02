#include "AddAtomicMutex.h"
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
        // Search for let bindings that access the providers.
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
                                {},
                                op->tuple_size,
                                op->dimensions,
                                std::move(body));
        }
    }

    std::set<string> remove_mutex_lock_names;
};

class FindAtomicMutexUsage : public IRGraphVisitor {
public:
    FindAtomicMutexUsage(const std::string &producer_name)
        : producer_name(producer_name) {}

    using IRGraphVisitor::visit;

    void visit(const Atomic *op) override {
        for (Expr i : op->mutex_indices) {
            include(i);
        }
        include(op->body);
        if (op->producer_name == producer_name && !op->mutex_name.empty()) {
            if (found) {
                // Multiple atomics inside the producer,
                // make sure they have consistent information
                internal_assert(mutex_name == op->mutex_name &&
                    tuple_size == op->tuple_size &&
                    dimensions == op->dimensions) <<
                    "Inconsistent information of atomics inside a Producer node.\n";
            }
            found = true;
            mutex_name = op->mutex_name;
            tuple_size = op->tuple_size;
            dimensions = op->dimensions;
        }
    }

    const std::string &producer_name;
    bool found = false;
    std::string mutex_name;
    int tuple_size = 0;
    int dimensions = 0;
};

class AddAtomicMutex : public IRMutator {
public:
    using IRMutator::visit;

    Stmt visit(const ProducerConsumer *op) override {
        FindAtomicMutexUsage finder(op->name);
        if (op->is_producer) {
            finder.visit(op);
            if (!finder.found) {
                return IRMutator::visit(op);
            }
        } else {
            return IRMutator::visit(op);
        }

        const std::string &producer_name = finder.producer_name;
        const std::string &mutex_name = finder.mutex_name;
        int tuple_size = finder.tuple_size;
        int dimensions = finder.dimensions;
        const char *extent_field = tuple_size == 1 ? ".extent." : ".0.extent.";
        const char *min_field = tuple_size == 1 ? ".min." : ".0.min.";
        const char *stride_field = tuple_size == 1 ? ".stride." : ".0.stride.";
        Expr buffer_size = Expr(1);
        for (int i = 0; i < dimensions; i++) {
            Expr extent = Variable::make(Int(32), producer_name + extent_field + std::to_string(i));
            buffer_size *= extent;
        }
        Expr mutex_array = Call::make(type_of<halide_mutex_array *>(),
                                      "halide_mutex_array_create",
                                      {buffer_size},
                                      Call::Extern);
        Stmt body = mutate(op->body);
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
        // Insert Let statements for the buffer's min and stride for storage flattening.
        for (int i = 0; i < dimensions; i++) {
            body = LetStmt::make(mutex_name + ".min." + std::to_string(i),
                                 Variable::make(Int(32), producer_name + min_field + std::to_string(i)),
                                 body);
            body = LetStmt::make(mutex_name + ".stride." + std::to_string(i),
                                 Variable::make(Int(32), producer_name + stride_field + std::to_string(i)),
                                 body);
        }

        return ProducerConsumer::make(op->name, op->is_producer, std::move(body));
    }

    Stmt visit(const Atomic *op) override {
        if (op->mutex_name == "") {
            return IRMutator::visit(op);
        }

        // op->mutex_indices.size() could be 0.
        Expr index = op->mutex_indices.size() == 1 ? op->mutex_indices[0] : Expr(0);
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
                            op->mutex_indices,
                            op->tuple_size,
                            op->dimensions,
                            std::move(body));
    }
};

}  // namespace

Stmt add_atomic_mutex(Stmt s) {
    s = RemoveUnnecessaryMutexUse().mutate(s);
    s = AddAtomicMutex().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
