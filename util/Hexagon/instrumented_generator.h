#pragma once
#include "Halide.h"
#include <iostream>
#include <iso646.h>
#include <map>
#include <optional>
#include <regex>
#include <set>

static_assert(__cplusplus >= 201703L, "requires c++17");

namespace {
using namespace Halide;
using namespace Halide::Internal;

struct HexagonInstrumentation : IRMutator {
  HexagonInstrumentation(const std::string &generator_name,
                         std::vector<Argument> &&program_arguments,
                         std::vector<Func> &&program_outputs,
                         const std::vector<std::string> &schedule_desc)
      : generator_name(generator_name),
        program_arguments(std::move(program_arguments)),
        program_outputs(std::move(program_outputs)),
        schedule_desc(schedule_desc) {}

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
        if (arg.is_scalar()) {
          std::stringstream type;
          type << arg.type;

          if (arg.type.is_float()) {
            stmts.push_back(Evaluate::make(
                Call::make(Handle(), "trace_parameter_float",
                           {Expr{arg.name}, Variable::make(arg.type, arg.name),
                            Expr{type.str()}, Expr{false}},
                           Call::Extern)));
          } else {
            stmts.push_back(Evaluate::make(Call::make(
                Handle(), "trace_parameter_int",
                {Expr{arg.name},
                 Cast::make(Int(32), Variable::make(arg.type, arg.name)),
                 Expr{type.str()}, Expr{false}},
                Call::Extern)));
          }
        } else {
          stmts.push_back(Evaluate::make(Call::make(
              Handle(), "trace_parameter_buffer",
              {Expr{arg.name}, Variable::make(Handle(), arg.name + ".buffer"),
               Expr{false}},
              Call::Extern)));
        }
      }
      for (auto &output : program_outputs) {
        stmts.push_back(Evaluate::make(Call::make(
            Handle(), "trace_parameter_buffer",
            {Expr{output.name()},
             Variable::make(Handle(), output.name() + ".buffer"), Expr{true}},
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
  auto visit(const For *loop) -> Stmt override {
    auto id = node_id_generator++;

    if (loop->is_parallel()) {
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
      return Block::make({loop_start_stmt(id, loop->name),
                          IRMutator::visit(loop), loop_end_stmt()});
    }
  }

  bool passed_entry_point = false;
  uint32_t node_id_generator = 0;

  const std::string generator_name;
  const std::vector<Argument> program_arguments;
  const std::vector<Func> program_outputs;
  const std::vector<std::string> schedule_desc;

private:
  static auto program_start_stmt(uint32_t root_loop_id,
                                 const std::string &label) -> Stmt {
    return Evaluate::make(Call::make(Handle(), "program_start",
                                     {Expr{root_loop_id}, Expr{label}},
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
} // namespace

template <class Gen> class InstrumentedGenerator : public Generator<Gen> {
  virtual auto regex_patterns() const -> std::vector<std::string> {
    return {"$\b"};
  }

  auto build_pipeline() -> Pipeline override {
    auto pipeline = Generator<Gen>::build_pipeline();

    auto generator_name = std::regex_replace(
        std::regex_replace(__PRETTY_FUNCTION__,
                           std::regex(">::build_pipeline.*"), ""),
        std::regex("virtual Halide::Pipeline InstrumentedGenerator<"), "");
    auto schedule_desc = [&] {
      std::vector<std::string> lines;

      std::stringstream buffer;
      std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());
      auto dtor =
          std::shared_ptr<void>{nullptr, [&](auto) { std::cerr.rdbuf(old); }};
      pipeline.print_loop_nest();

      for (std::string line; std::getline(buffer, line);) {
        lines.emplace_back(std::move(line));
      }

      return lines;
    }();

    pipeline.add_custom_lowering_pass(
        new HexagonInstrumentation(generator_name, pipeline.infer_arguments(),
                                   pipeline.outputs(), schedule_desc));

    return pipeline;
  }
};
