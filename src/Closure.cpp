#include "Closure.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;

void Closure::include(const Stmt &s, const string &loop_variable) {
    if (!loop_variable.empty()) {
        ignore.push(loop_variable);
    }
    s.accept(this);
    if (!loop_variable.empty()) {
        ignore.pop(loop_variable);
    }
}

void Closure::visit(const Let *op) {
    op->value.accept(this);
    ScopedBinding<> p(ignore, op->name);
    op->body.accept(this);
}

void Closure::visit(const LetStmt *op) {
    op->value.accept(this);
    ScopedBinding<> p(ignore, op->name);
    op->body.accept(this);
}

void Closure::visit(const For *op) {
    ScopedBinding<> p(ignore, op->name);
    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);
}

void Closure::found_buffer_ref(const string &name, Type type,
                               bool read, bool written, const Halide::Buffer<> &image) {
    if (!ignore.contains(name)) {
        debug(3) << "Adding buffer " << name << " to closure\n";
        Buffer &ref = buffers[name];
        ref.type = type.element_of();  // TODO: Validate type is the same as existing refs?
        ref.read = ref.read || read;
        ref.write = ref.write || written;

        // If reading an image/buffer, compute the size.
        if (image.defined()) {
            ref.size = image.size_in_bytes();
            ref.dimensions = image.dimensions();
        }
    } else {
        debug(3) << "Not adding " << name << " to closure\n";
    }
}

void Closure::visit(const Load *op) {
    op->predicate.accept(this);
    op->index.accept(this);
    found_buffer_ref(op->name, op->type, true, false, op->image);
}

void Closure::visit(const Store *op) {
    op->predicate.accept(this);
    op->index.accept(this);
    op->value.accept(this);
    found_buffer_ref(op->name, op->value.type(), false, true, Halide::Buffer<>());
}

void Closure::visit(const Allocate *op) {
    if (op->new_expr.defined()) {
        op->new_expr.accept(this);
    }
    ScopedBinding<> p(ignore, op->name);
    for (const auto &extent : op->extents) {
        extent.accept(this);
    }
    op->condition.accept(this);
    op->body.accept(this);
}

void Closure::visit(const Variable *op) {
    if (ignore.contains(op->name)) {
        debug(3) << "Not adding " << op->name << " to closure\n";
    } else {
        debug(3) << "Adding " << op->name << " to closure\n";
        vars[op->name] = op->type;
    }
}

void Closure::visit(const Atomic *op) {
    if (!op->mutex_name.empty()) {
        found_buffer_ref(op->mutex_name, type_of<void *>(), true, true, Halide::Buffer<>());
    }
    op->body.accept(this);
}

}  // namespace Internal
}  // namespace Halide
