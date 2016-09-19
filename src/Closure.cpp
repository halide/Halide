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

void Closure::visit(const Load *op) {
    op->index.accept(this);
    if (!ignore.contains(op->name)) {
        debug(3) << "Adding buffer " << op->name << " to closure\n";
        BufferRef & ref = buffers[op->name];
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
    op->index.accept(this);
    op->value.accept(this);
    if (!ignore.contains(op->name)) {
        debug(3) << "Adding buffer " << op->name << " to closure\n";
        BufferRef & ref = buffers[op->name];
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
    for (const pair<string, BufferRef> &i : buffers) {
        debug(2) << "buffers: " << i.first << "\n";
        res.push_back(i.first + ".host");
        res.push_back(i.first + ".buffer");
    }
    return res;
}

}
}
