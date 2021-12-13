#include "Closure.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;

namespace {
constexpr int DBG = 3;
}  // namespace

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
        debug(DBG) << "Adding buffer " << name << " to closure:\n";
        Buffer &ref = buffers[name];
        ref.type = type.element_of();  // TODO: Validate type is the same as existing refs?
        ref.read = ref.read || read;
        ref.write = ref.write || written;

        // If reading an image/buffer, compute the size.
        if (image.defined()) {
            ref.size = image.size_in_bytes();
            ref.dimensions = image.dimensions();
        }
        debug(DBG) << "   "
                   << " t=" << ref.type
                   << " d=" << (int)ref.dimensions
                   << " r=" << ref.read
                   << " w=" << ref.write
                   << " mt=" << (int)ref.memory_type
                   << " sz=" << ref.size << "\n";
    } else {
        debug(DBG) << "Not adding buffer " << name << " to closure\n";
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
        debug(DBG) << "Not adding var " << op->name << " to closure\n";
    } else {
        debug(DBG) << "Adding var " << op->name << " to closure\n";
        vars[op->name] = op->type;
    }
}

void Closure::visit(const Atomic *op) {
    if (!op->mutex_name.empty()) {
        found_buffer_ref(op->mutex_name, type_of<void *>(), true, true, Halide::Buffer<>());
    }
    op->body.accept(this);
}

Expr Closure::pack_into_struct() const {
    std::vector<Expr> elements;

    for (const auto &b : buffers) {
        Expr ptr_var = Variable::make(type_of<void *>(), b.first);
        elements.emplace_back(ptr_var);
    }
    for (const auto &v : vars) {
        Expr var = Variable::make(v.second, v.first);
        elements.emplace_back(var);
    }

    // Sort by decreasing size, to guarantee the struct is densely packed in
    // memory. We don't actually rely on this, it's just nice to have.
    std::stable_sort(elements.begin(), elements.end(),
                     [&](const Expr &a, const Expr &b) {
                         return a.type().bytes() > b.type().bytes();
                     });

    Expr result = Call::make(Handle(),
                             Call::make_struct, elements, Call::Intrinsic);
    return result;
}

Stmt Closure::unpack_from_struct(const Expr &e, const Stmt &s) const {
    // Use the struct-packing code just to make sure the order of elements is
    // the same.
    Expr packed = pack_into_struct();

    // Make a prototype of the packed struct
    class ReplaceCallArgsWithZero : public IRMutator {
    public:
        using IRMutator::mutate;
        Expr mutate(const Expr &e) override {
            if (!e.as<Call>()) {
                return make_zero(e.type());
            } else {
                return IRMutator::mutate(e);
            }
        }
    } replacer;
    string prototype_name = unique_name("closure_prototype");
    Expr prototype = replacer.mutate(packed);
    Expr prototype_var = Variable::make(Handle(), prototype_name);

    const Call *c = packed.as<Call>();

    Stmt result = s;
    for (int idx = (int)c->args.size() - 1; idx >= 0; idx--) {
        Expr arg = c->args[idx];
        const Variable *var = arg.as<Variable>();
        Expr val = Call::make(var->type,
                              Call::load_typed_struct_member,
                              {e, prototype_var, idx},
                              Call::Intrinsic);
        if (stmt_uses_var(result, var->name)) {
            // If a closure is generated for multiple consuming blocks of IR,
            // then some of those blocks might only need some of the field.
            result = LetStmt::make(var->name, val, result);
        }
    }
    result = LetStmt::make(prototype_name, prototype, result);

    return result;
}

}  // namespace Internal
}  // namespace Halide
