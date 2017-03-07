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

void Closure::visit(const Call *op) {
    if (op->is_intrinsic(Call::address_of)) {
        const Load *load = op->args[0].as<Load>();
        internal_assert(load);
        if (!ignore.contains(load->name)) {
            debug(3) << "Adding buffer " << load->name << " to closure\n";
            Buffer &ref = buffers[load->name];
            ref.type = load->type.element_of(); // TODO: Validate type is the same as existing refs?
            ref.read = address_of_read;
            ref.write = address_of_written;

            // If reading an image/buffer, compute the size.
            if (op->image.defined()) {
                ref.size = op->image.size_in_bytes();
                ref.dimensions = op->image.dimensions();
            }
        } else {
            debug(3) << "Not adding " << load->name << " to closure\n";
        }
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
            address_of_written = true;
        }
        IRVisitor::visit(op);
        address_of_written = old_address_of_written;
    }
}

void Closure::visit(const Load *op) {
    op->predicate.accept(this);
    op->index.accept(this);
    if (!ignore.contains(op->name)) {
        debug(3) << "Adding buffer " << op->name << " to closure\n";
        Buffer & ref = buffers[op->name];
        ref.type = op->type.element_of(); // TODO: Validate type is the same as existing refs?
        ref.read = true;

        // If reading an image/buffer, compute the size.
        if (op->image.defined()) {
            ref.size = op->image.size_in_bytes();
            ref.dimensions = op->image.dimensions();
        }
    } else {
        debug(3) << "Not adding " << op->name << " to closure\n";
    }
}

void Closure::visit(const Store *op) {
    op->predicate.accept(this);
    op->index.accept(this);
    op->value.accept(this);
    if (!ignore.contains(op->name)) {
        debug(3) << "Adding buffer " << op->name << " to closure\n";
        Buffer & ref = buffers[op->name];
        ref.type = op->value.type().element_of(); // TODO: Validate type is the same as existing refs?
        // TODO: do we need to set ref.dimensions?
        ref.write = true;
    } else {
        debug(3) << "Not adding " << op->name << " to closure\n";
    }
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
