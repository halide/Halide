#include "StripAsserts.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include <set>

namespace Halide {
namespace Internal {

namespace {

bool may_discard(const Expr &e) {
    bool result = true;
    // Extern calls that are side-effecty in the sense that you can't
    // move them around in the IR, but we're free to discard because
    // they're just getters.
    static const std::set<std::string> discardable{
        Call::buffer_get_dimensions,
        Call::buffer_get_min,
        Call::buffer_get_extent,
        Call::buffer_get_stride,
        Call::buffer_get_max,
        Call::buffer_get_host,
        Call::buffer_get_device,
        Call::buffer_get_device_interface,
        Call::buffer_get_shape,
        Call::buffer_get_host_dirty,
        Call::buffer_get_device_dirty,
        Call::buffer_get_type};

    visit_with(e, [&](auto *self, const Call *op) {
        if (!(op->is_pure() ||
              discardable.count(op->name))) {
            result = false;
        } else {
            self->visit_base(op);
        }
    });

    return result;
}

class StripAsserts : public IRMutator {
    using IRMutator::visit;

    // We're going to track which symbols are used so that we can strip lets we
    // don't need after removing the asserts.
    std::set<std::string> used;

    // Drop all assert stmts. Assumes that you don't want any side-effects from
    // the condition.
    Stmt visit(const AssertStmt *op) override {
        return Evaluate::make(0);
    }

    Expr visit(const Variable *op) override {
        used.insert(op->name);
        return op;
    }

    Expr visit(const Load *op) override {
        used.insert(op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        used.insert(op->name);
        return IRMutator::visit(op);
    }

    // Also dead-code eliminate any let stmts wrapped around asserts
    Stmt visit(const LetStmt *op) override {
        Stmt body = mutate(op->body);
        if (is_no_op(body)) {
            if (may_discard(op->value)) {
                return body;
            } else {
                // We visit the value just to keep the used variable set
                // accurate.
                mutate(op->value);
                return Evaluate::make(op->value);
            }
        } else if (body.same_as(op->body)) {
            mutate(op->value);
            return op;
        } else if (may_discard(op->value) && !used.count(op->name)) {
            return body;
        } else {
            mutate(op->value);
            return LetStmt::make(op->name, op->value, body);
        }
    }

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);
        if (first.same_as(op->first) && rest.same_as(op->rest)) {
            return op;
        } else if (is_no_op(rest)) {
            return first;
        } else if (is_no_op(first)) {
            return rest;
        } else {
            return Block::make(first, rest);
        }
    }
};

}  // namespace

Stmt strip_asserts(const Stmt &s) {
    return StripAsserts().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
