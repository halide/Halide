#include "AsyncProducers.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::set;
using std::pair;
using std::string;
using std::map;

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
                                  op->new_expr, op->free_function);
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
            string cloned_acquire = var->name + unique_name('_');
            cloned_acquires[var->name] = cloned_acquire;
            return Acquire::make(Variable::make(type_of<halide_semaphore_t *>(), cloned_acquire), op->count, body);
        }
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

    map<string, string> &cloned_acquires;
    set<string> inner_semaphores;

public:
    GenerateProducerBody(const string &f, const vector<Expr> &s, map<string, string> &a) :
        func(f), sema(s), cloned_acquires(a) {
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
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        // Don't want to keep the producer's storage-folding tracker - it's dead code on the consumer side
        if (starts_with(op->name, func + ".folding_semaphore.") && ends_with(op->name, ".head")) {
            return mutate(op->body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (starts_with(op->name, func + ".folding_semaphore.") && ends_with(op->name, ".head")) {
            return Evaluate::make(0);
        } else {
            return IRMutator::visit(op);
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
            return IRMutator::visit(op);
        }
    }

public:
    GenerateConsumerBody(const string &f, const vector<Expr> &s) :
        func(f), sema(s) {}
};

class CloneAcquire : public IRMutator {
    using IRMutator::visit;

    const string &old_name;
    Expr new_var;

    Stmt visit(const Evaluate *op) override {
        const Call *call = op->value.as<Call>();
        const Variable *var = ((call && !call->args.empty()) ?
                               call->args[0].as<Variable>() :
                               nullptr);
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
    CloneAcquire(const string &o, const string &new_name) : old_name(o) {
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
    CountConsumeNodes(const string &f) : func(f) {}
    int count = 0;
};

class ForkAsyncProducers : public IRMutator {
    using IRMutator::visit;

    const map<string, Function> &env;

    map<string, string> cloned_acquires;

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
                sema_vars.push_back(Variable::make(Handle(), sema_names.back()));
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
                auto it = cloned_acquires.find(sema_name);
                if (it != cloned_acquires.end()) {
                    body = CloneAcquire(sema_name, it->second).mutate(body);
                    body = LetStmt::make(it->second, sema_space, body);
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
    ForkAsyncProducers(const map<string, Function> &e) : env(e) {}
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

    Stmt make_producer_consumer(string name, bool is_producer, Stmt body, const Scope<int> &scope) {
        if (const LetStmt *let = body.as<LetStmt>()) {
            if (expr_uses_vars(let->value, scope)) {
                return ProducerConsumer::make(name, is_producer, body);
            } else {
                return LetStmt::make(let->name, let->value, make_producer_consumer(name, is_producer, let->body, scope));
            }
        } else if (const Block *block = body.as<Block>()) {
            // Check which sides it's used on
            bool first = stmt_uses_vars(block->first, scope);
            bool rest = stmt_uses_vars(block->rest, scope);
            if (is_producer) {
                return ProducerConsumer::make(name, is_producer, body);
            } else if (first && rest) {
                return Block::make(make_producer_consumer(name, is_producer, block->first, scope),
                                   make_producer_consumer(name, is_producer, block->rest, scope));
            } else if (first) {
                return Block::make(make_producer_consumer(name, is_producer, block->first, scope), block->rest);
            } else if (rest) {
                return Block::make(block->first, make_producer_consumer(name, is_producer, block->rest, scope));
            } else {
                // Used on neither side?!
                return body;
            }
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
    TightenProducerConsumerNodes(const map<string, Function> &e) : env(e) {}
};

// Broaden the scope of acquire nodes to pack trailing work into the
// same task and to potentially reduce the nesting depth of tasks.
class ExpandAcquireNodes : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first), rest = mutate(op->rest);
        if (const Acquire *a = first.as<Acquire>()) {
            // May as well nest the rest stmt inside the acquire
            // node. It's also blocked on it.
            return Acquire::make(a->semaphore, a->count,
                                 mutate(Block::make(a->body, op->rest)));
        } else {
            return Block::make(first, rest);
        }
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
        Stmt body = mutate(op->body);
        const Acquire *a = body.as<Acquire>();
        if (a &&
            !expr_uses_var(a->semaphore, op->name) &&
            !expr_uses_var(a->count, op->name)) {
            return Acquire::make(a->semaphore, a->count,
                                 LetStmt::make(op->name, op->value, a->body));
        } else {
            return LetStmt::make(op->name, op->value, body);
        }
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

    Stmt make_fork(Stmt first, Stmt rest) {
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

Stmt fork_async_producers(Stmt s, const map<string, Function> &env) {
    s = TightenProducerConsumerNodes(env).mutate(s);
    s = ForkAsyncProducers(env).mutate(s);
    s = ExpandAcquireNodes().mutate(s);
    s = TightenForkNodes().mutate(s);
    s = InitializeSemaphores().mutate(s);
    return s;
}

}
}
