#include "Closure.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;

Closure::Closure(Stmt s, const string &loop_variable) {
    if (!loop_variable.empty()) {
        ignore.push(loop_variable, 0);
    }
    s.accept(this);
}

void Closure::visit(const Let *op) {
    op->value.accept(this);
    ignore.push(op->name, 0);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const LetStmt *op) {
    op->value.accept(this);
    ignore.push(op->name, 0);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const For *op) {
    ignore.push(op->name, 0);
    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::found_buffer_ref(const string &name, Type type,
                               bool read, bool written, Halide::Buffer<> image) {
    if (!ignore.contains(name)) {
        debug(3) << "Adding buffer " << name << " to closure\n";
        Buffer &ref = buffers[name];
        ref.type = type.element_of(); // TODO: Validate type is the same as existing refs?
        ref.read = read;
        ref.write = written;

        // If reading an image/buffer, compute the size.
        if (image.defined()) {
            ref.size = image.size_in_bytes();
            ref.dimensions = image.dimensions();
        }
    } else {
        debug(3) << "Not adding " << name << " to closure\n";
    }
}

void Closure::visit(const Call *op) {
    if (op->is_intrinsic(Call::address_of)) {
        const Load *load = op->args[0].as<Load>();
        internal_assert(load);
        found_buffer_ref(load->name, load->type, address_of_read, address_of_written, op->image);
    } else if (op->is_intrinsic(Call::copy_memory)) {
        internal_assert(op->args.size() == 3);
        bool old_address_of_read = address_of_read;
        bool old_address_of_written = address_of_written;

        // The destination is first, which is written but not read.
        address_of_read = false;
        address_of_written = true;
        op->args[0].accept(this);

        // The source is second, which is read but not written.
        address_of_read = true;
        address_of_written = false;
        op->args[1].accept(this);

        address_of_read = old_address_of_read;
        address_of_written = old_address_of_written;

        op->args[2].accept(this);
    } else {
        bool old_address_of_written = address_of_written;
        if (!op->is_pure()) {
            // Assume that non-pure calls using an address_of will
            // write to the result of address_of. Reads use the
            // inherited behavior (default true).
            address_of_written = true;
        }
        IRVisitor::visit(op);
        address_of_written = old_address_of_written;
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
    ignore.push(op->name, 0);
    for (size_t i = 0; i < op->extents.size(); i++) {
        op->extents[i].accept(this);
    }
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const Variable *op) {
    if (ignore.contains(op->name)) {
        debug(3) << "Not adding " << op->name << " to closure\n";
    } else {
        debug(3) << "Adding " << op->name << " to closure\n";
        vars[op->name] = op->type;
    }
}

vector<string> Closure::names() const {
    vector<string> res;
    for (const pair<string, Type> &i : vars) {
        debug(2) << "vars:  " << i.first << "\n";
        res.push_back(i.first);
    }
    for (const pair<string, Buffer> &i : buffers) {
        debug(2) << "buffers: " << i.first << "\n";
        res.push_back(i.first + ".host");
        res.push_back(i.first + ".buffer");
    }
    return res;
}

}
}
