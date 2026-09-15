#include "Closure.h"
#include "Debug.h"
#include "ExprUsesVar.h"
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
    op->max.accept(this);
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

std::vector<Closure::ClosureField> Closure::sorted_fields() const {
    std::vector<ClosureField> fields;
    fields.reserve(buffers.size() + vars.size());
    for (const auto &b : buffers) {
        fields.push_back({b.first, type_of<void *>()});
    }
    for (const auto &v : vars) {
        fields.push_back({v.first, v.second});
    }

    // Sort by decreasing size, to guarantee the struct is densely packed in
    // memory. We don't actually rely on this, it's just nice to have.
    std::stable_sort(fields.begin(), fields.end(),
                     [](const ClosureField &a, const ClosureField &b) {
                         return a.type.bytes() > b.type.bytes();
                     });
    return fields;
}

Type Closure::struct_type() const {
    std::vector<StructField> struct_fields;
    for (const auto &f : sorted_fields()) {
        struct_fields.push_back({f.name, f.type});
    }
    return Type::Struct(struct_fields);
}

Stmt Closure::pack_into_struct(const std::string &alloc_name, const Stmt &body) const {
    std::vector<ClosureField> fields = sorted_fields();
    if (fields.empty()) {
        // Nothing captured: bind the closure pointer to null rather than
        // allocating a (disallowed) zero-field struct.
        return LetStmt::make(alloc_name, reinterpret(Handle(), make_zero(UInt(64))), body);
    }

    std::vector<StructField> struct_fields;
    std::vector<Expr> values;
    struct_fields.reserve(fields.size());
    values.reserve(fields.size());
    for (const auto &f : fields) {
        struct_fields.push_back({f.name, f.type});
        values.push_back(Variable::make(f.type, f.name));
    }
    Type struct_t = Type::Struct(struct_fields);

    Stmt store = Store::make(alloc_name, pack_struct(struct_t, values), make_zero(Int(32)),
                             Parameter(), const_true(), ModulusRemainder(), false);
    Stmt result = Block::make(store, body);
    return Allocate::make(alloc_name, struct_t, MemoryType::Stack, {1}, const_true(), result);
}

Stmt Closure::unpack_from_struct(const Expr &e, const Stmt &s) const {
    std::vector<ClosureField> fields = sorted_fields();
    if (fields.empty()) {
        return s;
    }

    const Variable *ptr = e.as<Variable>();
    internal_assert(ptr) << "Closure::unpack_from_struct expects a Variable naming the "
                         << "pointer to the packed struct, not: " << e << "\n";

    std::vector<StructField> struct_fields;
    struct_fields.reserve(fields.size());
    for (const auto &f : fields) {
        struct_fields.push_back({f.name, f.type});
    }
    Type struct_t = Type::Struct(struct_fields);

    Expr struct_value = Load::make(struct_t, ptr->name, make_zero(Int(32)),
                                   Halide::Buffer<>(), Parameter(),
                                   const_true(), ModulusRemainder(), false);

    // If a closure is generated for multiple consuming blocks of IR, then some
    // of those blocks might only need some of the fields, so only bind the ones
    // that are used.
    std::vector<std::pair<std::string, Expr>> lets;
    lets.reserve(fields.size());
    for (int idx = 0; idx < (int)fields.size(); idx++) {
        lets.emplace_back(fields[idx].name, field(struct_value, idx));
    }
    return rewrap_used_lets(s, lets);
}

}  // namespace Internal
}  // namespace Halide
