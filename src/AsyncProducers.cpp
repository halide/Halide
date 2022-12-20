#include "AsyncProducers.h"
#include "ExprUsesVar.h"
#include "Function.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

/** A mutator which eagerly folds no-op stmts */
class NoOpCollapsingMutator : public IRMutator {
protected:
    using IRMutator::visit;

    Stmt visit(const LetStmt *op) override {
        Stmt body = mutate(op->body);
        if (is_no_op(body)) {
            return body;
        } else {
            return LetStmt::make(op->name, op->value, body);
        }
    }

    Stmt visit(const For *op) override {
        Stmt body = mutate(op->body);
        if (is_no_op(body)) {
            return body;
        } else {
            return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
        }
    }

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);
        if (is_no_op(first)) {
            return rest;
        } else if (is_no_op(rest)) {
            return first;
        } else {
            return Block::make(first, rest);
        }
    }

    Stmt visit(const Fork *op) override {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);
        if (is_no_op(first)) {
            return rest;
        } else if (is_no_op(rest)) {
            return first;
        } else {
            return Fork::make(first, rest);
        }
    }

    Stmt visit(const Realize *op) override {
        Stmt body = mutate(op->body);
        if (is_no_op(body)) {
            return body;
        } else {
            return Realize::make(op->name, op->types, op->memory_type,
                                 op->bounds, op->condition, body);
        }
    }

    Stmt visit(const Allocate *op) override {
        Stmt body = mutate(op->body);
        if (is_no_op(body)) {
            return body;
        } else {
            return Allocate::make(op->name, op->type, op->memory_type,
                                  op->extents, op->condition, body,
                                  op->new_expr, op->free_function, op->padding);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);
        if (is_no_op(then_case) && is_no_op(else_case)) {
            return then_case;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

    Stmt visit(const Atomic *op) override {
        Stmt body = mutate(op->body);
        if (is_no_op(body)) {
            return body;
        } else {
            return Atomic::make(op->producer_name,
                                op->mutex_name,
                                std::move(body));
        }
    }
};

class GenerateProducerBody : public NoOpCollapsingMutator {
    const string &func;
    vector<Expr> sema;

    using NoOpCollapsingMutator::visit;

    // Preserve produce nodes and add synchronization
    Stmt visit(const ProducerConsumer *op) override {
        if (op->name == func && op->is_producer) {
            // Add post-synchronization
            internal_assert(!sema.empty()) << "Duplicate produce node: " << op->name << "\n";
            Stmt body = op->body;
            while (!sema.empty()) {
                Expr release = Call::make(Int(32), "halide_semaphore_release", {sema.back(), 1}, Call::Extern);
                body = Block::make(body, Evaluate::make(release));
                sema.pop_back();
            }
            return ProducerConsumer::make_produce(op->name, body);
        } else {
            Stmt body = mutate(op->body);
            if (is_no_op(body) || op->is_producer) {
                return body;
            } else {
                return ProducerConsumer::make(op->name, op->is_producer, body);
            }
        }
    }

    // Other stmt leaves get replaced with no-ops
    Stmt visit(const Evaluate *) override {
        return Evaluate::make(0);
    }

    Stmt visit(const Provide *) override {
        return Evaluate::make(0);
    }

    Stmt visit(const Store *op) override {
        if (starts_with(op->name, func + ".folding_semaphore.") && ends_with(op->name, ".head")) {
            // This is a counter associated with the producer side of a storage-folding semaphore. Keep it.
            return op;
        } else {
            return Evaluate::make(0);
        }
    }

    Stmt visit(const AssertStmt *) override {
        return Evaluate::make(0);
    }

    Stmt visit(const Prefetch *) override {
        return Evaluate::make(0);
    }

    Stmt visit(const Acquire *op) override {
        Stmt body = mutate(op->body);
        const Variable *var = op->semaphore.as<Variable>();
        internal_assert(var);
        if (is_no_op(body)) {
            return body;
        } else if (starts_with(var->name, func + ".folding_semaphore.")) {
            // This is a storage-folding semaphore for the func we're producing. Keep it.
            return Acquire::make(op->semaphore, op->count, body);
        } else {
            // This semaphore will end up on both sides of the fork,
            // so we'd better duplicate it.
            vector<string> &clones = cloned_acquires[var->name];
            clones.push_back(var->name + unique_name('_'));
            return Acquire::make(Variable::make(type_of<halide_semaphore_t *>(), clones.back()), op->count, body);
        }
    }

    Stmt visit(const Atomic *op) override {
        return Evaluate::make(0);
    }

    Expr visit(const Call *op) override {
        if (op->name == "halide_semaphore_init") {
            internal_assert(op->args.size() == 2);
            const Variable *var = op->args[0].as<Variable>();
            internal_assert(var);
            inner_semaphores.insert(var->name);
        }
        return op;
    }

    map<string, vector<string>> &cloned_acquires;
    set<string> inner_semaphores;

public:
    GenerateProducerBody(const string &f, const vector<Expr> &s, map<string, vector<string>> &a)
        : func(f), sema(s), cloned_acquires(a) {
    }
};

class GenerateConsumerBody : public NoOpCollapsingMutator {
    const string &func;
    vector<Expr> sema;

    using NoOpCollapsingMutator::visit;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->name == func) {
            if (op->is_producer) {
                // Remove the work entirely
                return Evaluate::make(0);
            } else {
                // Synchronize on the work done by the producer before beginning consumption
                Expr acquire_sema = sema.back();
                sema.pop_back();
                return Acquire::make(acquire_sema, 1, op);
            }
        } else {
            return NoOpCollapsingMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        // Don't want to keep the producer's storage-folding tracker - it's dead code on the consumer side
        if (starts_with(op->name, func + ".folding_semaphore.") && ends_with(op->name, ".head")) {
            return mutate(op->body);
        } else {
            return NoOpCollapsingMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (starts_with(op->name, func + ".folding_semaphore.") && ends_with(op->name, ".head")) {
            return Evaluate::make(0);
        } else {
            return NoOpCollapsingMutator::visit(op);
        }
    }

    Stmt visit(const Acquire *op) override {
        // Don't want to duplicate any semaphore acquires.
        // Ones from folding should go to the producer side.
        const Variable *var = op->semaphore.as<Variable>();
        internal_assert(var);
        if (starts_with(var->name, func + ".folding_semaphore.")) {
            return mutate(op->body);
        } else {
            return NoOpCollapsingMutator::visit(op);
        }
    }

public:
    GenerateConsumerBody(const string &f, const vector<Expr> &s)
        : func(f), sema(s) {
    }
};

class CloneAcquire : public IRMutator {
    using IRMutator::visit;

    const string &old_name;
    Expr new_var;

    Stmt visit(const Evaluate *op) override {
        const Call *call = op->value.as<Call>();
        const Variable *var = ((call && !call->args.empty()) ? call->args[0].as<Variable>() : nullptr);
        if (var && var->name == old_name &&
            (call->name == "halide_semaphore_release" ||
             call->name == "halide_semaphore_init")) {
            vector<Expr> args = call->args;
            args[0] = new_var;
            Stmt new_stmt =
                Evaluate::make(Call::make(call->type, call->name, args, call->call_type));
            return Block::make(op, new_stmt);
        } else {
            return op;
        }
    }

public:
    CloneAcquire(const string &o, const string &new_name)
        : old_name(o) {
        new_var = Variable::make(type_of<halide_semaphore_t *>(), new_name);
    }
};

class CountConsumeNodes : public IRVisitor {
    const string &func;

    using IRVisitor::visit;

    void visit(const ProducerConsumer *op) override {
        if (op->name == func && !op->is_producer) {
            count++;
        }
        IRVisitor::visit(op);
    }

public:
    CountConsumeNodes(const string &f)
        : func(f) {
    }
    int count = 0;
};

class ForkAsyncProducers : public IRMutator {
    using IRMutator::visit;

    const map<string, Function> &env;

    map<string, vector<string>> cloned_acquires;

    Stmt visit(const Realize *op) override {
        auto it = env.find(op->name);
        internal_assert(it != env.end());
        Function f = it->second;
        if (f.schedule().async()) {
            Stmt body = op->body;

            // Make two copies of the body, one which only does the
            // producer, and one which only does the consumer. Inject
            // synchronization to preserve dependencies. Put them in a
            // task-parallel block.

            // Make a semaphore per consume node
            CountConsumeNodes consumes(op->name);
            body.accept(&consumes);

            vector<string> sema_names;
            vector<Expr> sema_vars;
            for (int i = 0; i < consumes.count; i++) {
                sema_names.push_back(op->name + ".semaphore_" + std::to_string(i));
                sema_vars.push_back(Variable::make(type_of<halide_semaphore_t *>(), sema_names.back()));
            }

            Stmt producer = GenerateProducerBody(op->name, sema_vars, cloned_acquires).mutate(body);
            Stmt consumer = GenerateConsumerBody(op->name, sema_vars).mutate(body);

            // Recurse on both sides
            producer = mutate(producer);
            consumer = mutate(consumer);

            // Run them concurrently
            body = Fork::make(producer, consumer);

            for (const string &sema_name : sema_names) {
                // Make a semaphore on the stack
                Expr sema_space = Call::make(type_of<halide_semaphore_t *>(), "halide_make_semaphore",
                                             {0}, Call::Extern);

                // If there's a nested async producer, we may have
                // recursively cloned this semaphore inside the mutation
                // of the producer and consumer.
                const vector<string> &clones = cloned_acquires[sema_name];
                for (const auto &i : clones) {
                    body = CloneAcquire(sema_name, i).mutate(body);
                    body = LetStmt::make(i, sema_space, body);
                }

                body = LetStmt::make(sema_name, sema_space, body);
            }

            return Realize::make(op->name, op->types, op->memory_type,
                                 op->bounds, op->condition, body);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    ForkAsyncProducers(const map<string, Function> &e)
        : env(e) {
    }
};

// Lowers semaphore initialization from a call to
// "halide_make_semaphore" to an alloca followed by a call into the
// runtime to initialize. If something crashes before releasing a
// semaphore, the task system is responsible for propagating the
// failure to all branches of the fork. This depends on all semaphore
// acquires happening as part of the halide_do_parallel_tasks logic,
// not via explicit code in the closure.  The current design for this
// does not propagate failures downward to subtasks of a failed
// fork. It assumes these will be able to reach completion in spite of
// the failure, which remains to be proven. (There is a test for the
// simple failure case, error_async_require_fail. One has not been
// written for the complex nested case yet.)
class InitializeSemaphores : public IRMutator {
    using IRMutator::visit;

    const Type sema_type = type_of<halide_semaphore_t *>();

    Stmt visit(const LetStmt *op) override {
        vector<const LetStmt *> frames;

        // Find first op that is of sema_type
        while (op && op->value.type() != sema_type) {
            frames.push_back(op);
            op = op->body.as<LetStmt>();
        }

        Stmt body;
        if (op) {
            body = mutate(op->body);
            // Peel off any enclosing let expressions from the value
            vector<pair<string, Expr>> lets;
            Expr value = op->value;
            while (const Let *l = value.as<Let>()) {
                lets.emplace_back(l->name, l->value);
                value = l->body;
            }
            const Call *call = value.as<Call>();
            if (call && call->name == "halide_make_semaphore") {
                internal_assert(call->args.size() == 1);

                Expr sema_var = Variable::make(sema_type, op->name);
                Expr sema_init = Call::make(Int(32), "halide_semaphore_init",
                                            {sema_var, call->args[0]}, Call::Extern);
                Expr sema_allocate = Call::make(sema_type, Call::alloca,
                                                {(int)sizeof(halide_semaphore_t)}, Call::Intrinsic);
                body = Block::make(Evaluate::make(sema_init), std::move(body));
                body = LetStmt::make(op->name, std::move(sema_allocate), std::move(body));

                // Re-wrap any other lets
                for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                    body = LetStmt::make(it->first, it->second, std::move(body));
                }
            }
        } else {
            body = mutate(frames.back()->body);
        }

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            Expr value = mutate((*it)->value);
            if (value.same_as((*it)->value) && body.same_as((*it)->body)) {
                body = *it;
            } else {
                body = LetStmt::make((*it)->name, std::move(value), std::move(body));
            }
        }
        return body;
    }

    Expr visit(const Call *op) override {
        internal_assert(op->name != "halide_make_semaphore")
            << "Call to halide_make_semaphore in unexpected place\n";
        return op;
    }
};

// Tighten the scope of consume nodes as much as possible to avoid needless synchronization.
class TightenProducerConsumerNodes : public IRMutator {
    using IRMutator::visit;

    Stmt make_producer_consumer(const string &name, bool is_producer, Stmt body, const Scope<int> &scope) {
        if (const LetStmt *let = body.as<LetStmt>()) {
            Stmt orig = body;
            // 'orig' is only used to keep a reference to the let
            // chain in scope. We're going to be keeping pointers to
            // LetStmts we peeled off 'body' while also mutating
            // 'body', which is probably the only reference counted
            // object that keeps those pointers live.

            // Peel off all lets that don't depend on any vars in scope.
            vector<const LetStmt *> containing_lets;
            while (let && !expr_uses_vars(let->value, scope)) {
                containing_lets.push_back(let);
                body = let->body;
                let = body.as<LetStmt>();
            }

            if (let) {
                // That's as far as we can go
                body = ProducerConsumer::make(name, is_producer, body);
            } else {
                // Recurse onto a non-let-node
                body = make_producer_consumer(name, is_producer, body, scope);
            }

            for (auto it = containing_lets.rbegin(); it != containing_lets.rend(); it++) {
                body = LetStmt::make((*it)->name, (*it)->value, body);
            }

            return body;
        } else if (const Block *block = body.as<Block>()) {
            if (is_producer) {
                // We don't push produce nodes into blocks
                return ProducerConsumer::make(name, is_producer, body);
            }
            vector<Stmt> sub_stmts;
            Stmt rest;
            do {
                Stmt first = block->first;
                sub_stmts.push_back(block->first);
                rest = block->rest;
                block = rest.as<Block>();
            } while (block);
            sub_stmts.push_back(rest);

            for (Stmt &s : sub_stmts) {
                if (stmt_uses_vars(s, scope)) {
                    s = make_producer_consumer(name, is_producer, s, scope);
                }
            }

            return Block::make(sub_stmts);
        } else if (const ProducerConsumer *pc = body.as<ProducerConsumer>()) {
            return ProducerConsumer::make(pc->name, pc->is_producer, make_producer_consumer(name, is_producer, pc->body, scope));
        } else if (const Realize *r = body.as<Realize>()) {
            return Realize::make(r->name, r->types, r->memory_type,
                                 r->bounds, r->condition,
                                 make_producer_consumer(name, is_producer, r->body, scope));
        } else {
            return ProducerConsumer::make(name, is_producer, body);
        }
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body = mutate(op->body);
        Scope<int> scope;
        scope.push(op->name, 0);
        Function f = env.find(op->name)->second;
        if (f.outputs() == 1) {
            scope.push(op->name + ".buffer", 0);
        } else {
            for (int i = 0; i < f.outputs(); i++) {
                scope.push(op->name + "." + std::to_string(i) + ".buffer", 0);
            }
        }
        return make_producer_consumer(op->name, op->is_producer, body, scope);
    }

    const map<string, Function> &env;

public:
    TightenProducerConsumerNodes(const map<string, Function> &e)
        : env(e) {
    }
};

// Broaden the scope of acquire nodes to pack trailing work into the
// same task and to potentially reduce the nesting depth of tasks.
class ExpandAcquireNodes : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Block *op) override {
        // Do an entire sequence of blocks in a single visit method to conserve stack space.
        vector<Stmt> stmts;
        Stmt result;
        do {
            stmts.push_back(mutate(op->first));
            result = op->rest;
        } while ((op = result.as<Block>()));

        result = mutate(result);

        vector<pair<Expr, Expr>> semaphores;
        for (auto it = stmts.rbegin(); it != stmts.rend(); it++) {
            Stmt s = *it;
            while (const Acquire *a = s.as<Acquire>()) {
                semaphores.emplace_back(a->semaphore, a->count);
                s = a->body;
            }
            result = Block::make(s, result);
            while (!semaphores.empty()) {
                result = Acquire::make(semaphores.back().first, semaphores.back().second, result);
                semaphores.pop_back();
            }
        }

        return result;
    }

    Stmt visit(const Realize *op) override {
        Stmt body = mutate(op->body);
        if (const Acquire *a = body.as<Acquire>()) {
            // Don't do the allocation until we have the
            // semaphore. Reduces peak memory use.
            return Acquire::make(a->semaphore, a->count,
                                 mutate(Realize::make(op->name, op->types, op->memory_type,
                                                      op->bounds, op->condition, a->body)));
        } else {
            return Realize::make(op->name, op->types, op->memory_type,
                                 op->bounds, op->condition, body);
        }
    }

    Stmt visit(const LetStmt *op) override {
        Stmt orig = op;
        Stmt body;
        vector<const LetStmt *> frames;
        do {
            frames.push_back(op);
            body = op->body;
            op = body.as<LetStmt>();
        } while (op);

        Stmt s = mutate(body);

        if (const Acquire *a = s.as<Acquire>()) {
            // Pull the acquire node outside as many lets as possible,
            // wrapping them around the Acquire node's original body.
            body = a->body;
            while (!frames.empty() &&
                   !expr_uses_var(a->semaphore, frames.back()->name) &&
                   !expr_uses_var(a->count, frames.back()->name)) {
                body = LetStmt::make(frames.back()->name, frames.back()->value, body);
                frames.pop_back();
            }
            s = Acquire::make(a->semaphore, a->count, body);
        } else if (body.same_as(s)) {
            return orig;
        }

        // Rewrap the rest of the lets
        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            s = LetStmt::make((*it)->name, (*it)->value, s);
        }

        return s;
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body = mutate(op->body);
        if (const Acquire *a = body.as<Acquire>()) {
            return Acquire::make(a->semaphore, a->count,
                                 mutate(ProducerConsumer::make(op->name, op->is_producer, a->body)));
        } else {
            return ProducerConsumer::make(op->name, op->is_producer, body);
        }
    }
};

class TightenForkNodes : public IRMutator {
    using IRMutator::visit;

    Stmt make_fork(const Stmt &first, const Stmt &rest) {
        const LetStmt *lf = first.as<LetStmt>();
        const LetStmt *lr = rest.as<LetStmt>();
        const Realize *rf = first.as<Realize>();
        const Realize *rr = rest.as<Realize>();
        if (lf && lr &&
            lf->name == lr->name &&
            equal(lf->value, lr->value)) {
            return LetStmt::make(lf->name, lf->value, make_fork(lf->body, lr->body));
        } else if (lf && !stmt_uses_var(rest, lf->name)) {
            return LetStmt::make(lf->name, lf->value, make_fork(lf->body, rest));
        } else if (lr && !stmt_uses_var(first, lr->name)) {
            return LetStmt::make(lr->name, lr->value, make_fork(first, lr->body));
        } else if (rf && !stmt_uses_var(rest, rf->name)) {
            return Realize::make(rf->name, rf->types, rf->memory_type,
                                 rf->bounds, rf->condition, make_fork(rf->body, rest));
        } else if (rr && !stmt_uses_var(first, rr->name)) {
            return Realize::make(rr->name, rr->types, rr->memory_type,
                                 rr->bounds, rr->condition, make_fork(first, rr->body));
        } else {
            return Fork::make(first, rest);
        }
    }

    Stmt visit(const Fork *op) override {
        Stmt first, rest;
        {
            ScopedValue<bool> old_in_fork(in_fork, true);
            first = mutate(op->first);
            rest = mutate(op->rest);
        }

        if (is_no_op(first)) {
            return rest;
        } else if (is_no_op(rest)) {
            return first;
        } else {
            return make_fork(first, rest);
        }
    }

    // This is also a good time to nuke any dangling allocations and lets in the fork children.
    Stmt visit(const Realize *op) override {
        Stmt body = mutate(op->body);
        if (in_fork && !stmt_uses_var(body, op->name) && !stmt_uses_var(body, op->name + ".buffer")) {
            return body;
        } else {
            return Realize::make(op->name, op->types, op->memory_type,
                                 op->bounds, op->condition, body);
        }
    }

    Stmt visit(const LetStmt *op) override {
        Stmt body = mutate(op->body);
        if (in_fork && !stmt_uses_var(body, op->name)) {
            return body;
        } else {
            return LetStmt::make(op->name, op->value, body);
        }
    }

    bool in_fork = false;
};

// TODO: merge semaphores?

}  // namespace

Stmt fork_async_producers(Stmt s, const map<string, Function> &env) {
    s = TightenProducerConsumerNodes(env).mutate(s);
    s = ForkAsyncProducers(env).mutate(s);
    s = ExpandAcquireNodes().mutate(s);
    s = TightenForkNodes().mutate(s);
    s = InitializeSemaphores().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
