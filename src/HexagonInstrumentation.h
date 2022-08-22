#pragma once

/** \file
 * Defines a Generator interface which injects timing code around loops and
 * emits a report to logcat via adsprpc (Hexagon only).
 */

#include "IRMutator.h"
#include <iostream>
#include <iso646.h>
#include <map>
#include <optional>
#include <regex>
#include <set>

static_assert(__cplusplus >= 201703L, "requires c++17");

namespace Halide::Internal {
// Profiling code injection
// Injects calls to:
// 1. Record metadata (generator name, arguments, output dimensions, schedule) at program start
// 2. Record timer value before and after a loop.
// 3. Record self-induced overhead of thread launches
// 4. Report collected data and program end
// To rephrase in psuedocode, take a filter such as:
//    my_filter(in, args) -> out {
//      for x parallel:
//        for y:
//          out = do_work(in)
//    }
// it will be rewritten to
//    my_filter(in, args) -> out {
//      record_signature("my_filter", args, in.dims, out.dims)
//      record_loop_start("x")
//      record_fork_overhead_start("x")
//      for x parallel:
//        record_fork_overhead_end("x")
//        record_loop_start("y")
//        for y:
//          out = do_work(in)
//        record_loop_end("y")
//      record_loop_end("x")
//      report_recorded_data()
//    }
struct HexagonInstrumentation : IRMutator {
    HexagonInstrumentation(const std::string &generator_name,
                           std::vector<Argument> &&program_arguments,
                           std::vector<Func> &&program_outputs,
                           const std::vector<std::string> &schedule_desc)
        : generator_name(generator_name),
          program_arguments(std::move(program_arguments)),
          program_outputs(std::move(program_outputs)),
          schedule_desc(schedule_desc) {
    }

    // Inject metadata collection and reporting at program entry and exit
    auto visit(const Block *block) -> Stmt override {
        bool is_root = false;
        if (not passed_entry_point) {
            is_root = true;
            passed_entry_point = true;
        }

        if (is_root) {
            std::vector<Stmt> stmts;

            stmts.push_back(
                Evaluate::make(Call::make(Handle(), "declare_generator",
                                          {Expr{generator_name}}, Call::Extern)));
            for (auto &arg : program_arguments) {
                auto [trace_call, variable, type_string] = [&]() -> std::tuple<std::string, Expr, std::string> {
                    if (arg.is_scalar()) {
                        auto base_var = Variable::make(arg.type, arg.name);
                        std::stringstream type;
                        type << arg.type;

                        if (arg.type.is_float()) {
                            return {"trace_parameter_float", base_var, type.str()};
                        } else if (arg.type.is_int()) {
                            return {"trace_parameter_int", Cast::make(Int(64), base_var), type.str()};
                        } else if (arg.type.is_uint()) {
                            return {"trace_parameter_uint", Cast::make(UInt(64), base_var), type.str()};
                        } else {
                            throw std::runtime_error("unexpected arg type");
                        }
                    } else {
                        return {"trace_parameter_buffer", Variable::make(Handle(), arg.name + ".buffer"), "buffer"};
                    }
                }();

                stmts.push_back(Evaluate::make(Call::make(
                    Handle(), trace_call, {Expr{arg.name}, variable, Expr{type_string}, Expr{false}}, Call::Extern)));
            }
            for (auto &output : program_outputs) {
                stmts.push_back(Evaluate::make(Call::make(
                    Handle(), "trace_parameter_buffer",
                    {Expr{output.name()},
                     Variable::make(Handle(), output.name() + ".buffer"), Expr{"buffer"}, Expr{true}},
                    Call::Extern)));
            }
            for (auto &line : schedule_desc) {
                if (not line.empty()) {
                    stmts.push_back(Evaluate::make(Call::make(
                        Handle(), "describe_schedule", {Expr{line}}, Call::Extern)));
                }
            }

            stmts.push_back(program_start_stmt(node_id_generator++, generator_name));
            stmts.push_back(IRMutator::visit(block));
            stmts.push_back(program_end_stmt());
            stmts.push_back(print_report_stmt());

            return Block::make(stmts);
        } else {
            return IRMutator::visit(block);
        }
    }
    // Inject time measurement at loop entry and exit
    auto visit(const For *loop) -> Stmt override {
        auto id = node_id_generator++;

        if (loop->is_parallel()) {
            // Sets up the fork point and timing start/stop for this loop, and thread
            // tracking for the child loops.
            return Block::make(
                {pre_fork_stmt(id, loop->name),
                 with_parent_thread_id_stmt(IRMutator::visit(
                     For::make(loop->name, loop->min, loop->extent, loop->for_type,
                               loop->device_api,
                               Block::make({fork_start_stmt(id, loop->name),
                                            loop->body, fork_end_stmt()}))
                         .as<For>())),
                 post_fork_stmt()});
        } else {
            // Sets up the timing start/stop for this loop
            return Block::make({loop_start_stmt(id, loop->name),
                                IRMutator::visit(loop), loop_end_stmt()});
        }
    }
    using IRMutator::visit;

    bool passed_entry_point = false;  // used to identify the entry block
    uint32_t node_id_generator = 0;   // used to generate unique identifiers for
                                      // nodes in the control flow graph

    // metadata
    const std::string generator_name;
    const std::vector<Argument> program_arguments;
    const std::vector<Func> program_outputs;
    const std::vector<std::string> schedule_desc;

private:  // Halide statements for accessing profiling library on DSP
    static auto program_start_stmt(uint32_t root_node_id,
                                   const std::string &label) -> Stmt {
        return Evaluate::make(Call::make(Handle(), "program_start",
                                         {Expr{root_node_id}, Expr{label}},
                                         Call::Extern));
    }
    static auto program_end_stmt() -> Stmt {
        return Evaluate::make(
            Call::make(Handle(), "program_end", {}, Call::Extern));
    }
    static auto with_parent_thread_id_stmt(Stmt body) -> Stmt {
        return LetStmt::make(
            "parent_thread_id",
            Call::make(UInt(32), "get_thread_id", {}, Call::Extern), body);
    }
    static auto pre_fork_stmt(uint32_t loop_id, const std::string &label)
        -> Stmt {
        return Evaluate::make(Call::make(Handle(), "pre_fork",
                                         {Expr{loop_id}, Expr{label + ".fork"}},
                                         Call::Extern));
    }
    static auto post_fork_stmt() -> Stmt {
        return Evaluate::make(Call::make(Handle(), "post_fork", {}, Call::Extern));
    }
    static auto fork_start_stmt(uint32_t loop_id, const std::string &label)
        -> Stmt {
        return Evaluate::make(
            Call::make(Handle(), "fork_start",
                       {Variable::make(UInt(32), "parent_thread_id"), Expr{loop_id},
                        Expr{label}},
                       Call::Extern));
    }
    static auto fork_end_stmt() -> Stmt {
        return Evaluate::make(Call::make(Handle(), "fork_end", {}, Call::Extern));
    }
    static auto loop_start_stmt(uint32_t loop_id, const std::string &label)
        -> Stmt {
        return Evaluate::make(Call::make(
            Handle(), "loop_start", {Expr{loop_id}, Expr{label}}, Call::Extern));
    }
    static auto loop_end_stmt() -> Stmt {
        return Evaluate::make(Call::make(Handle(), "loop_end", {}, Call::Extern));
    }
    static auto print_report_stmt() -> Stmt {
        return Evaluate::make(
            Call::make(Handle(), "print_report", {}, Call::Extern));
    }
};
}  // namespace
