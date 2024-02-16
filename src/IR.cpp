#include "IR.h"

#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include <numeric>
#include <utility>

namespace Halide {
namespace Internal {

namespace {
template<typename T>
struct AllocateResult {
    T *node;
    Expr *exprs;
    char *chars;
};

template<typename T>
AllocateResult<T> allocate_ir_node(int num_exprs, int num_chars) {
    AllocateResult<T> result;
    constexpr size_t base_bytes = sizeof(T);
    const size_t expr_bytes = num_exprs * sizeof(Expr);
    static_assert(sizeof(T) % sizeof(Expr) == 0);
    const size_t char_bytes = num_chars;
    uint8_t *mem = (uint8_t *)malloc(base_bytes + expr_bytes + char_bytes);

    result.node = (T *)mem;
    new (mem) T;
    result.exprs = (Expr *)(mem + base_bytes);
    result.chars = (char *)(mem + base_bytes + expr_bytes);
    // Zero-initialization is fine for Exprs and chars
    memset(result.exprs, 0, expr_bytes + char_bytes);
    return result;
}

template<typename T>
AllocateResult<T> allocate_named_ir_node(int num_exprs, std::string_view name) {
    AllocateResult<T> result;
    constexpr size_t base_bytes = sizeof(T);
    const size_t expr_bytes = num_exprs * sizeof(Expr);
    static_assert(sizeof(T) % sizeof(Expr) == 0);
    const size_t char_bytes = name.size();
    uint8_t *mem = (uint8_t *)malloc(base_bytes + expr_bytes + char_bytes);

    result.node = (T *)mem;
    new (mem) T;
    result.exprs = (Expr *)(mem + base_bytes);
    result.chars = (char *)(mem + base_bytes + expr_bytes);
    memset(result.exprs, 0, expr_bytes);
    result.node->name = std::string_view(result.chars, name.size());
    memcpy(result.chars, name.data(), name.size());
    return result;
}

template<typename T>
Expr make_binary_op(Expr a, Expr b) {
    auto mem = allocate_ir_node<T>(0, 0);
    mem.node->type = a.type();
    mem.node->a = std::move(a);
    mem.node->b = std::move(b);
    return mem.node;
}

template<typename T>
Expr make_comparison_op(Expr a, Expr b) {
    auto mem = allocate_ir_node<T>(0, 0);
    mem.node->type = Bool(a.type().lanes());
    mem.node->a = std::move(a);
    mem.node->b = std::move(b);
    return mem.node;
}

}  // namespace

Expr Cast::make(Type t, Expr v) {
    internal_assert(v.defined()) << "Cast of undefined\n";
    internal_assert(t.lanes() == v.type().lanes()) << "Cast may not change vector widths\n";

    auto mem = allocate_ir_node<Cast>(0, 0);
    mem.node->type = t;
    mem.node->value = std::move(v);
    return mem.node;
}

Expr Reinterpret::make(Type t, Expr v) {
    user_assert(v.defined()) << "reinterpret of undefined Expr\n";
    int from_bits = v.type().bits() * v.type().lanes();
    int to_bits = t.bits() * t.lanes();
    user_assert(from_bits == to_bits)
        << "Reinterpret cast from type " << v.type()
        << " which has " << from_bits
        << " bits, to type " << t
        << " which has " << to_bits << " bits\n";

    auto mem = allocate_ir_node<Reinterpret>(0, 0);
    mem.node->type = t;
    mem.node->value = std::move(v);
    return mem.node;
}

Expr Add::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Add of undefined\n";
    internal_assert(b.defined()) << "Add of undefined\n";
    internal_assert(a.type() == b.type()) << "Add of mismatched types\n";

    return make_binary_op<Add>(std::move(a), std::move(b));
}

Expr Sub::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Sub of undefined\n";
    internal_assert(b.defined()) << "Sub of undefined\n";
    internal_assert(a.type() == b.type()) << "Sub of mismatched types\n";

    return make_binary_op<Sub>(std::move(a), std::move(b));
}

Expr Mul::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Mul of undefined\n";
    internal_assert(b.defined()) << "Mul of undefined\n";
    internal_assert(a.type() == b.type()) << "Mul of mismatched types\n";

    return make_binary_op<Mul>(std::move(a), std::move(b));
}

Expr Div::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Div of undefined\n";
    internal_assert(b.defined()) << "Div of undefined\n";
    internal_assert(a.type() == b.type()) << "Div of mismatched types\n";

    return make_binary_op<Div>(std::move(a), std::move(b));
}

Expr Mod::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Mod of undefined\n";
    internal_assert(b.defined()) << "Mod of undefined\n";
    internal_assert(a.type() == b.type()) << "Mod of mismatched types\n";

    return make_binary_op<Mod>(std::move(a), std::move(b));
}

Expr Min::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Min of undefined\n";
    internal_assert(b.defined()) << "Min of undefined\n";
    internal_assert(a.type() == b.type()) << "Min of mismatched types\n";

    return make_binary_op<Min>(std::move(a), std::move(b));
}

Expr Max::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Max of undefined\n";
    internal_assert(b.defined()) << "Max of undefined\n";
    internal_assert(a.type() == b.type()) << "Max of mismatched types\n";

    return make_binary_op<Max>(std::move(a), std::move(b));
}

Expr EQ::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "EQ of undefined\n";
    internal_assert(b.defined()) << "EQ of undefined\n";
    internal_assert(a.type() == b.type()) << "EQ of mismatched types\n";

    return make_comparison_op<EQ>(std::move(a), std::move(b));
}

Expr NE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "NE of undefined\n";
    internal_assert(b.defined()) << "NE of undefined\n";
    internal_assert(a.type() == b.type()) << "NE of mismatched types\n";

    return make_comparison_op<NE>(std::move(a), std::move(b));
}

Expr LT::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "LT of undefined\n";
    internal_assert(b.defined()) << "LT of undefined\n";
    internal_assert(a.type() == b.type()) << "LT of mismatched types\n";

    return make_comparison_op<LT>(std::move(a), std::move(b));
}

Expr LE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "LE of undefined\n";
    internal_assert(b.defined()) << "LE of undefined\n";
    internal_assert(a.type() == b.type()) << "LE of mismatched types\n";

    return make_comparison_op<LE>(std::move(a), std::move(b));
}

Expr GT::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "GT of undefined\n";
    internal_assert(b.defined()) << "GT of undefined\n";
    internal_assert(a.type() == b.type()) << "GT of mismatched types\n";

    return make_comparison_op<GT>(std::move(a), std::move(b));
}

Expr GE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "GE of undefined\n";
    internal_assert(b.defined()) << "GE of undefined\n";
    internal_assert(a.type() == b.type()) << "GE of mismatched types\n";

    return make_comparison_op<GE>(std::move(a), std::move(b));
}

Expr And::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "And of undefined\n";
    internal_assert(b.defined()) << "And of undefined\n";
    internal_assert(a.type().is_bool()) << "lhs of And is not a bool\n";
    internal_assert(b.type().is_bool()) << "rhs of And is not a bool\n";
    internal_assert(a.type() == b.type()) << "And of mismatched types\n";

    return make_binary_op<And>(std::move(a), std::move(b));
}

Expr Or::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Or of undefined\n";
    internal_assert(b.defined()) << "Or of undefined\n";
    internal_assert(a.type().is_bool()) << "lhs of Or is not a bool\n";
    internal_assert(b.type().is_bool()) << "rhs of Or is not a bool\n";
    internal_assert(a.type() == b.type()) << "Or of mismatched types\n";

    return make_binary_op<Or>(std::move(a), std::move(b));
}

Expr Not::make(Expr a) {
    internal_assert(a.defined()) << "Not of undefined\n";
    internal_assert(a.type().is_bool()) << "argument of Not is not a bool\n";

    auto mem = allocate_ir_node<Not>(0, 0);
    mem.node->type = Bool(a.type().lanes());
    mem.node->a = std::move(a);
    return mem.node;
}

Expr Select::make(Expr condition, Expr true_value, Expr false_value) {
    internal_assert(condition.defined()) << "Select of undefined\n";
    internal_assert(true_value.defined()) << "Select of undefined\n";
    internal_assert(false_value.defined()) << "Select of undefined\n";
    internal_assert(condition.type().is_bool()) << "First argument to Select is not a bool: " << condition.type() << "\n";
    internal_assert(false_value.type() == true_value.type()) << "Select of mismatched types\n";
    internal_assert(condition.type().is_scalar() ||
                    condition.type().lanes() == true_value.type().lanes())
        << "In Select, vector lanes of condition must either be 1, or equal to vector lanes of arguments\n";

    auto mem = allocate_ir_node<Select>(0, 0);
    mem.node->type = true_value.type();
    mem.node->condition = std::move(condition);
    mem.node->true_value = std::move(true_value);
    mem.node->false_value = std::move(false_value);
    return mem.node;
}

Expr Load::make(Type type, std::string_view name, Expr index, Buffer<> image, Parameter param, Expr predicate, ModulusRemainder alignment) {
    internal_assert(predicate.defined()) << "Load with undefined predicate\n";
    internal_assert(index.defined()) << "Load of undefined\n";
    internal_assert(type.lanes() == index.type().lanes()) << "Vector lanes of Load must match vector lanes of index\n";
    internal_assert(type.lanes() == predicate.type().lanes())
        << "Vector lanes of Load must match vector lanes of predicate\n";

    auto mem = allocate_named_ir_node<Load>(0, name);
    mem.node->type = type;
    mem.node->predicate = std::move(predicate);
    mem.node->index = std::move(index);
    mem.node->image = std::move(image);
    mem.node->param = std::move(param);
    mem.node->alignment = alignment;
    return mem.node;
}

Expr Ramp::make(Expr base, Expr stride, int lanes) {
    internal_assert(base.defined()) << "Ramp of undefined\n";
    internal_assert(stride.defined()) << "Ramp of undefined\n";
    internal_assert(lanes > 1) << "Ramp of lanes <= 1\n";
    internal_assert(stride.type() == base.type()) << "Ramp of mismatched types\n";

    auto mem = allocate_ir_node<Ramp>(0, 0);
    mem.node->type = base.type().with_lanes(lanes * base.type().lanes());
    mem.node->base = std::move(base);
    mem.node->stride = std::move(stride);
    mem.node->lanes = lanes;
    return mem.node;
}

Expr Broadcast::make(Expr value, int lanes) {
    internal_assert(value.defined()) << "Broadcast of undefined\n";
    internal_assert(lanes != 1) << "Broadcast of lanes 1\n";

    auto mem = allocate_ir_node<Broadcast>(0, 0);
    mem.node->type = value.type().with_lanes(lanes * value.type().lanes());
    mem.node->value = std::move(value);
    mem.node->lanes = lanes;
    return mem.node;
}

Expr Let::make(std::string_view name, Expr value, Expr body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    auto mem = allocate_named_ir_node<Let>(0, name);
    mem.node->type = body.type();
    mem.node->value = std::move(value);
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt LetStmt::make(std::string_view name, Expr value, Stmt body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    auto mem = allocate_named_ir_node<LetStmt>(0, name);
    mem.node->value = std::move(value);
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt AssertStmt::make(Expr condition, Expr message) {
    internal_assert(condition.defined()) << "AssertStmt of undefined\n";
    internal_assert(message.type() == Int(32)) << "AssertStmt message must be an int:" << message << "\n";

    auto mem = allocate_ir_node<AssertStmt>(0, 0);
    mem.node->condition = std::move(condition);
    mem.node->message = std::move(message);
    return mem.node;
}

Stmt ProducerConsumer::make(std::string_view name, bool is_producer, Stmt body) {
    internal_assert(body.defined()) << "ProducerConsumer of undefined\n";

    auto mem = allocate_named_ir_node<ProducerConsumer>(0, name);
    mem.node->is_producer = is_producer;
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt ProducerConsumer::make_produce(std::string_view name, Stmt body) {
    return ProducerConsumer::make(name, true, std::move(body));
}

Stmt ProducerConsumer::make_consume(std::string_view name, Stmt body) {
    return ProducerConsumer::make(name, false, std::move(body));
}

Stmt For::make(std::string_view name,
               Expr min, Expr extent,
               ForType for_type, Partition partition_policy,
               DeviceAPI device_api,
               Stmt body) {
    internal_assert(min.defined()) << "For of undefined\n";
    internal_assert(extent.defined()) << "For of undefined\n";
    internal_assert(min.type() == Int(32)) << "For with non-integer min\n";
    internal_assert(extent.type() == Int(32)) << "For with non-integer extent\n";
    internal_assert(body.defined()) << "For of undefined\n";

    auto mem = allocate_named_ir_node<For>(0, name);
    mem.node->min = std::move(min);
    mem.node->extent = std::move(extent);
    mem.node->for_type = for_type;
    mem.node->partition_policy = partition_policy;
    mem.node->device_api = device_api;
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt Acquire::make(Expr semaphore, Expr count, Stmt body) {
    internal_assert(semaphore.defined()) << "Acquire with undefined semaphore\n";
    internal_assert(body.defined()) << "Acquire with undefined body\n";

    auto mem = allocate_ir_node<Acquire>(0, 0);
    mem.node->semaphore = std::move(semaphore);
    mem.node->count = std::move(count);
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt Store::make(std::string_view name, Expr value, Expr index, Parameter param, Expr predicate, ModulusRemainder alignment) {
    internal_assert(predicate.defined()) << "Store with undefined predicate\n";
    internal_assert(value.defined()) << "Store of undefined\n";
    internal_assert(index.defined()) << "Store of undefined\n";
    internal_assert(value.type().lanes() == index.type().lanes()) << "Vector lanes of Store must match vector lanes of index\n";
    internal_assert(value.type().lanes() == predicate.type().lanes())
        << "Vector lanes of Store must match vector lanes of predicate\n";

    auto mem = allocate_named_ir_node<Store>(0, name);
    mem.node->predicate = std::move(predicate);
    mem.node->value = std::move(value);
    mem.node->index = std::move(index);
    mem.node->param = std::move(param);
    mem.node->alignment = alignment;
    return mem.node;
}

Stmt Provide::make(std::string_view name, ExprVector values, ExprVector args, const Expr &predicate) {
    internal_assert(predicate.defined()) << "Provide with undefined predicate\n";
    internal_assert(!values.empty()) << "Provide of no values\n";
    for (const auto &value : values) {
        internal_assert(value.defined()) << "Provide of undefined value\n";
    }
    for (const auto &arg : args) {
        internal_assert(arg.defined()) << "Provide to undefined location\n";
    }

    auto mem = allocate_named_ir_node<Provide>(0, name);
    mem.node->values = values;
    mem.node->args = args;
    mem.node->predicate = predicate;
    return mem.node;
}

Stmt Allocate::make(std::string_view name, Type type, MemoryType memory_type,
                    ExprVector extents,
                    Expr condition, Stmt body,
                    Expr new_expr, std::string_view free_function, int padding) {
    for (const auto &extent : extents) {
        internal_assert(extent.defined()) << "Allocate of undefined extent\n";
        internal_assert(extent.type().is_scalar() == 1) << "Allocate of vector extent\n";
    }
    internal_assert(body.defined()) << "Allocate of undefined\n";
    internal_assert(condition.defined()) << "Allocate with undefined condition\n";
    internal_assert(condition.type().is_bool()) << "Allocate condition is not boolean\n";
    internal_assert(!(new_expr.defined() && padding))
        << "Allocate nodes with custom new expressions may not have padding\n";

    auto mem = allocate_ir_node<Allocate>(extents.size(), name.size() + free_function.size());
    mem.node->type = type;
    mem.node->name = std::string_view(mem.chars, name.size());
    memcpy(mem.chars, name.data(), name.size());
    mem.node->free_function = std::string_view(mem.chars + name.size(), free_function.size());
    memcpy(mem.chars + name.size(), free_function.data(), free_function.size());
    mem.node->memory_type = memory_type;
    mem.node->extents.ptr = mem.exprs;
    mem.node->extents.num = extents.size();
    for (size_t i = 0; i < extents.size(); i++) {
        new (mem.exprs + i) Expr{extents[i]};
    }
    mem.node->new_expr = std::move(new_expr);
    mem.node->free_function = free_function;
    mem.node->condition = std::move(condition);
    mem.node->padding = padding;
    mem.node->body = std::move(body);
    return mem.node;
}

int32_t Allocate::constant_allocation_size(ExprVector extents, std::string_view name) {
    int64_t result = 1;

    for (const auto &extent : extents) {
        if (const IntImm *int_size = extent.as<IntImm>()) {
            // Check if the individual dimension is > 2^31 - 1. Not
            // currently necessary because it's an int32_t, which is
            // always smaller than 2^31 - 1. If we ever upgrade the
            // type of IntImm but not the maximum allocation size, we
            // should re-enable this.
            /*
            if ((int64_t)int_size->value > (((int64_t)(1)<<31) - 1)) {
                user_error
                    << "Dimension " << i << " for allocation " << name << " has size " <<
                    int_size->value << " which is greater than 2^31 - 1.";
            }
            */
            result *= int_size->value;
            if (result > (static_cast<int64_t>(1) << 31) - 1) {
                user_error
                    << "Total size for allocation " << name
                    << " is constant but exceeds 2^31 - 1.\n";
            }
        } else {
            return 0;
        }
    }

    return static_cast<int32_t>(result);
}

int32_t Allocate::constant_allocation_size() const {
    return Allocate::constant_allocation_size(extents, name);
}

Stmt Free::make(std::string_view name) {
    auto mem = allocate_named_ir_node<Free>(0, name);
    return mem.node;
}

Stmt Realize::make(std::string_view name, const std::vector<Type> &types, MemoryType memory_type, const Region &bounds, Expr condition, Stmt body) {
    for (const auto &bound : bounds) {
        internal_assert(bound.min.defined()) << "Realize of undefined\n";
        internal_assert(bound.extent.defined()) << "Realize of undefined\n";
        internal_assert(bound.min.type().is_scalar()) << "Realize of vector size\n";
        internal_assert(bound.extent.type().is_scalar()) << "Realize of vector size\n";
    }
    internal_assert(body.defined()) << "Realize of undefined\n";
    internal_assert(!types.empty()) << "Realize has empty type\n";
    internal_assert(condition.defined()) << "Realize with undefined condition\n";
    internal_assert(condition.type().is_bool()) << "Realize condition is not boolean\n";

    auto mem = allocate_named_ir_node<Realize>(0, name);
    mem.node->types = types;  // TODO: allocate this inline
    mem.node->memory_type = memory_type;
    mem.node->bounds = bounds;  // TODO: allocate this inline
    mem.node->condition = std::move(condition);
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt Prefetch::make(std::string_view name, const std::vector<Type> &types,
                    const Region &bounds,
                    const PrefetchDirective &prefetch,
                    Expr condition, Stmt body) {
    for (const auto &bound : bounds) {
        internal_assert(bound.min.defined()) << "Prefetch of undefined\n";
        internal_assert(bound.extent.defined()) << "Prefetch of undefined\n";
        internal_assert(bound.min.type().is_scalar()) << "Prefetch of vector size\n";
        internal_assert(bound.extent.type().is_scalar()) << "Prefetch of vector size\n";
    }
    internal_assert(!types.empty()) << "Prefetch has empty type\n";
    internal_assert(body.defined()) << "Prefetch of undefined\n";
    internal_assert(condition.defined()) << "Prefetch with undefined condition\n";
    internal_assert(condition.type().is_bool()) << "Prefetch condition is not boolean\n";

    user_assert(is_pure(prefetch.offset)) << "The offset to the prefetch directive must be pure.";

    auto mem = allocate_named_ir_node<Prefetch>(0, name);
    mem.node->types = types;
    mem.node->bounds = bounds;
    mem.node->prefetch = prefetch;
    mem.node->condition = std::move(condition);
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt Block::make(Stmt first, Stmt rest) {
    internal_assert(first.defined()) << "Block of undefined\n";
    internal_assert(rest.defined()) << "Block of undefined\n";

    auto mem = allocate_ir_node<Block>(0, 0);
    if (const Block *b = first.as<Block>()) {
        // Use a canonical block nesting order
        mem.node->first = b->first;
        mem.node->rest = Block::make(b->rest, std::move(rest));
    } else {
        mem.node->first = std::move(first);
        mem.node->rest = std::move(rest);
    }

    return mem.node;
}

Stmt Block::make(const std::vector<Stmt> &stmts) {
    if (stmts.empty()) {
        return Stmt();
    }
    Stmt result = stmts.back();
    for (size_t i = stmts.size() - 1; i > 0; i--) {
        result = Block::make(stmts[i - 1], result);
    }
    return result;
}

Stmt Fork::make(Stmt first, Stmt rest) {
    internal_assert(first.defined()) << "Fork of undefined\n";
    internal_assert(rest.defined()) << "Fork of undefined\n";

    auto mem = allocate_ir_node<Fork>(0, 0);
    if (const Fork *b = first.as<Fork>()) {
        // Use a canonical fork nesting order
        mem.node->first = b->first;
        mem.node->rest = Fork::make(b->rest, std::move(rest));
    } else {
        mem.node->first = std::move(first);
        mem.node->rest = std::move(rest);
    }

    return mem.node;
}

Stmt IfThenElse::make(Expr condition, Stmt then_case, Stmt else_case) {
    internal_assert(condition.defined() && then_case.defined()) << "IfThenElse of undefined\n";
    // else_case may be null.

    internal_assert(condition.type().is_scalar()) << "IfThenElse with vector condition\n";

    auto mem = allocate_ir_node<IfThenElse>(0, 0);
    mem.node->condition = std::move(condition);
    mem.node->then_case = std::move(then_case);
    mem.node->else_case = std::move(else_case);
    return mem.node;
}

Stmt Evaluate::make(Expr v) {
    internal_assert(v.defined()) << "Evaluate of undefined\n";

    auto mem = allocate_ir_node<Evaluate>(0, 0);
    mem.node->value = std::move(v);
    return mem.node;
}

Expr Call::make(const Function &func, int num_args, const Expr *args, int idx) {
    internal_assert(idx >= 0 &&
                    idx < func.outputs())
        << "Value index out of range in call to halide function\n";
    internal_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Call to undefined halide function\n";
    return make(func.output_types()[(size_t)idx], func.name(), num_args, args, Halide,
                func.get_contents(), idx, Buffer<>(), Parameter());
}

namespace {

const char *const intrinsic_op_names[] = {
    "abs",
    "absd",
    "add_image_checks_marker",
    "alloca",
    "bitwise_and",
    "bitwise_not",
    "bitwise_or",
    "bitwise_xor",
    "bool_to_mask",
    "bundle",
    "call_cached_indirect_function",
    "cast_mask",
    "concat_bits",
    "count_leading_zeros",
    "count_trailing_zeros",
    "debug_to_file",
    "declare_box_touched",
    "div_round_to_zero",
    "dynamic_shuffle",
    "extract_bits",
    "extract_mask_element",
    "get_user_context",
    "gpu_thread_barrier",
    "halving_add",
    "halving_sub",
    "hvx_gather",
    "hvx_scatter",
    "hvx_scatter_acc",
    "hvx_scatter_release",
    "if_then_else",
    "if_then_else_mask",
    "image_load",
    "image_store",
    "lerp",
    "likely",
    "likely_if_innermost",
    "load_typed_struct_member",
    "make_struct",
    "memoize_expr",
    "mod_round_to_zero",
    "mul_shift_right",
    "mux",
    "popcount",
    "prefetch",
    "promise_clamped",
    "random",
    "register_destructor",
    "require",
    "require_mask",
    "return_second",
    "rewrite_buffer",
    "round",
    "rounding_halving_add",
    "rounding_mul_shift_right",
    "rounding_shift_left",
    "rounding_shift_right",
    "saturating_add",
    "saturating_sub",
    "saturating_cast",
    "scatter_gather",
    "select_mask",
    "shift_left",
    "shift_right",
    "signed_integer_overflow",
    "size_of_halide_buffer_t",
    "sliding_window_marker",
    "sorted_avg",
    "strict_float",
    "stringify",
    "undef",
    "unreachable",
    "unsafe_promise_clamped",
    "widen_right_add",
    "widen_right_mul",
    "widen_right_sub",
    "widening_add",
    "widening_mul",
    "widening_shift_left",
    "widening_shift_right",
    "widening_sub",
};

static_assert(sizeof(intrinsic_op_names) / sizeof(intrinsic_op_names[0]) == Call::IntrinsicOpCount,
              "intrinsic_op_names needs attention");

}  // namespace

const char *Call::get_intrinsic_name(IntrinsicOp op) {
    return intrinsic_op_names[op];
}

Expr Call::make(Type type, Call::IntrinsicOp op, int num_args, const Expr *args, CallType call_type,
                FunctionPtr func, int value_index,
                const Buffer<> &image, const Parameter &param) {
    internal_assert(call_type == Call::Intrinsic || call_type == Call::PureIntrinsic);
    return Call::make(type, intrinsic_op_names[op], num_args, args, call_type, std::move(func), value_index, image, std::move(param));
}

Expr Call::make(Type type, std::string_view name, int num_args, const Expr *args, CallType call_type,
                FunctionPtr func, int value_index,
                const Buffer<> &image, const Parameter &param) {
    if (name == intrinsic_op_names[Call::prefetch] && call_type == Call::Intrinsic) {
        internal_assert(num_args % 2 == 0)
            << "Number of args to a prefetch call should be even: {base, offset, extent0, stride0, extent1, stride1, ...}\n";
    }
    for (int i = 0; i < num_args; i++) {
        internal_assert(args[i].defined()) << "Call of " << name << " with argument " << i << " undefined.\n";
    }
    if (call_type == Halide || call_type == Image) {
        for (int i = 0; i < num_args; i++) {
            internal_assert(args[i].type() == Int(32))
                << "Args to call to halide function must be type Int(32)\n";
        }
    }
    if (call_type == Image) {
        internal_assert((param.defined() || image.defined()))
            << "Call node to undefined image\n";
    }

    auto mem = allocate_named_ir_node<Call>(num_args, name);
    mem.node->type = type;
    mem.node->args.ptr = mem.exprs;
    mem.node->args.num = num_args;
    mem.node->call_type = call_type;
    mem.node->func = std::move(func);
    mem.node->value_index = value_index;
    mem.node->image = std::move(image);
    mem.node->param = std::move(param);
    for (int i = 0; i < num_args; i++) {
        new (mem.exprs + i) Expr{args[i]};
    }
    return mem.node;
}

Expr Variable::make(Type type, std::string_view name, Buffer<> image, Parameter param, ReductionDomain reduction_domain) {
    internal_assert(!name.empty());

    auto mem = allocate_named_ir_node<Variable>(0, name);
    mem.node->type = type;
    mem.node->image = std::move(image);
    mem.node->param = std::move(param);
    mem.node->reduction_domain = std::move(reduction_domain);
    return mem.node;
}

Expr Shuffle::make(ExprVector vectors,
                   const std::vector<int> &indices) {
    internal_assert(!vectors.empty()) << "Shuffle of zero vectors.\n";
    internal_assert(!indices.empty()) << "Shufle with zero indices.\n";
    Type element_ty = vectors.front().type().element_of();
    int input_lanes = 0;
    for (const Expr &i : vectors) {
        internal_assert(i.type().element_of() == element_ty) << "Shuffle of vectors of mismatched types.\n";
        input_lanes += i.type().lanes();
    }
    for (int i : indices) {
        internal_assert(0 <= i && i < input_lanes) << "Shuffle vector index out of range: " << i << "\n";
    }

    auto mem = allocate_ir_node<Shuffle>(vectors.size(), 0);
    mem.node->type = element_ty.with_lanes((int)indices.size());
    mem.node->vectors.ptr = mem.exprs;
    mem.node->vectors.num = vectors.size();
    for (size_t i = 0; i < vectors.size(); i++) {
        new (mem.exprs + i) Expr{vectors[i]};
    }
    mem.node->indices = indices;  // TODO: embed this
    return mem.node;
}

Expr Shuffle::make_interleave(ExprVector vectors) {
    internal_assert(!vectors.empty()) << "Interleave of zero vectors.\n";

    if (vectors.size() == 1) {
        return vectors.front();
    }

    int lanes = vectors.front().type().lanes();

    for (const Expr &i : vectors) {
        internal_assert(i.type().lanes() == lanes)
            << "Interleave of vectors with different sizes.\n";
    }

    // TODO: All these indices should be done inline instead of by constructing
    // a vector and then copying it.
    std::vector<int> indices;
    for (int i = 0; i < lanes; i++) {
        for (int j = 0; j < (int)vectors.size(); j++) {
            indices.push_back(j * lanes + i);
        }
    }

    return make(vectors, indices);
}

Expr Shuffle::make_concat(ExprVector vectors) {
    internal_assert(!vectors.empty()) << "Concat of zero vectors.\n";

    if (vectors.size() == 1) {
        return vectors.front();
    }

    std::vector<int> indices;
    int lane = 0;
    for (const auto &vector : vectors) {
        for (int j = 0; j < vector.type().lanes(); j++) {
            indices.push_back(lane++);
        }
    }

    return make(vectors, indices);
}

Expr Shuffle::make_broadcast(Expr vector, int factor) {
    std::vector<int> indices(factor * vector.type().lanes());
    for (int ix = 0; ix < factor; ix++) {
        std::iota(indices.begin() + ix * vector.type().lanes(),
                  indices.begin() + (ix + 1) * vector.type().lanes(), 0);
    }

    return make({std::move(vector)}, indices);
}

Expr Shuffle::make_slice(Expr vector, int begin, int stride, int size) {
    if (begin == 0 && size == vector.type().lanes() && stride == 1) {
        return vector;
    }

    std::vector<int> indices;
    for (int i = 0; i < size; i++) {
        indices.push_back(begin + i * stride);
    }

    return make({std::move(vector)}, indices);
}

Expr Shuffle::make_extract_element(Expr vector, int i) {
    return make_slice(std::move(vector), i, 1, 1);
}

bool Shuffle::is_broadcast() const {
    int lanes = indices.size();
    int factor = broadcast_factor();
    if (factor == 0 || factor >= lanes) {
        return false;
    }
    int broadcasted_lanes = lanes / factor;

    if (broadcasted_lanes < 2 || broadcasted_lanes >= lanes || lanes % broadcasted_lanes != 0) {
        return false;
    }
    for (int i = 0; i < lanes; i++) {
        if (indices[i % broadcasted_lanes] != indices[i]) {
            return false;
        }
    }
    return true;
}

int Shuffle::broadcast_factor() const {
    int lanes = indices.size();
    int broadcasted_lanes = 0;
    for (; broadcasted_lanes < lanes; broadcasted_lanes++) {
        if (indices[broadcasted_lanes] != broadcasted_lanes) {
            break;
        }
    }
    if (broadcasted_lanes > 0) {
        return lanes / broadcasted_lanes;
    } else {
        return 0;
    }
}

bool Shuffle::is_interleave() const {
    int lanes = vectors.front().type().lanes();

    // Don't consider concat of scalars as an interleave.
    if (lanes == 1) {
        return false;
    }

    for (const Expr &i : vectors) {
        if (i.type().lanes() != lanes) {
            return false;
        }
    }

    // Require that we are a complete interleaving.
    if (lanes * vectors.size() != indices.size()) {
        return false;
    }

    for (int i = 0; i < (int)vectors.size(); i++) {
        for (int j = 0; j < lanes; j++) {
            if (indices[j * (int)vectors.size() + i] != i * lanes + j) {
                return false;
            }
        }
    }

    return true;
}

Stmt Atomic::make(std::string_view producer_name,
                  std::string_view mutex_name,
                  Stmt body) {
    auto mem = allocate_ir_node<Atomic>(0, producer_name.size() + mutex_name.size());

    mem.node->producer_name = std::string_view(mem.chars, producer_name.size());
    memcpy(mem.chars, producer_name.data(), producer_name.size());
    mem.node->mutex_name = std::string_view(mem.chars + producer_name.size(), mutex_name.size());
    memcpy(mem.chars + producer_name.size(), mutex_name.data(), mutex_name.size());
    internal_assert(body.defined()) << "Atomic must have a body statement.\n";
    mem.node->body = std::move(body);
    return mem.node;
}

Stmt HoistedStorage::make(std::string_view name,
                          Stmt body) {
    internal_assert(body.defined()) << "HoistedStorage must have a body statement.\n";
    auto mem = allocate_named_ir_node<HoistedStorage>(0, name);
    mem.node->body = std::move(body);
    return mem.node;
}

Expr VectorReduce::make(VectorReduce::Operator op,
                        Expr vec,
                        int lanes) {
    if (vec.type().is_bool()) {
        internal_assert(op == VectorReduce::And || op == VectorReduce::Or)
            << "The only legal operators for VectorReduce on a Bool"
            << "vector are VectorReduce::And and VectorReduce::Or\n";
    }
    internal_assert(!vec.type().is_handle()) << "VectorReduce of handle type";
    // Check the output lanes is a factor of the input lanes. They can
    // also both be zero if we're constructing a wildcard expression.
    internal_assert((lanes == 0 && vec.type().lanes() == 0) ||
                    (lanes != 0 && (vec.type().lanes() % lanes == 0)))
        << "Vector reduce output lanes must be a divisor of the number of lanes in the argument "
        << lanes << " " << vec.type().lanes() << "\n";

    auto mem = allocate_ir_node<VectorReduce>(0, 0);
    mem.node->type = vec.type().with_lanes(lanes);
    mem.node->op = op;
    mem.node->value = std::move(vec);
    return mem.node;
}

namespace {

// Helper function to determine if a sequence of indices is a
// contiguous ramp.
bool is_ramp(const std::vector<int> &indices, int stride = 1) {
    for (size_t i = 0; i + 1 < indices.size(); i++) {
        if (indices[i + 1] != indices[i] + stride) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool Shuffle::is_concat() const {
    size_t input_lanes = 0;
    for (const Expr &i : vectors) {
        input_lanes += i.type().lanes();
    }

    // A concat is a ramp where the output has the same number of
    // lanes as the input.
    return indices.size() == input_lanes && is_ramp(indices);
}

bool Shuffle::is_slice() const {
    size_t input_lanes = 0;
    for (const Expr &i : vectors) {
        input_lanes += i.type().lanes();
    }

    // A slice is a ramp where the output does not contain all of the
    // lanes of the input.
    return indices.size() < input_lanes && is_ramp(indices, slice_stride());
}

bool Shuffle::is_extract_element() const {
    return indices.size() == 1;
}

template<>
void ExprNode<IntImm>::accept(IRVisitor *v) const {
    v->visit((const IntImm *)this);
}
template<>
void ExprNode<UIntImm>::accept(IRVisitor *v) const {
    v->visit((const UIntImm *)this);
}
template<>
void ExprNode<FloatImm>::accept(IRVisitor *v) const {
    v->visit((const FloatImm *)this);
}
template<>
void ExprNode<StringImm>::accept(IRVisitor *v) const {
    v->visit((const StringImm *)this);
}
template<>
void ExprNode<Cast>::accept(IRVisitor *v) const {
    v->visit((const Cast *)this);
}
template<>
void ExprNode<Reinterpret>::accept(IRVisitor *v) const {
    v->visit((const Reinterpret *)this);
}
template<>
void ExprNode<Variable>::accept(IRVisitor *v) const {
    v->visit((const Variable *)this);
}
template<>
void ExprNode<Add>::accept(IRVisitor *v) const {
    v->visit((const Add *)this);
}
template<>
void ExprNode<Sub>::accept(IRVisitor *v) const {
    v->visit((const Sub *)this);
}
template<>
void ExprNode<Mul>::accept(IRVisitor *v) const {
    v->visit((const Mul *)this);
}
template<>
void ExprNode<Div>::accept(IRVisitor *v) const {
    v->visit((const Div *)this);
}
template<>
void ExprNode<Mod>::accept(IRVisitor *v) const {
    v->visit((const Mod *)this);
}
template<>
void ExprNode<Min>::accept(IRVisitor *v) const {
    v->visit((const Min *)this);
}
template<>
void ExprNode<Max>::accept(IRVisitor *v) const {
    v->visit((const Max *)this);
}
template<>
void ExprNode<EQ>::accept(IRVisitor *v) const {
    v->visit((const EQ *)this);
}
template<>
void ExprNode<NE>::accept(IRVisitor *v) const {
    v->visit((const NE *)this);
}
template<>
void ExprNode<LT>::accept(IRVisitor *v) const {
    v->visit((const LT *)this);
}
template<>
void ExprNode<LE>::accept(IRVisitor *v) const {
    v->visit((const LE *)this);
}
template<>
void ExprNode<GT>::accept(IRVisitor *v) const {
    v->visit((const GT *)this);
}
template<>
void ExprNode<GE>::accept(IRVisitor *v) const {
    v->visit((const GE *)this);
}
template<>
void ExprNode<And>::accept(IRVisitor *v) const {
    v->visit((const And *)this);
}
template<>
void ExprNode<Or>::accept(IRVisitor *v) const {
    v->visit((const Or *)this);
}
template<>
void ExprNode<Not>::accept(IRVisitor *v) const {
    v->visit((const Not *)this);
}
template<>
void ExprNode<Select>::accept(IRVisitor *v) const {
    v->visit((const Select *)this);
}
template<>
void ExprNode<Load>::accept(IRVisitor *v) const {
    v->visit((const Load *)this);
}
template<>
void ExprNode<Ramp>::accept(IRVisitor *v) const {
    v->visit((const Ramp *)this);
}
template<>
void ExprNode<Broadcast>::accept(IRVisitor *v) const {
    v->visit((const Broadcast *)this);
}
template<>
void ExprNode<Call>::accept(IRVisitor *v) const {
    v->visit((const Call *)this);
}
template<>
void ExprNode<Shuffle>::accept(IRVisitor *v) const {
    v->visit((const Shuffle *)this);
}
template<>
void ExprNode<VectorReduce>::accept(IRVisitor *v) const {
    v->visit((const VectorReduce *)this);
}
template<>
void ExprNode<Let>::accept(IRVisitor *v) const {
    v->visit((const Let *)this);
}
template<>
void StmtNode<LetStmt>::accept(IRVisitor *v) const {
    v->visit((const LetStmt *)this);
}
template<>
void StmtNode<AssertStmt>::accept(IRVisitor *v) const {
    v->visit((const AssertStmt *)this);
}
template<>
void StmtNode<ProducerConsumer>::accept(IRVisitor *v) const {
    v->visit((const ProducerConsumer *)this);
}
template<>
void StmtNode<For>::accept(IRVisitor *v) const {
    v->visit((const For *)this);
}
template<>
void StmtNode<Store>::accept(IRVisitor *v) const {
    v->visit((const Store *)this);
}
template<>
void StmtNode<Provide>::accept(IRVisitor *v) const {
    v->visit((const Provide *)this);
}
template<>
void StmtNode<Allocate>::accept(IRVisitor *v) const {
    v->visit((const Allocate *)this);
}
template<>
void StmtNode<Free>::accept(IRVisitor *v) const {
    v->visit((const Free *)this);
}
template<>
void StmtNode<Realize>::accept(IRVisitor *v) const {
    v->visit((const Realize *)this);
}
template<>
void StmtNode<Block>::accept(IRVisitor *v) const {
    v->visit((const Block *)this);
}
template<>
void StmtNode<IfThenElse>::accept(IRVisitor *v) const {
    v->visit((const IfThenElse *)this);
}
template<>
void StmtNode<Evaluate>::accept(IRVisitor *v) const {
    v->visit((const Evaluate *)this);
}
template<>
void StmtNode<Prefetch>::accept(IRVisitor *v) const {
    v->visit((const Prefetch *)this);
}
template<>
void StmtNode<Acquire>::accept(IRVisitor *v) const {
    v->visit((const Acquire *)this);
}
template<>
void StmtNode<Fork>::accept(IRVisitor *v) const {
    v->visit((const Fork *)this);
}
template<>
void StmtNode<Atomic>::accept(IRVisitor *v) const {
    v->visit((const Atomic *)this);
}
template<>
void StmtNode<HoistedStorage>::accept(IRVisitor *v) const {
    v->visit((const HoistedStorage *)this);
}

template<>
Expr ExprNode<IntImm>::mutate_expr(IRMutator *v) const {
    return v->visit((const IntImm *)this);
}
template<>
Expr ExprNode<UIntImm>::mutate_expr(IRMutator *v) const {
    return v->visit((const UIntImm *)this);
}
template<>
Expr ExprNode<FloatImm>::mutate_expr(IRMutator *v) const {
    return v->visit((const FloatImm *)this);
}
template<>
Expr ExprNode<StringImm>::mutate_expr(IRMutator *v) const {
    return v->visit((const StringImm *)this);
}
template<>
Expr ExprNode<Cast>::mutate_expr(IRMutator *v) const {
    return v->visit((const Cast *)this);
}
template<>
Expr ExprNode<Reinterpret>::mutate_expr(IRMutator *v) const {
    return v->visit((const Reinterpret *)this);
}
template<>
Expr ExprNode<Variable>::mutate_expr(IRMutator *v) const {
    return v->visit((const Variable *)this);
}
template<>
Expr ExprNode<Add>::mutate_expr(IRMutator *v) const {
    return v->visit((const Add *)this);
}
template<>
Expr ExprNode<Sub>::mutate_expr(IRMutator *v) const {
    return v->visit((const Sub *)this);
}
template<>
Expr ExprNode<Mul>::mutate_expr(IRMutator *v) const {
    return v->visit((const Mul *)this);
}
template<>
Expr ExprNode<Div>::mutate_expr(IRMutator *v) const {
    return v->visit((const Div *)this);
}
template<>
Expr ExprNode<Mod>::mutate_expr(IRMutator *v) const {
    return v->visit((const Mod *)this);
}
template<>
Expr ExprNode<Min>::mutate_expr(IRMutator *v) const {
    return v->visit((const Min *)this);
}
template<>
Expr ExprNode<Max>::mutate_expr(IRMutator *v) const {
    return v->visit((const Max *)this);
}
template<>
Expr ExprNode<EQ>::mutate_expr(IRMutator *v) const {
    return v->visit((const EQ *)this);
}
template<>
Expr ExprNode<NE>::mutate_expr(IRMutator *v) const {
    return v->visit((const NE *)this);
}
template<>
Expr ExprNode<LT>::mutate_expr(IRMutator *v) const {
    return v->visit((const LT *)this);
}
template<>
Expr ExprNode<LE>::mutate_expr(IRMutator *v) const {
    return v->visit((const LE *)this);
}
template<>
Expr ExprNode<GT>::mutate_expr(IRMutator *v) const {
    return v->visit((const GT *)this);
}
template<>
Expr ExprNode<GE>::mutate_expr(IRMutator *v) const {
    return v->visit((const GE *)this);
}
template<>
Expr ExprNode<And>::mutate_expr(IRMutator *v) const {
    return v->visit((const And *)this);
}
template<>
Expr ExprNode<Or>::mutate_expr(IRMutator *v) const {
    return v->visit((const Or *)this);
}
template<>
Expr ExprNode<Not>::mutate_expr(IRMutator *v) const {
    return v->visit((const Not *)this);
}
template<>
Expr ExprNode<Select>::mutate_expr(IRMutator *v) const {
    return v->visit((const Select *)this);
}
template<>
Expr ExprNode<Load>::mutate_expr(IRMutator *v) const {
    return v->visit((const Load *)this);
}
template<>
Expr ExprNode<Ramp>::mutate_expr(IRMutator *v) const {
    return v->visit((const Ramp *)this);
}
template<>
Expr ExprNode<Broadcast>::mutate_expr(IRMutator *v) const {
    return v->visit((const Broadcast *)this);
}
template<>
Expr ExprNode<Call>::mutate_expr(IRMutator *v) const {
    return v->visit((const Call *)this);
}
template<>
Expr ExprNode<Shuffle>::mutate_expr(IRMutator *v) const {
    return v->visit((const Shuffle *)this);
}
template<>
Expr ExprNode<VectorReduce>::mutate_expr(IRMutator *v) const {
    return v->visit((const VectorReduce *)this);
}
template<>
Expr ExprNode<Let>::mutate_expr(IRMutator *v) const {
    return v->visit((const Let *)this);
}

template<>
Stmt StmtNode<LetStmt>::mutate_stmt(IRMutator *v) const {
    return v->visit((const LetStmt *)this);
}
template<>
Stmt StmtNode<AssertStmt>::mutate_stmt(IRMutator *v) const {
    return v->visit((const AssertStmt *)this);
}
template<>
Stmt StmtNode<ProducerConsumer>::mutate_stmt(IRMutator *v) const {
    return v->visit((const ProducerConsumer *)this);
}
template<>
Stmt StmtNode<For>::mutate_stmt(IRMutator *v) const {
    return v->visit((const For *)this);
}
template<>
Stmt StmtNode<Store>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Store *)this);
}
template<>
Stmt StmtNode<Provide>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Provide *)this);
}
template<>
Stmt StmtNode<Allocate>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Allocate *)this);
}
template<>
Stmt StmtNode<Free>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Free *)this);
}
template<>
Stmt StmtNode<Realize>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Realize *)this);
}
template<>
Stmt StmtNode<Block>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Block *)this);
}
template<>
Stmt StmtNode<IfThenElse>::mutate_stmt(IRMutator *v) const {
    return v->visit((const IfThenElse *)this);
}
template<>
Stmt StmtNode<Evaluate>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Evaluate *)this);
}
template<>
Stmt StmtNode<Prefetch>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Prefetch *)this);
}
template<>
Stmt StmtNode<Acquire>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Acquire *)this);
}
template<>
Stmt StmtNode<Fork>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Fork *)this);
}
template<>
Stmt StmtNode<Atomic>::mutate_stmt(IRMutator *v) const {
    return v->visit((const Atomic *)this);
}
template<>
Stmt StmtNode<HoistedStorage>::mutate_stmt(IRMutator *v) const {
    return v->visit((const HoistedStorage *)this);
}

Call::ConstString Call::buffer_get_dimensions = "_halide_buffer_get_dimensions";
Call::ConstString Call::buffer_get_min = "_halide_buffer_get_min";
Call::ConstString Call::buffer_get_extent = "_halide_buffer_get_extent";
Call::ConstString Call::buffer_get_stride = "_halide_buffer_get_stride";
Call::ConstString Call::buffer_get_max = "_halide_buffer_get_max";
Call::ConstString Call::buffer_get_host = "_halide_buffer_get_host";
Call::ConstString Call::buffer_get_device = "_halide_buffer_get_device";
Call::ConstString Call::buffer_get_device_interface = "_halide_buffer_get_device_interface";
Call::ConstString Call::buffer_get_shape = "_halide_buffer_get_shape";
Call::ConstString Call::buffer_get_host_dirty = "_halide_buffer_get_host_dirty";
Call::ConstString Call::buffer_get_device_dirty = "_halide_buffer_get_device_dirty";
Call::ConstString Call::buffer_get_type = "_halide_buffer_get_type";
Call::ConstString Call::buffer_set_host_dirty = "_halide_buffer_set_host_dirty";
Call::ConstString Call::buffer_set_device_dirty = "_halide_buffer_set_device_dirty";
Call::ConstString Call::buffer_is_bounds_query = "_halide_buffer_is_bounds_query";
Call::ConstString Call::buffer_init = "_halide_buffer_init";
Call::ConstString Call::buffer_init_from_buffer = "_halide_buffer_init_from_buffer";
Call::ConstString Call::buffer_crop = "_halide_buffer_crop";
Call::ConstString Call::buffer_set_bounds = "_halide_buffer_set_bounds";
Call::ConstString Call::trace = "halide_trace_helper";

}  // namespace Internal
}  // namespace Halide
