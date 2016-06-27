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
    bool found_device_malloc;

    FindLastUse(string s) : func(s), found_device_malloc(false), in_loop(false) {}

private:
    bool in_loop;
    Stmt containing_stmt;

    using IRVisitor::visit;

    void visit(const For *loop) {
        loop->min.accept(this);
        loop->extent.accept(this);
        bool old_in_loop = in_loop;
        in_loop = true;
        loop->body.accept(this);
        in_loop = old_in_loop;
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
        if (call->name == "halide_device_malloc" && expr_uses_var(call, func + ".buffer")) {
            found_device_malloc = true;
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
        if (starts_with(var->name, func + ".") &&
            (ends_with(var->name, ".buffer") || ends_with(var->name, ".host"))) {
            last_use = containing_stmt;
        }
    }

    void visit(const IfThenElse *op) {
        // It's a bad idea to inject it in either side of an
        // ifthenelse, so we treat this as being in a loop.
        op->condition.accept(this);
        bool old_in_loop = in_loop;
        in_loop = true;
        op->then_case.accept(this);
        if (op->else_case.defined()) {
            op->else_case.accept(this);
        }
        in_loop = old_in_loop;
    }

    void visit(const ProducerConsumer *pipe) {
        if (in_loop) {
            IRVisitor::visit(pipe);
        } else {

            Stmt old_containing_stmt = containing_stmt;

            containing_stmt = pipe->produce;
            pipe->produce.accept(this);

            if (pipe->update.defined()) {
                containing_stmt = pipe->update;
                pipe->update.accept(this);
            }

            containing_stmt = old_containing_stmt;
            pipe->consume.accept(this);
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

Stmt make_free(string func, bool device) {
    Stmt free = Free::make(func);
    if (device) {
        Expr buf = Variable::make(Handle(), func + ".buffer");
        Stmt device_free = call_extern_and_assert("halide_device_free", {buf});
        free = Block::make({device_free, free});
    }
    return free;
}

class InjectMarker : public IRMutator {
public:
    string func;
    Stmt last_use;
    bool inject_device_free;

    InjectMarker() : inject_device_free(false), injected(false) {}
private:

    bool injected;

    using IRMutator::visit;

    Stmt inject_marker(Stmt s) {
        if (injected) return s;
        if (s.same_as(last_use)) {
            injected = true;
            return Block::make(s, make_free(func, inject_device_free));
        } else {
            return mutate(s);
        }
    }

    void visit(const ProducerConsumer *pipe) {
        // Do it in reverse order, so the injection occurs in the last instance of the stmt.
        Stmt new_consume = inject_marker(pipe->consume);
        Stmt new_update;
        if (pipe->update.defined()) {
            new_update = inject_marker(pipe->update);
        }
        Stmt new_produce = inject_marker(pipe->produce);

        if (new_produce.same_as(pipe->produce) &&
            new_update.same_as(pipe->update) &&
            new_consume.same_as(pipe->consume)) {
            stmt = pipe;
        } else {
            stmt = ProducerConsumer::make(pipe->name, new_produce, new_update, new_consume);
        }
    }

    void visit(const Block *block) {
        Stmt new_rest = inject_marker(block->rest);
        Stmt new_first = inject_marker(block->first);

        if (new_first.same_as(block->first) &&
            new_rest.same_as(block->rest)) {
            stmt = block;
        } else {
            stmt = Block::make(new_first, new_rest);
        }
    }
};

class InjectEarlyFrees : public IRMutator {
    using IRMutator::visit;

    void visit(const Allocate *alloc) {
        IRMutator::visit(alloc);
        alloc = stmt.as<Allocate>();
        internal_assert(alloc);

        FindLastUse last_use(alloc->name);
        stmt.accept(&last_use);

        if (last_use.last_use.defined()) {
            InjectMarker inject_marker;
            inject_marker.func = alloc->name;
            inject_marker.last_use = last_use.last_use;
            inject_marker.inject_device_free = last_use.found_device_malloc;
            stmt = inject_marker.mutate(stmt);
        } else {
            stmt = Allocate::make(alloc->name, alloc->type, alloc->extents, alloc->condition,
                                  Block::make(alloc->body, make_free(alloc->name, last_use.found_device_malloc)),
                                  alloc->new_expr);
        }

    }
};

Stmt inject_early_frees(Stmt s) {
    InjectEarlyFrees early_frees;
    return early_frees.mutate(s);
}

// TODO: test

}
}
