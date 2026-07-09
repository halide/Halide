#include "LowerStructTypes.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::vector;

namespace {

// Number of pack_struct() args occupied by all fields before field_index.
int flat_start_of_field(const StructTypeInfo &info, int field_index) {
    int start = 0;
    for (int i = 0; i < field_index; i++) {
        start += info.fields[i].array_extent.value_or(1);
    }
    return start;
}

class LowerStructTypesMutator : public IRMutator {
    using IRMutator::visit;

    // Struct-typed Let bindings are tracked here because project_field() needs
    // direct syntactic access to the underlying struct_pack()/Select/Load. Because
    // struct-typed values are never materialized (only individual fields are),
    // inlining it back in at each field() use site costs nothing at runtime.
    Scope<Expr> struct_lets;

    Expr resolve_struct_value(Expr e) const {
        while (const Variable *v = e.as<Variable>()) {
            internal_assert(v->type.is_struct());
            user_assert(struct_lets.contains(v->name))
                << "Struct-typed variable \"" << v->name << "\" is unbound; a struct-typed Expr "
                << "may only be the value of a struct-typed Func/Store, or a direct field() "
                << "argument, in v1.\n";
            e = struct_lets.get(v->name);
        }
        return e;
    }

    static Expr fold_field_of_pack(const Call *pack, const StructTypeInfo &info, int field_index, const Expr &elem_index) {
        const StructField &f = info.fields[field_index];
        int flat_start = flat_start_of_field(info, field_index);
        int extent = f.array_extent.value_or(1);

        if (auto ci = as_const_int(elem_index)) {
            int flat = flat_start + (int)*ci;
            internal_assert(flat >= 0 && flat < (int)pack->args.size());
            return pack->args[flat];
        }

        internal_assert(extent >= 1);
        Expr result = pack->args[flat_start + extent - 1];
        for (int i = extent - 2; i >= 0; i--) {
            result = select(elem_index == i, pack->args[flat_start + i], result);
        }
        return result;
    }

    // Rewrite `field(e, field_index)[elem_index]`, where e is a real (already
    // flattened) struct-typed Load into byte-addressed Loads plus a Reinterpret.
    Expr lower_field_read_from_load(const Load *load, const Type &field_type, int field_base_offset, const Expr &elem_index_in) {
        // A field whose own type is itself Type::Struct (a sub-struct field)
        // can't be read this way: the byte-combine-and-Reinterpret trick
        // below fundamentally assumes field_type's *bits* describe its
        // *bytes* (true for every ordinary scalar type), but a struct's
        // runtime tag is always plain UInt(8) regardless of its real
        // (packed, possibly not power-of-two) byte size. Reinterpret would
        // be asked to reinterpret between mismatched bit widths. Nested
        // struct fields read from a literal pack_struct() go through
        // fold_field_of_pack instead and don't hit this at all; only
        // reading a nested field from a (non-inlined) struct-typed
        // buffer is unsupported.
        user_assert(!field_type.is_struct())
            << "Reading a nested struct field (a field whose own type is Type::Struct) directly "
            << "from a struct-typed buffer/Func that isn't fully inlined is not supported. "
            << "Field \"" << field_type << "\" at byte offset " << field_base_offset
            << " within " << load->name << " was requested this way.\n";

        Expr elem_index = mutate(elem_index_in);
        Expr flat_index = mutate(load->index);
        Type index_type = flat_index.type();
        int elem_bytes = field_type.bytes();

        Expr byte_offset = flat_index * make_const(index_type, load->type.bytes()) +
                           make_const(index_type, field_base_offset) +
                           cast(index_type, elem_index) * make_const(index_type, elem_bytes);

        auto byte_load = [&](int b) {
            Expr idx = b == 0 ? byte_offset : byte_offset + make_const(index_type, b);
            return Load::make(UInt(8), load->name, idx, load->image, load->param,
                              const_true(), ModulusRemainder());
        };

        if (elem_bytes == 1) {
            Expr bl = byte_load(0);
            return field_type == UInt(8) ? bl : Reinterpret::make(field_type, bl);
        }

        Type wide = UInt(8 * elem_bytes);
        Expr combined = cast(wide, byte_load(0));
        for (int i = 1; i < elem_bytes; i++) {
            combined = combined | (cast(wide, byte_load(i)) << (8 * i));
        }
        return Reinterpret::make(field_type, combined);
    }

    // Project field field_index's element elem_index (0 for a scalar
    // field) out of a struct-typed Expr. Recurses through the only shapes a
    // struct-typed Expr can legally have at this point in lowering: a
    // literal struct_pack(), a Select between two struct-typed branches, a
    // struct-typed Let (via struct_lets), or a genuine flattened Load.
    Expr project_field(const Expr &struct_expr, int field_index, const Expr &elem_index) {
        Expr e = resolve_struct_value(struct_expr);
        const StructTypeInfo *info = e.type().struct_type;
        internal_assert(info != nullptr) << "project_field applied to a non-struct-typed Expr.\n";
        internal_assert(field_index >= 0 && field_index < (int)info->fields.size());

        if (const Call *pack = e.as<Call>(); pack != nullptr && pack->is_intrinsic(Call::struct_pack)) {
            return mutate(fold_field_of_pack(pack, *info, field_index, elem_index));
        }

        // e is itself an unresolved nested field() access (e.g. field(field(outer, "inner"), "a"),
        // written as two separate front-end field() calls). We resolve the inner one first, then
        // continue projecting the requested field out of *that* result.
        // This is what makes nested struct fields work.
        if (const Call *nested = e.as<Call>(); nested != nullptr && nested->is_intrinsic(Call::struct_field_read)) {
            auto nested_field_index = as_const_int(nested->args[1]);
            internal_assert(nested_field_index) << "struct_field_read's field index must be a constant.\n";
            Expr resolved_inner = project_field(nested->args[0], (int)*nested_field_index, nested->args[2]);
            return project_field(resolved_inner, field_index, elem_index);
        }

        if (const Select *sel = e.as<Select>()) {
            return Select::make(mutate(sel->condition),
                                project_field(sel->true_value, field_index, elem_index),
                                project_field(sel->false_value, field_index, elem_index));
        }

        if (const Let *let = e.as<Let>()) {
            ScopedBinding<Expr> bind(struct_lets, let->name, let->value);
            return project_field(let->body, field_index, elem_index);
        }

        const Load *load = e.as<Load>();
        user_assert(load != nullptr && load->type.is_struct())
            << "This struct-typed Expr cannot be read with field(): a struct-typed Expr may only "
            << "be a literal pack_struct(), a Select between two struct-typed Exprs, or the direct "
            << "result of calling a struct-typed ImageParam/Buffer/Func, not: " << e << "\n";
        return lower_field_read_from_load(load, info->fields[field_index].type, info->offsets[field_index], elem_index);
    }

protected:
    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::struct_field_read)) {
            // Deliberately don't generically mutate() op->args[0] first: see
            // project_field/resolve_struct_value, which handle its only
            // legal shapes directly. Running it through the generic Call
            // visitor would (correctly, for any *other* context) flag it as
            // a leaked struct-typed Expr, but here it's about to be
            // legitimately consumed, not leaked.
            auto field_index_opt = as_const_int(op->args[1]);
            internal_assert(field_index_opt) << "struct_field_read's field index must be a constant.\n";
            return project_field(op->args[0], (int)*field_index_opt, op->args[2]);
        }

        if (op->is_intrinsic(Call::struct_pack)) {
            // Just mutate the pack elements. The actual packing is handled at
            // consumption time (either by another field or by a Store).
            return IRMutator::visit(op);
        }

        if (op->type.is_struct()) {
            user_error << "Struct-typed Expr \"" << Expr(op) << "\" is not the direct argument of "
                       << "field(), and is not a struct-typed Store's own value. A struct-typed Expr "
                       << "may only appear in one of those two positions -- it can't be passed to an "
                       << "arbitrary intrinsic or stored in a Let generically.\n";
        }

        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (!op->value.type().is_struct()) {
            return IRMutator::visit(op);
        }

        Type struct_t = op->value.type();
        const StructTypeInfo &info = *struct_t.struct_type;
        Expr dest_index = mutate(op->index);
        Type index_type = dest_index.type();
        Expr dest_byte_base = dest_index * make_const(index_type, struct_t.bytes());
        Expr predicate = mutate(op->predicate);

        vector<Stmt> stores;
        for (int field_index = 0; field_index < (int)info.fields.size(); field_index++) {
            const StructField &f = info.fields[field_index];
            // A field whose own type is itself Type::Struct (a nested struct field) can't be
            // split into byte-stores this way: the Reinterpret below fundamentally assumes
            // f.type's *bits* describe its *bytes* (true for every ordinary scalar type), but a
            // struct's runtime tag is always plain UInt(8) regardless of its real (packed,
            // possibly not power-of-two) byte size -- Reinterpret would be asked to reinterpret
            // between mismatched bit widths. Only supported for a fully-inlined struct producer
            // (see lower_field_read_from_load's matching restriction on the read side).
            user_assert(!f.type.is_struct())
                << "Storing a nested struct field (a field whose own type is Type::Struct) as "
                << "part of a materialized (non-inlined) struct-typed Store is not supported. "
                << "Field \"" << f.name << "\" of type " << f.type << " in a Store to "
                << op->name << " was requested this way.\n";
            int extent = f.array_extent.value_or(1);
            int elem_bytes = f.type.bytes();
            for (int e = 0; e < extent; e++) {
                Expr value = project_field(op->value, field_index, make_const(Int(32), e));
                Expr bits = elem_bytes == 1 ? value : Reinterpret::make(UInt(8 * elem_bytes), value);
                Expr elem_byte_base = dest_byte_base +
                                      make_const(index_type, info.offsets[field_index] + e * elem_bytes);
                for (int b = 0; b < elem_bytes; b++) {
                    Expr byte_val = elem_bytes == 1 ? bits : extract_bits(UInt(8), bits, make_const(Int(32), 8 * b));
                    Expr byte_idx = b == 0 ? elem_byte_base : elem_byte_base + make_const(index_type, b);
                    stores.push_back(Store::make(op->name, byte_val, byte_idx, op->param, predicate, ModulusRemainder()));
                }
            }
        }
        return Block::make(stores);
    }

    Expr visit(const Let *op) override {
        if (op->value.type().is_struct()) {
            ScopedBinding bind(struct_lets, op->name, op->value);
            return mutate(op->body);
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Variable *op) override {
        if (op->type.is_struct()) {
            user_assert(struct_lets.contains(op->name))
                << "Struct-typed variable \"" << op->name << "\" is unbound; a struct-typed Expr "
                << "may only be the value of a struct-typed Func/Store, or a direct field() "
                << "argument, in v1.\n";
            return mutate(struct_lets.get(op->name));
        }
        return op;
    }
};

// After LowerStructTypes, no struct-typed node of any kind
// should survive in the Stmt. This is purely defensive.
class CheckNoStructTypesRemain : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    static void fail(const Expr &e, const char *what) {
        internal_error << "Struct-typed " << what << " survived struct type lowering: " << e << "\n";
    }

protected:
    void visit(const Call *op) override {
        if (op->type.is_struct() || op->is_intrinsic(Call::struct_field_read) || op->is_intrinsic(Call::struct_pack)) {
            fail(op, "Call");
        }
        IRGraphVisitor::visit(op);
    }
    void visit(const Load *op) override {
        if (op->type.is_struct()) {
            fail(op, "Load");
        }
        IRGraphVisitor::visit(op);
    }
    void visit(const Variable *op) override {
        if (op->type.is_struct()) {
            fail(op, "Variable");
        }
        IRGraphVisitor::visit(op);
    }
    void visit(const Let *op) override {
        if (op->value.type().is_struct()) {
            fail(op->value, "Let value");
        }
        IRGraphVisitor::visit(op);
    }
    void visit(const Store *op) override {
        if (op->value.type().is_struct()) {
            fail(op->value, "Store value");
        }
        IRGraphVisitor::visit(op);
    }
};

}  // namespace

Stmt lower_struct_types(const Stmt &s) {
    Stmt result = LowerStructTypesMutator()(s);
    CheckNoStructTypesRemain checker;
    result.accept(&checker);
    return result;
}

}  // namespace Internal
}  // namespace Halide
