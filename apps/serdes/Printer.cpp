#include "Printer.h"
#include <iostream>

void Printer::print_pipeline(const Halide::Pipeline& pipeline) {
    std::cout << "Printing pipeline\n";
    std::cout << "outputs: [Func]\n";
    for (const auto& func : pipeline.outputs()) {
        this->print_function(func.function());
    }
    std::cout << "requirements: [Stmt]\n";
    for (const auto& stmt : pipeline.requirements()) {
        this->print_stmt(stmt);
    }
}

void Printer::print_function(const Halide::Internal::Function& function) {
    std::cout << "Printing function\n";
    std::cout << "name: " << function.name() << "\n";
    std::cout << "origin_name: " << function.origin_name() << "\n";
    std::cout << "output_types: [Type]\n";
    for (const auto& type : function.output_types()) {
        this->print_type(type);
    }
    std::cout << "required_types: [Type]\n";
    for (const auto& type : function.required_types()) {
        this->print_type(type);
    }
    std::cout << "required_dimensions: " << function.required_dimensions() << "\n";
    std::cout << "args: [string]\n";
    for (const auto& arg : function.args()) {
        std::cout << arg << "\n";
    }
}

void Printer::print_type(const Halide::Type& type) {
    std::cout << "Printing type\n";
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

void Printer::print_stmt(const Halide::Internal::Stmt& stmt) {
    std::cout << "Printing stmt\n";
    switch  (stmt->node_type) {
        case Halide::Internal::IRNodeType::LetStmt: {
            std::cout << "node_type: LetStmt\n";
            auto let_stmt = stmt.as<Halide::Internal::LetStmt>();
            std::string name = let_stmt->name;
            std::cout << "name: " << name << "\n";
            std::cout << "body: Stmt\n";
            print_stmt(let_stmt->body);
        }
        case Halide::Internal::IRNodeType::AssertStmt: {
            std::cout << "node_type: AssertStmt\n";
            // auto assert_stmt = stmt.as<Halide::Internal::AssertStmt>();
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
            std::cout << "body: Stmt\n";
            print_stmt(for_stmt->body);
        }
        case Halide::Internal::IRNodeType::Store: {
            std::cout << "node_type: Store\n";
            auto store_stmt = stmt.as<Halide::Internal::Store>();
            std::string name = store_stmt->name;
            std::cout << "name: " << name << "\n";
        }
        case Halide::Internal::IRNodeType::Provide: {
            std::cout << "node_type: Provide\n";
            auto provide_stmt = stmt.as<Halide::Internal::Provide>();
            std::string name = provide_stmt->name;
            std::cout << "name: " << name << "\n";
        }
        case Halide::Internal::IRNodeType::Allocate: {
            std::cout << "node_type: Allocate\n";
            auto allocate_stmt = stmt.as<Halide::Internal::Allocate>();
            std::string name = allocate_stmt->name;
            std::cout << "name: " << name << "\n";
            std::cout << "type: Type\n";
            print_type(allocate_stmt->type);
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
            for (const auto& type : realize_stmt->types) {
                print_type(type);
            }
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
            std::cout << "then_case: Stmt\n";
            print_stmt(if_then_else_stmt->then_case);
            std::cout << "else_case: Stmt\n";
            print_stmt(if_then_else_stmt->else_case);
        }
        case Halide::Internal::IRNodeType::Evaluate: {
            std::cout << "node_type: Evaluate\n";
            // auto evaluate_stmt = stmt.as<Halide::Internal::Evaluate>();
        }
        case Halide::Internal::IRNodeType::Prefetch: {
            std::cout << "node_type: Prefetch\n";
            auto prefetch_stmt = stmt.as<Halide::Internal::Prefetch>();
            auto name = prefetch_stmt->name;
            std::cout << "name: " << name << "\n";
            std::cout << "types: [Type]\n";
            for (const auto& type : prefetch_stmt->types) {
                print_type(type);
            }
            auto body = prefetch_stmt->body;
            std::cout << "body: Stmt\n";
            print_stmt(body);
        }
        case Halide::Internal::IRNodeType::Acquire: {
            std::cout << "node_type: Acquire\n";
            auto acquire_stmt = stmt.as<Halide::Internal::Acquire>();
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