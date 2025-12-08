#include <map>
#include <utility>

#include "EarlyFree.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "InjectHostDevBufferCopies.h"

namespace Halide {
namespace Internal {
namespace {

using std::string;

class FindLastUse : public IRVisitor {
public:
    string func;
    Stmt last_use;

    FindLastUse(string s)
        : func(std::move(s)) {
    }

private:
    bool in_loop = false;
    Stmt containing_stmt;

    using IRVisitor::visit;

    void visit(const For *loop) override {
        loop->min.accept(this);
        loop->max.accept(this);
        ScopedValue<bool> old_in_loop(in_loop, true);
        loop->body.accept(this);
    }

    void visit(const Fork *fork) override {
        ScopedValue<bool> old_in_loop(in_loop, true);
        fork->first.accept(this);
        fork->rest.accept(this);
    }

    void visit(const Acquire *acq) override {
        acq->semaphore.accept(this);
        acq->count.accept(this);
        ScopedValue<bool> old_in_loop(in_loop, true);
        acq->body.accept(this);
    }

    void visit(const Load *load) override {
        if (func == load->name) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(load);
    }

    void visit(const Call *call) override {
        if (call->name == func) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(call);
    }

    void visit(const Store *store) override {
        if (func == store->name) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(store);
    }

    void visit(const Variable *var) override {
        if (var->name == func || var->name == func + ".buffer") {
            // Don't free the allocation while a buffer that may refer
            // to it is still in use.
            last_use = containing_stmt;
        }
    }

    void visit(const IfThenElse *op) override {
        // It's a bad idea to inject it in either side of an
        // ifthenelse, so we treat this as being in a loop.
        op->condition.accept(this);
        ScopedValue<bool> old_in_loop(in_loop, true);
        op->then_case.accept(this);
        if (op->else_case.defined()) {
            op->else_case.accept(this);
        }
    }

    void visit(const Block *block) override {
        if (in_loop) {
            IRVisitor::visit(block);
        } else {
            ScopedValue<Stmt> old_containing_stmt(containing_stmt, block->first);
            block->first.accept(this);
            if (block->rest.defined()) {
                containing_stmt = block->rest;
                block->rest.accept(this);
            }
        }
    }

    void visit(const Atomic *op) override {
        if (op->mutex_name == func) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(op);
    }
};

Stmt inject_marker(const Stmt &stmt, const string &func, const Stmt &last_use) {
    bool injected = false;
    return mutate_with(stmt, [&](auto *self, const Block *block) -> Stmt {
        auto do_injection = [&](Stmt s) {
            if (injected) {
                return s;
            }
            if (s.same_as(last_use)) {
                injected = true;
                return Block::make(s, Free::make(func));
            }
            return self->mutate(s);
        };

        Stmt new_rest = do_injection(block->rest);
        Stmt new_first = do_injection(block->first);

        if (new_first.same_as(block->first) && new_rest.same_as(block->rest)) {
            return block;
        }
        return Block::make(new_first, new_rest);
    });
};

class InjectEarlyFrees : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Allocate *alloc) override {
        Stmt stmt = IRMutator::visit(alloc);
        alloc = stmt.as<Allocate>();
        internal_assert(alloc);

        FindLastUse last_use(alloc->name);
        stmt.accept(&last_use);

        if (last_use.last_use.defined()) {
            stmt = inject_marker(stmt, alloc->name, last_use.last_use);
        } else {
            stmt = Allocate::make(alloc->name, alloc->type, alloc->memory_type,
                                  alloc->extents, alloc->condition,
                                  Block::make(alloc->body, Free::make(alloc->name)),
                                  alloc->new_expr, alloc->free_function, alloc->padding);
        }
        return stmt;
    }
};

}  // namespace

Stmt inject_early_frees(const Stmt &s) {
    InjectEarlyFrees early_frees;
    return early_frees.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
