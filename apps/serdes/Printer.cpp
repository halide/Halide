#include "Printer.h"
#include <iostream>

void Printer::print_pipeline(const Halide::Pipeline &pipeline) {
    std::cout << "Printing pipeline\n";
    std::cout << "outputs: [Func]\n";
    for (const auto &func : pipeline.outputs()) {
        this->print_function(func.function());
    }
    std::cout << "requirements: [Stmt]\n";
    for (const auto &stmt : pipeline.requirements()) {
        this->print_stmt(stmt);
    }
}

void Printer::print_function(const Halide::Internal::Function &function) {
    std::cout << "Printing Function\n";
    std::cout << "name: " << function.name() << "\n";
    std::cout << "origin_name: " << function.origin_name() << "\n";
    std::cout << "output_types: [Type]\n";
    for (const auto &type : function.output_types()) {
        this->print_type(type);
    }
    std::cout << "required_types: [Type]\n";
    for (const auto &type : function.required_types()) {
        this->print_type(type);
    }
    std::cout << "required_dimensions: " << function.required_dimensions() << "\n";
    std::cout << "args: [string]\n";
    for (const auto &arg : function.args()) {
        std::cout << arg << "\n";
    }
}

void Printer::print_type(const Halide::Type &type) {
    std::cout << "Printing Type\n";
    std::cout << "bits: " << type.bits() << "\n";
    std::cout << "lanes: " << type.lanes() << "\n";
    switch (type.code()) {
    case Halide::Type::Int:
        std::cout << "code: Int\n";
        break;
    case Halide::Type::UInt:
        std::cout << "code: UInt\n";
        break;
    case Halide::Type::Float:
        std::cout << "code: Float\n";
        break;
    case Halide::Type::Handle:
        std::cout << "code: Handle\n";
        break;
    case Halide::Type::BFloat:
        std::cout << "code: BFloat\n";
        break;
    default:
        std::cout << "code: Unknown\n";
        break;
    }
}

void Printer::print_stmt(const Halide::Internal::Stmt &stmt) {
    std::cout << "Printing Stmt\n";
    if (!stmt.defined()) {
        std::cout << "(undefined)\n";
        return;
    }
    switch (stmt->node_type) {
    case Halide::Internal::IRNodeType::LetStmt: {
        std::cout << "node_type: LetStmt\n";
        auto let_stmt = stmt.as<Halide::Internal::LetStmt>();
        std::string name = let_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "value: Expr\n";
        print_expr(let_stmt->value);
        std::cout << "body: Stmt\n";
        print_stmt(let_stmt->body);
    }
    case Halide::Internal::IRNodeType::AssertStmt: {
        std::cout << "node_type: AssertStmt\n";
        auto assert_stmt = stmt.as<Halide::Internal::AssertStmt>();
        std::cout << "condition: Expr\n";
        print_expr(assert_stmt->condition);
        std::cout << "message: Expr\n";
        print_expr(assert_stmt->message);
    }
    case Halide::Internal::IRNodeType::ProducerConsumer: {
        std::cout << "node_type: ProducerConsumer\n";
        auto producer_consumer_stmt = stmt.as<Halide::Internal::ProducerConsumer>();
        auto name = producer_consumer_stmt->name;
        std::cout << "name: " << name << "\n";
        auto is_producer = producer_consumer_stmt->is_producer;
        std::cout << "is_producer: " << is_producer << "\n";
        std::cout << "body: Stmt\n";
        print_stmt(producer_consumer_stmt->body);
    }
    case Halide::Internal::IRNodeType::For: {
        std::cout << "node_type: For\n";
        auto for_stmt = stmt.as<Halide::Internal::For>();
        std::string name = for_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "min: Expr\n";
        print_expr(for_stmt->min);
        std::cout << "extent: Expr\n";
        print_expr(for_stmt->extent);
        std::cout << "body: Stmt\n";
        print_stmt(for_stmt->body);
    }
    case Halide::Internal::IRNodeType::Store: {
        std::cout << "node_type: Store\n";
        auto store_stmt = stmt.as<Halide::Internal::Store>();
        std::string name = store_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "predicate: Expr\n";
        print_expr(store_stmt->predicate);
        std::cout << "value: Expr\n";
        print_expr(store_stmt->value);
        std::cout << "index: Expr\n";
        print_expr(store_stmt->index);
    }
    case Halide::Internal::IRNodeType::Provide: {
        std::cout << "node_type: Provide\n";
        auto provide_stmt = stmt.as<Halide::Internal::Provide>();
        std::string name = provide_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "values: [Expr]\n";
        for (const auto &value : provide_stmt->values) {
            print_expr(value);
        }
        std::cout << "args: [Expr]\n";
        for (const auto &arg : provide_stmt->args) {
            print_expr(arg);
        }
        std::cout << "predicate: Expr\n";
        print_expr(provide_stmt->predicate);
    }
    case Halide::Internal::IRNodeType::Allocate: {
        std::cout << "node_type: Allocate\n";
        auto allocate_stmt = stmt.as<Halide::Internal::Allocate>();
        std::string name = allocate_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "type: Type\n";
        print_type(allocate_stmt->type);
        std::cout << "extents: [Expr]\n";
        for (const auto &extent : allocate_stmt->extents) {
            print_expr(extent);
        }
        std::cout << "condition: Expr\n";
        print_expr(allocate_stmt->condition);
        std::cout << "new_expr: Expr\n";
        print_expr(allocate_stmt->new_expr);
        std::string free_function = allocate_stmt->free_function;
        std::cout << "free_function: " << free_function << "\n";
        std::cout << "padding: " << allocate_stmt->padding << "\n";
        std::cout << "body: Stmt\n";
        print_stmt(allocate_stmt->body);
    }
    case Halide::Internal::IRNodeType::Free: {
        std::cout << "node_type: Free\n";
        auto free_stmt = stmt.as<Halide::Internal::Free>();
        std::string name = free_stmt->name;
        std::cout << "name: " << name << "\n";
    }
    case Halide::Internal::IRNodeType::Realize: {
        std::cout << "node_type: Realize\n";
        auto realize_stmt = stmt.as<Halide::Internal::Realize>();
        std::string name = realize_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "types: [Type]\n";
        for (const auto &type : realize_stmt->types) {
            print_type(type);
        }
        std::cout << "bounds: [Range]\n";
        for (const auto &bound : realize_stmt->bounds) {
            print_range(bound);
        }
        std::cout << "condition: Expr\n";
        print_expr(realize_stmt->condition);
        std::cout << "body: Stmt\n";
        print_stmt(realize_stmt->body);
    }
    case Halide::Internal::IRNodeType::Block: {
        std::cout << "node_type: Block\n";
        auto block_stmt = stmt.as<Halide::Internal::Block>();
        std::cout << "first: Stmt\n";
        print_stmt(block_stmt->first);
        std::cout << "rest: Stmt\n";
        print_stmt(block_stmt->rest);
    }
    case Halide::Internal::IRNodeType::IfThenElse: {
        std::cout << "node_type: IfThenElse\n";
        auto if_then_else_stmt = stmt.as<Halide::Internal::IfThenElse>();
        std::cout << "condition: Expr\n";
        print_expr(if_then_else_stmt->condition);
        std::cout << "then_case: Stmt\n";
        print_stmt(if_then_else_stmt->then_case);
        std::cout << "else_case: Stmt\n";
        print_stmt(if_then_else_stmt->else_case);
    }
    case Halide::Internal::IRNodeType::Evaluate: {
        std::cout << "node_type: Evaluate\n";
        auto evaluate_stmt = stmt.as<Halide::Internal::Evaluate>();
        std::cout << "value: Expr\n";
        print_expr(evaluate_stmt->value);
    }
    case Halide::Internal::IRNodeType::Prefetch: {
        std::cout << "node_type: Prefetch\n";
        auto prefetch_stmt = stmt.as<Halide::Internal::Prefetch>();
        auto name = prefetch_stmt->name;
        std::cout << "name: " << name << "\n";
        std::cout << "types: [Type]\n";
        for (const auto &type : prefetch_stmt->types) {
            print_type(type);
        }
        std::cout << "bounds: [Range]\n";
        for (const auto &bound : prefetch_stmt->bounds) {
            print_range(bound);
        }
        std::cout << "condition: Expr\n";
        print_expr(prefetch_stmt->condition);
        auto body = prefetch_stmt->body;
        std::cout << "body: Stmt\n";
        print_stmt(body);
    }
    case Halide::Internal::IRNodeType::Acquire: {
        std::cout << "node_type: Acquire\n";
        auto acquire_stmt = stmt.as<Halide::Internal::Acquire>();
        std::cout << "semaphore: Expr\n";
        auto semaphore = acquire_stmt->semaphore;
        std::cout << "count: Expr\n";
        auto count = acquire_stmt->count;
        auto body = acquire_stmt->body;
        std::cout << "body: Stmt\n";
        print_stmt(body);
    }
    case Halide::Internal::IRNodeType::Fork: {
        std::cout << "node_type: Fork\n";
        auto fork_stmt = stmt.as<Halide::Internal::Fork>();
        auto first = fork_stmt->first;
        std::cout << "first: Stmt\n";
        print_stmt(first);
        auto rest = fork_stmt->rest;
        std::cout << "rest: Stmt\n";
        print_stmt(rest);
    }
    case Halide::Internal::IRNodeType::Atomic: {
        std::cout << "node_type: Atomic\n";
        auto atomic_stmt = stmt.as<Halide::Internal::Atomic>();
        auto producer_name = atomic_stmt->producer_name;
        std::cout << "producer_name: " << producer_name << "\n";
        auto mutex_name = atomic_stmt->mutex_name;
        std::cout << "mutex_name: " << mutex_name << "\n";
        auto body = atomic_stmt->body;
        std::cout << "body: Stmt\n";
        print_stmt(body);
    }
    default:
        std::cerr << "Unsupported stmt type\n";
        exit(1);
    }
}

void Printer::print_expr(const Halide::Expr &expr) {
    std::cout << "Printing Expr\n";
    if (!expr.defined()) {
        std::cout << "(undefined)\n";
        return;
    }
    switch (expr->node_type) {
    case Halide::Internal::IRNodeType::IntImm: {
        std::cout << "node_type: IntImm\n";
        std::cout << "value: " << expr.as<Halide::Internal::IntImm>()->value << "\n";
    }
    case Halide::Internal::IRNodeType::UIntImm: {
        std::cout << "node_type: UIntImm\n";
        std::cout << "value: " << expr.as<Halide::Internal::UIntImm>()->value << "\n";
    }
    case Halide::Internal::IRNodeType::FloatImm: {
        std::cout << "node_type: FloatImm\n";
        std::cout << "value: " << expr.as<Halide::Internal::FloatImm>()->value << "\n";
    }
    case Halide::Internal::IRNodeType::StringImm: {
        std::cout << "node_type: StringImm\n";
        std::cout << "value: " << expr.as<Halide::Internal::StringImm>()->value << "\n";
    }
    case Halide::Internal::IRNodeType::Cast: {
        std::cout << "node_type: Cast\n";
        auto cast_expr = expr.as<Halide::Internal::Cast>();
        auto value = cast_expr->value;
        std::cout << "value: Expr\n";
        print_expr(value);
    }
    case Halide::Internal::IRNodeType::Reinterpret: {
        std::cout << "node_type: Reinterpret\n";
        auto reinterpret_expr = expr.as<Halide::Internal::Reinterpret>();
        auto value = reinterpret_expr->value;
        std::cout << "value: Expr\n";
    }
    case Halide::Internal::IRNodeType::Add: {
        std::cout << "node_type: Add\n";
        auto add_expr = expr.as<Halide::Internal::Add>();
        auto a = add_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = add_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Sub: {
        std::cout << "node_type: Sub\n";
        auto sub_expr = expr.as<Halide::Internal::Sub>();
        auto a = sub_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = sub_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Mul: {
        std::cout << "node_type: Mul\n";
        auto mul_expr = expr.as<Halide::Internal::Mul>();
        auto a = mul_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = mul_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Div: {
        std::cout << "node_type: Div\n";
        auto div_expr = expr.as<Halide::Internal::Div>();
        auto a = div_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = div_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Mod: {
        std::cout << "node_type: Mod\n";
        auto mod_expr = expr.as<Halide::Internal::Mod>();
        auto a = mod_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = mod_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Min: {
        std::cout << "node_type: Min\n";
        auto min_expr = expr.as<Halide::Internal::Min>();
        auto a = min_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = min_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Max: {
        std::cout << "node_type: Max\n";
        auto max_expr = expr.as<Halide::Internal::Max>();
        auto a = max_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = max_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::EQ: {
        std::cout << "node_type: EQ\n";
        auto eq_expr = expr.as<Halide::Internal::EQ>();
        auto a = eq_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = eq_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::NE: {
        std::cout << "node_type: NE\n";
        auto ne_expr = expr.as<Halide::Internal::NE>();
        auto a = ne_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = ne_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::LT: {
        std::cout << "node_type: LT\n";
        auto lt_expr = expr.as<Halide::Internal::LT>();
        auto a = lt_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = lt_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::LE: {
        std::cout << "node_type: LE\n";
        auto le_expr = expr.as<Halide::Internal::LE>();
        auto a = le_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = le_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::GT: {
        std::cout << "node_type: GT\n";
        auto gt_expr = expr.as<Halide::Internal::GT>();
        auto a = gt_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = gt_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::GE: {
        std::cout << "node_type: GE\n";
        auto ge_expr = expr.as<Halide::Internal::GE>();
        auto a = ge_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = ge_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::And: {
        std::cout << "node_type: And\n";
        auto and_expr = expr.as<Halide::Internal::And>();
        auto a = and_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = and_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Or: {
        std::cout << "node_type: Or\n";
        auto or_expr = expr.as<Halide::Internal::Or>();
        auto a = or_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
        auto b = or_expr->b;
        std::cout << "b: Expr\n";
    }
    case Halide::Internal::IRNodeType::Not: {
        std::cout << "node_type: Not\n";
        auto not_expr = expr.as<Halide::Internal::Not>();
        auto a = not_expr->a;
        std::cout << "a: Expr\n";
        print_expr(a);
    }
    case Halide::Internal::IRNodeType::Select: {
        std::cout << "node_type: Select\n";
        auto select_expr = expr.as<Halide::Internal::Select>();
        auto condition = select_expr->condition;
        std::cout << "condition: Expr\n";
        print_expr(condition);
        auto true_value = select_expr->true_value;
        std::cout << "true_value: Expr\n";
        print_expr(true_value);
        auto false_value = select_expr->false_value;
        std::cout << "false_value: Expr\n";
        print_expr(false_value);
    }
    case Halide::Internal::IRNodeType::Load: {
        std::cout << "node_type: Load\n";
        auto load_expr = expr.as<Halide::Internal::Load>();
        auto name = load_expr->name;
        std::cout << "name: " << name << "\n";
        auto predicate = load_expr->predicate;
        std::cout << "predicate: Expr\n";
        print_expr(predicate);
        auto index = load_expr->index;
        std::cout << "index: Expr\n";
        print_expr(index);
    }
    case Halide::Internal::IRNodeType::Ramp: {
        std::cout << "node_type: Ramp\n";
        auto ramp_expr = expr.as<Halide::Internal::Ramp>();
        auto base = ramp_expr->base;
        std::cout << "base: Expr\n";
        print_expr(base);
        auto stride = ramp_expr->stride;
        std::cout << "stride: Expr\n";
        print_expr(stride);
        auto lanes = ramp_expr->lanes;
        std::cout << "lanes: " << lanes << "\n";
    }
    case Halide::Internal::IRNodeType::Broadcast: {
        std::cout << "node_type: Broadcast\n";
        auto broadcast_expr = expr.as<Halide::Internal::Broadcast>();
        auto value = broadcast_expr->value;
        std::cout << "value: Expr\n";
        print_expr(value);
        auto lanes = broadcast_expr->lanes;
        std::cout << "lanes: " << lanes << "\n";
    }
    case Halide::Internal::IRNodeType::Let: {
        std::cout << "node_type: Let\n";
        auto let_expr = expr.as<Halide::Internal::Let>();
        auto name = let_expr->name;
        std::cout << "name: " << name << "\n";
        auto value = let_expr->value;
        std::cout << "value: Expr\n";
        print_expr(value);
        auto body = let_expr->body;
        std::cout << "body: Expr\n";
        print_expr(body);
    }
    case Halide::Internal::IRNodeType::Call: {
        std::cout << "node_type: Call\n";
        auto call_expr = expr.as<Halide::Internal::Call>();
        auto name = call_expr->name;
        std::cout << "name: " << name << "\n";
        auto args = call_expr->args;
        std::cout << "args: [Expr]\n";
        for (const auto &arg : args) {
            print_expr(arg);
        }
        auto value_index = call_expr->value_index;
        std::cout << "value_index: " << value_index << "\n";
    }
    case Halide::Internal::IRNodeType::Variable: {
        std::cout << "node_type: Variable\n";
        auto variable_expr = expr.as<Halide::Internal::Variable>();
        auto name = variable_expr->name;
        std::cout << "name: " << name << "\n";
    }
    case Halide::Internal::IRNodeType::Shuffle: {
        std::cout << "node_type: Shuffle\n";
        auto shuffle_expr = expr.as<Halide::Internal::Shuffle>();
        auto vectors = shuffle_expr->vectors;
        std::cout << "vectors: [Expr]\n";
        for (const auto &vector : vectors) {
            print_expr(vector);
        }
        auto indices = shuffle_expr->indices;
        std::cout << "indices: [int]\n";
        for (const auto &index : indices) {
            std::cout << index << "\n";
        }
    }
    case Halide::Internal::IRNodeType::VectorReduce: {
        std::cout << "node_type: VectorReduce\n";
        auto vector_reduce_expr = expr.as<Halide::Internal::VectorReduce>();
        auto value = vector_reduce_expr->value;
        std::cout << "value: Expr\n";
        print_expr(value);
    }
    default:
        std::cerr << "Unsupported Expr type\n";
        exit(1);
    }
}

void Printer::print_range(const Halide::Range &range) {
    std::cout << "Printing Range\n";
    std::cout << "min: Expr\n";
    print_expr(range.min);
    std::cout << "extent: Expr\n";
    print_expr(range.extent);
}

void Printer::print_bound(const Halide::Internal::Bound &bound) {
    std::cout << "Printing Bound\n";
    std::cout << "var: " << bound.var << "\n";
    std::cout << "min: Expr\n";
    print_expr(bound.min);
    std::cout << "extent: Expr\n";
    print_expr(bound.extent);
    std::cout << "modulus: Expr\n";
    print_expr(bound.modulus);
    std::cout << "remainder: Expr\n";
    print_expr(bound.remainder);
}

void Printer::print_storage_dim(const Halide::Internal::StorageDim &storage_dim) {
    std::cout << "Printing StorageDim\n";
    std::cout << "var: " << storage_dim.var << "\n";
    std::cout << "alignment: Expr\n";
    print_expr(storage_dim.alignment);
    std::cout << "bound: Expr\n";
    print_expr(storage_dim.bound);
    std::cout << "fold_factor: Expr\n";
    print_expr(storage_dim.fold_factor);
    std::cout << "fold_forward: " << storage_dim.fold_forward << "\n";
}

void Printer::print_loop_level(const Halide::LoopLevel &loop_level) {
    std::cout << "Printing LoopLevel\n";
    std::cout << "func_name: " << loop_level.func() << "\n";
    std::cout << "stage_index: " << loop_level.stage_index() << "\n";
    std::cout << "var_name: " << loop_level.var_name() << "\n";
    std::cout << "is_rvar: " << loop_level.is_rvar() << "\n";
    std::cout << "locked: " << loop_level.locked() << "\n";
}

void Printer::print_func_schedule(const Halide::Internal::FuncSchedule &func_schedule) {
    std::cout << "Printing FuncSchedule\n";
    std::cout << "store_level: LoopLevel\n";
    print_loop_level(func_schedule.store_level());
    std::cout << "compute_level: LoopLevel\n";
    print_loop_level(func_schedule.compute_level());
    std::cout << "storage_dims: [StorageDim]\n";
    for (const auto &storage_dim : func_schedule.storage_dims()) {
        print_storage_dim(storage_dim);
    }
    std::cout << "bounds: [Bound]\n";
    for (const auto &bound : func_schedule.bounds()) {
        print_bound(bound);
    }
    std::cout << "estimates: [Bound]\n";
    for (const auto &estimate : func_schedule.estimates()) {
        print_bound(estimate);
    }
    std::cout << "memoized: " << func_schedule.memoized() << "\n";
    std::cout << "async: " << func_schedule.async() << "\n";
    std::cout << "memoize_eviction_key: Expr\n";
    print_expr(func_schedule.memoize_eviction_key());
}