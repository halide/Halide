#include <map>

#include "EarlyFree.h"
#include "IRMutator.h"
#include "IREquality.h"
#include "ExprUsesVar.h"
#include "InjectHostDevBufferCopies.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

class FindLastUse : public IRVisitor {
public:
    string func;
    Stmt last_use;

    FindLastUse(string s) : func(s) {}

private:
    bool in_loop = false;
    Stmt containing_stmt;

    using IRVisitor::visit;

    void visit(const For *loop) {
        loop->min.accept(this);
        loop->extent.accept(this);
        ScopedValue<bool> old_in_loop(in_loop, true);
        loop->body.accept(this);
    }

    void visit(const Load *load) {
        if (func == load->name) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(load);
    }

    void visit(const Call *call) {
        if (call->name == func) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(call);
    }

    void visit(const Store *store) {
        if (func == store->name) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(store);
    }

    void visit(const Variable *var) {
        if (var->name == func || var->name == func + ".buffer") {
            // Don't free the allocation while a buffer that may refer
            // to it is still in use.
            last_use = containing_stmt;
        }
    }

    void visit(const IfThenElse *op) {
        // It's a bad idea to inject it in either side of an
        // ifthenelse, so we treat this as being in a loop.
        op->condition.accept(this);
        ScopedValue<bool> old_in_loop(in_loop, true);
        op->then_case.accept(this);
        if (op->else_case.defined()) {
            op->else_case.accept(this);
        }
    }

    void visit(const Block *block) {
        if (in_loop) {
            IRVisitor::visit(block);
        } else {
            Stmt old_containing_stmt = containing_stmt;
            containing_stmt = block->first;
            block->first.accept(this);
            if (block->rest.defined()) {
                containing_stmt = block->rest;
                block->rest.accept(this);
            }
            containing_stmt = old_containing_stmt;
        }
    }
};

class InjectMarker : public IRMutator2 {
public:
    string func;
    Stmt last_use;

private:

    bool injected = false;

    using IRMutator2::visit;

    Stmt inject_marker(Stmt s) {
        if (injected) return s;
        if (s.same_as(last_use)) {
            injected = true;
            return Block::make(s, Free::make(func));
        } else {
            return mutate(s);
        }
    }

    Stmt visit(const Block *block) override {
        Stmt new_rest = inject_marker(block->rest);
        Stmt new_first = inject_marker(block->first);

        if (new_first.same_as(block->first) &&
            new_rest.same_as(block->rest)) {
            return block;
        } else {
            return Block::make(new_first, new_rest);
        }
    }
};

class InjectEarlyFrees : public IRMutator2 {
    using IRMutator2::visit;

    Stmt visit(const Allocate *alloc) override {
        Stmt stmt = IRMutator2::visit(alloc);
        alloc = stmt.as<Allocate>();
        internal_assert(alloc);

        FindLastUse last_use(alloc->name);
        stmt.accept(&last_use);

        if (last_use.last_use.defined()) {
            InjectMarker inject_marker;
            inject_marker.func = alloc->name;
            inject_marker.last_use = last_use.last_use;
            stmt = inject_marker.mutate(stmt);
        } else {
            stmt = Allocate::make(alloc->name, alloc->type, alloc->memory_type,
                                  alloc->extents, alloc->condition,
                                  Block::make(alloc->body, Free::make(alloc->name)),
                                  alloc->new_expr, alloc->free_function);
        }
        return stmt;

    }
};

Stmt inject_early_frees(Stmt s) {
    InjectEarlyFrees early_frees;
    return early_frees.mutate(s);
}

}
}
