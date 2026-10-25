#include "StmtToHTML.h"
#include "Debug.h"
#include "Error.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "Module.h"
#include "Scope.h"
#include "Substitute.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

// Setting this to 0 is meant to speed up the development iteration cycle.
// Not inlining these template files, but just linking them by an absolute path
// causes you to be able to just edit the files without having to recompile Halide
// and then rerun your generator.
// For distribution purposes, they should be inlined, and this define should be on 1.
#define INLINE_TEMPLATES 1

#if !INLINE_TEMPLATES
#include <filesystem>
#endif

extern "C" unsigned char halide_html_template_StmtToHTML_dependencies_html[];
extern "C" unsigned char halide_html_template_StmtToHTML_css[];
extern "C" unsigned char halide_html_template_StmtToHTML_js[];

namespace Halide {
namespace Internal {

/** IRCostModel
 * A basic cost model for Halide IR. Estimates computation
 * cost through simple op-counting and data-movement cost
 * by counting the number of bits being moved.
 */
class IRCostModel : public IRVisitor {
public:
    IRCostModel() = default;

    // Pre-compute all costs to avoid repeated work
    void compute_all_costs(const Module &m) {
        // Compute all node costs
        for (const auto &fn : m.functions()) {
            fn.body.accept(this);
        }
    }

    void compute_conceptual_costs(const Module &m) {
        m.get_conceptual_stmt().accept(this);
    }

    void finalize_cost_computation() {
        // Compute the max cost for each category
        max_compute_cost = -1;
        for (auto const &entry : compute_cost) {
            max_compute_cost = std::max(entry.second, max_compute_cost);
        }

        max_data_cost = -1;
        for (auto const &entry : data_cost) {
            max_data_cost = std::max(entry.second, max_data_cost);
        }

        max_compute_cost_inclusive = -1;
        for (auto const &entry : compute_cost_inclusive) {
            max_compute_cost_inclusive = std::max(entry.second, max_compute_cost_inclusive);
        }

        max_data_cost_inclusive = -1;
        for (auto const &entry : data_cost_inclusive) {
            max_data_cost_inclusive = std::max(entry.second, max_data_cost_inclusive);
        }
    }

    // Whether the costs of a node are known
    bool has_cost(const IRNode *node) const {
        return compute_cost.count(node) != 0;
    }

    // Returns the compute cost of a node (estimated using simple op-counting)
    int get_compute_cost(const IRNode *node, bool include_subtree_cost) {
        internal_assert(node != nullptr) << "IRCostModel::get_compute_cost(): node is nullptr\n";

        int cost = -1;
        if (compute_cost.count(node)) {
            cost = include_subtree_cost ? compute_cost_inclusive[node] : compute_cost[node];
        } else {
            internal_error << "IRCostModel::get_compute_cost(): cost lookup failed\n";
        }

        internal_assert(cost >= 0) << "Cost must not be negative.\n";
        return cost;
    }

    // Returns the data movement cost of a node (the number of bits moved in load/store/shuffle ops)
    int get_data_movement_cost(const IRNode *node, bool include_subtree_cost) {
        internal_assert(node != nullptr) << "IRCostModel::get_data_movement_cost(): node is nullptr\n";

        int cost = -1;
        if (compute_cost.count(node)) {
            cost = include_subtree_cost ? data_cost_inclusive[node] : data_cost[node];
        } else {
            internal_error << "IRCostModel::get_data_movement_cost(): cost lookup failed\n";
        }

        internal_assert(cost >= 0) << "Cost cost must not be negative.\n";
        return cost;
    }

    // Returns the max compute cost of any node in the program
    int get_max_compute_cost(bool include_subtree_cost) {
        return include_subtree_cost ? max_compute_cost_inclusive : max_compute_cost;
    }

    // Returns the max data movement cost of any node in the program
    int get_max_data_movement_cost(bool include_subtree_cost) {
        return include_subtree_cost ? max_data_cost_inclusive : max_data_cost;
    }

private:
    // Cost database. We track two costs:
    //  - The line cost of a node is the sum of the node cost
    //    plus the cost of any children that are printed on
    //    a single line (since we display cost by each line in
    //    the program)
    //  - The inclusive cost is the cost of the entire sub-tree.
    //    We display this cost when the user collapses a program
    //    block in the IR.
    std::map<const IRNode *, int> compute_cost;
    std::map<const IRNode *, int> data_cost;

    std::map<const IRNode *, int> compute_cost_inclusive;
    std::map<const IRNode *, int> data_cost_inclusive;

    // We also track the max costs to determine the cost color
    // intensity for a given line of code
    int max_compute_cost = -1;
    int max_data_cost = -1;

    int max_compute_cost_inclusive = -1;
    int max_data_cost_inclusive = -1;

    /* Utility functions to store node costs in the cost database */
    void set_compute_costs(const IRNode *node, int node_cost, const std::vector<const IRNode *> &child_nodes) {
        set_compute_costs(node, node_cost, child_nodes, child_nodes);
    }

    void set_compute_costs(const IRNode *node, int node_cost, const std::vector<const IRNode *> &child_nodes, const std::vector<const IRNode *> &inline_child_nodes) {
        int subtree_cost = 0;
        for (const IRNode *child_node : child_nodes) {
            // Certain child nodes can be null. Ex: else-case
            // in an if statement
            if (child_node) {
                subtree_cost += get_compute_cost(child_node, true);
            }
        }

        int line_cost = node_cost;
        for (const IRNode *child_node : inline_child_nodes) {
            if (child_node) {
                line_cost += get_compute_cost(child_node, true);
            }
        }

        compute_cost[node] = line_cost;
        compute_cost_inclusive[node] = node_cost + subtree_cost;
    }

    void set_data_costs(const IRNode *node, int node_cost, const std::vector<const IRNode *> &child_nodes) {
        set_data_costs(node, node_cost, child_nodes, child_nodes);
    }

    void set_data_costs(const IRNode *node, int node_cost, const std::vector<const IRNode *> &child_nodes, const std::vector<const IRNode *> &inline_child_nodes) {
        int subtree_cost = 0;
        for (const IRNode *child_node : child_nodes) {
            // Certain child nodes can be null. Ex: else-case
            // in an if statement
            if (child_node) {
                subtree_cost += get_data_movement_cost(child_node, true);
            }
        }

        int line_cost = node_cost;
        for (const IRNode *child_node : inline_child_nodes) {
            if (child_node) {
                line_cost += get_data_movement_cost(child_node, true);
            }
        }

        data_cost[node] = line_cost;
        data_cost_inclusive[node] = node_cost + subtree_cost;
    }

    /* Visitor functions for each IR node */
    using IRVisitor::visit;

    void visit(const IntImm *op) override {
        set_compute_costs(op, 0, {});
        set_data_costs(op, 0, {});
    }

    void visit(const UIntImm *op) override {
        set_compute_costs(op, 0, {});
        set_data_costs(op, 0, {});
    }

    void visit(const FloatImm *op) override {
        set_compute_costs(op, 0, {});
        set_data_costs(op, 0, {});
    }

    void visit(const StringImm *op) override {
        set_compute_costs(op, 0, {});
        set_data_costs(op, 0, {});
    }

    void visit(const Variable *op) override {
        set_compute_costs(op, 0, {});
        set_data_costs(op, 0, {});
    }

    void visit(const Cast *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->value.get()});
        set_data_costs(op, 0, {op->value.get()});
    }

    void visit(const Reinterpret *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->value.get()});
        set_data_costs(op, 0, {op->value.get()});
    }

    void visit(const Add *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Sub *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Mul *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Div *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Mod *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Min *op) override {
        IRVisitor::visit(op);
        // This cost model treats min as a single op
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Max *op) override {
        IRVisitor::visit(op);
        // This cost model treats max as a single op
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const EQ *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const NE *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const LT *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const LE *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const GT *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const GE *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const And *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Or *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get(), op->b.get()});
        set_data_costs(op, 0, {op->a.get(), op->b.get()});
    }

    void visit(const Not *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->a.get()});
        set_data_costs(op, 0, {op->a.get()});
    }

    void visit(const Select *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, op->type.lanes(), {op->condition.get(), op->true_value.get(), op->false_value.get()});
        set_data_costs(op, 0, {op->condition.get(), op->true_value.get(), op->false_value.get()});
    }

    void visit(const Load *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->predicate.get(), op->index.get()});
        set_data_costs(op, op->type.bits() * op->type.lanes(), {op->predicate.get(), op->index.get()});
    }

    void visit(const Ramp *op) override {
        // The cost of a Ramp is higher when the stride is not 1,
        // but currently the cost model does not consider such cases
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->base.get(), op->stride.get()});
        set_data_costs(op, 0, {op->base.get(), op->stride.get()});
    }

    void visit(const Broadcast *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 1, {op->value.get()});
        set_data_costs(op, 0, {op->value.get()});
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        std::vector<const IRNode *> args;
        args.reserve(op->args.size());
        for (const auto &arg : op->args) {
            args.push_back(arg.get());
        }
        set_compute_costs(op, 1, args);
        // Currently there is no special handling
        // for intrinsics such as `prefetch`
        set_data_costs(op, 0, args);
    }

    void visit(const Let *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->value.get(), op->body.get()});
        set_data_costs(op, 0, {op->value.get(), op->body.get()});
    }

    void visit(const Shuffle *op) override {
        IRVisitor::visit(op);
        std::vector<const IRNode *> args;
        args.reserve(op->vectors.size());
        for (const auto &arg : op->vectors) {
            args.push_back(arg.get());
        }
        set_compute_costs(op, 0, args);
        set_data_costs(op, op->type.bits() * op->type.lanes(), args);
    }

    void visit(const VectorReduce *op) override {
        IRVisitor::visit(op);
        const int factor = op->value.type().lanes() / op->type.lanes();
        const int op_count = op->type.lanes() * (factor - 1);
        set_compute_costs(op, op_count, {op->value.get()});
        set_data_costs(op, 0, {op->value.get()});
    }

    void visit(const LetStmt *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->value.get(), op->body.get()}, {op->value.get()});
        set_data_costs(op, 0, {op->value.get(), op->body.get()}, {op->value.get()});
    }

    void visit(const AssertStmt *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 1, {op->condition.get()});
        set_data_costs(op, 0, {op->condition.get()});
    }

    void visit(const ProducerConsumer *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->body.get()}, {});
        set_data_costs(op, 0, {op->body.get()}, {});
    }

    void visit(const For *op) override {
        // The cost of a loop-node essentially depends on its iteration
        // count. The cost model currently ignores such costs.
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->min.get(), op->max.get(), op->body.get()}, {op->min.get(), op->max.get()});
        set_data_costs(op, 0, {op->min.get(), op->max.get(), op->body.get()}, {op->min.get(), op->max.get()});
    }

    void visit(const Acquire *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 1, {op->semaphore.get(), op->count.get(), op->body.get()}, {op->semaphore.get(), op->count.get()});
        set_data_costs(op, 0, {op->semaphore.get(), op->count.get(), op->body.get()}, {op->semaphore.get(), op->count.get()});
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->predicate.get(), op->value.get(), op->index.get()});
        set_data_costs(op, op->value.type().bits() * op->value.type().lanes(), {op->predicate.get(), op->value.get(), op->index.get()});
    }

    void visit(const Provide *op) override {
        IRVisitor::visit(op);
        std::vector<const IRNode *> args;
        args.reserve(op->values.size() + op->args.size() + 1);
        for (const auto &arg : op->values) {
            args.push_back(arg.get());
        }
        for (const auto &arg : op->args) {
            args.push_back(arg.get());
        }
        args.push_back(op->predicate.get());
        set_compute_costs(op, 0, args, {});
        set_data_costs(op, 0, args, {});
    }

    void visit(const Allocate *op) override {
        // We do not model allocation/de-allocation costs
        IRVisitor::visit(op);
        std::vector<const IRNode *> args_inline;
        args_inline.reserve(op->extents.size() + 2);
        for (const auto &arg : op->extents) {
            args_inline.push_back(arg.get());
        }
        args_inline.push_back(op->new_expr.get());
        std::vector<const IRNode *> args = args_inline;
        args.push_back(op->body.get());
        set_compute_costs(op, 0, args, args_inline);
        set_data_costs(op, 0, args, args_inline);
    }

    void visit(const Free *op) override {
        // We do not model allocation/de-allocation costs
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {});
        set_data_costs(op, 0, {});
    }

    void visit(const Realize *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->condition.get(), op->body.get()}, {op->condition.get()});
        set_data_costs(op, 0, {op->condition.get(), op->body.get()}, {op->condition.get()});
    }

    void visit(const Prefetch *op) override {
        IRVisitor::visit(op);
        std::vector<const IRNode *> args_inline;
        for (const auto &arg : op->bounds) {
            args_inline.push_back(arg.min.get());
        }
        args_inline.push_back(op->condition.get());
        std::vector<const IRNode *> args = args_inline;
        args.push_back(op->body.get());
        set_compute_costs(op, 0, args);
        int elem_size = 0;
        for (const auto &etype : op->types) {
            elem_size += etype.bits() * etype.lanes();
        }
        set_data_costs(op, elem_size, args, args_inline);
    }

    void visit(const Block *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->first.get(), op->rest.get()}, {});
        set_data_costs(op, 0, {op->first.get(), op->rest.get()}, {});
    }

    void visit(const Fork *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->first.get(), op->rest.get()}, {});
        set_data_costs(op, 0, {op->first.get(), op->rest.get()}, {});
    }

    void visit(const IfThenElse *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 1, {op->condition.get(), op->then_case.get()}, {op->condition.get()});
        set_data_costs(op, 0, {op->condition.get(), op->then_case.get()}, {op->condition.get()});
    }

    void visit(const Evaluate *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->value.get()});
        set_data_costs(op, 0, {op->value.get()});
    }

    void visit(const Atomic *op) override {
        IRVisitor::visit(op);
        set_compute_costs(op, 0, {op->body.get()}, {});
        set_data_costs(op, 0, {op->body.get()}, {});
    }
};

namespace {

std::string escape_html(std::string src) {
    src = replace_all(std::move(src), "&", "&amp;");
    src = replace_all(std::move(src), "<", "&lt;");
    src = replace_all(std::move(src), ">", "&gt;");
    src = replace_all(std::move(src), "\"", "&quot;");
    src = replace_all(std::move(src), "/", "&#x2F;");
    src = replace_all(std::move(src), "'", "&#39;");
    return src;
}

bool begins_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

/** DeviceCodePrinter
 * Prints CUDA device code (PTX) in HTML.
 */
class DeviceCodePrinter {
public:
    explicit DeviceCodePrinter(std::ostream &os)
        : stream(os), context_stack(1, 0) {
    }

    // CUDA kernels are embedded into modules as PTX assembly. This
    // routine pretty - prints that assembly format. If link_to_ir, the
    // instructions are linked to the line of IR their debug info points to,
    // and those lines are set in ir_lines.
    void print_cuda_gpu_source_kernels(const std::string &str, bool link_to_ir, std::vector<bool> &ir_lines) {
        print_opening_tag("div", "code ptx");

        int current_id = -1;
        int brace_depth = 0;  // Counting the inline asm braces.
        bool in_func_signature = false;

        std::string current_kernel;
        std::istringstream ss(str);

        uint32_t number = 0, ir_line = 0;
        for (std::string line; std::getline(ss, line);) {
            number++;
            const size_t first = line.find_first_not_of(" \t");
            const std::string_view trimmed = first == std::string::npos ? std::string_view{} : std::string_view(line).substr(first);
            if (begins_with(trimmed, ".loc\t") || begins_with(trimmed, ".loc ")) {
                // .loc <file> <line> <column>
                std::istringstream loc{std::string(trimmed.substr(4))};
                uint32_t file = 0;
                loc >> file >> ir_line;
                continue;
            }
            if (begins_with(trimmed, ".file\t") || begins_with(trimmed, ".file ")) {
                continue;
            }
            if (line.empty() || line == "\t") {
                stream << "<span class='line' data-n='" << number << "'></span>\n";
                continue;
            }
            const bool is_instruction = first != 0 && !trimmed.empty() &&
                                        trimmed[0] != '.' && trimmed[0] != '/' &&
                                        trimmed[0] != '{' && trimmed[0] != '}' &&
                                        trimmed.back() != ':';
            bool indent = false;
            if (line[0] == '\t') {
                // Replace first tab with four spaces.
                line = line.substr(1);
                indent = true;
            }
            line = escape_html(line);

            bool should_print_open_indent = false;

            if (starts_with(line, ".visible .entry")) {
                std::vector<std::string> parts = split_string(line, " ");
                if (parts.size() == 3) {
                    in_func_signature = true;
                    brace_depth = 0;
                    current_id = gen_unique_id();
                    print_show_hide_btn_begin(current_id);
                    std::string kernel_name = parts[2].substr(0, parts[2].length() - 1);
                    line = "<span class='keyword'>.visible</span> <span class='keyword'>.entry</span> ";
                    line += kernel_name + " <span class='matched'>(</span>";
                    current_kernel = kernel_name;
                }
            } else if (starts_with(line, ")") && in_func_signature) {
                in_func_signature = false;
                line = "<span class='matched'>)</span>" + line.substr(1);
            } else if (starts_with(line, "{") && brace_depth <= 0) {
                print_opening_brace();
                brace_depth = 1;
                internal_assert(current_id != -1);
                should_print_open_indent = true;
                current_id = -1;
                line = line.substr(1);
                scope.push(current_kernel, gen_unique_id());
            } else if (starts_with(line, "{") && brace_depth) {
                brace_depth++;
            } else if (starts_with(line, "}") && (--brace_depth <= 0)) {
                print_closing_tag("div");
                line = "<span class='matched'>}</span>" + line.substr(1);
                brace_depth = 0;
                scope.pop(current_kernel);
            }

            line = replace_all(std::move(line), ".f32", ".<span class='OpF32'>f32</span>");
            line = replace_all(std::move(line), ".f64", ".<span class='OpF64'>f64</span>");

            line = replace_all(std::move(line), ".s8", ".<span class='OpI8'>s8</span>");
            line = replace_all(std::move(line), ".s16", ".<span class='OpI16'>s16</span>");
            line = replace_all(std::move(line), ".s32", ".<span class='OpI32'>s32</span>");
            line = replace_all(std::move(line), ".s64", ".<span class='OpI64'>s64</span>");

            line = replace_all(std::move(line), ".u8", ".<span class='OpI8'>u8</span>");
            line = replace_all(std::move(line), ".u16", ".<span class='OpI16'>u16</span>");
            line = replace_all(std::move(line), ".u32", ".<span class='OpI32'>u32</span>");
            line = replace_all(std::move(line), ".u64", ".<span class='OpI64'>u64</span>");

            line = replace_all(std::move(line), ".b8", ".<span class='OpB8'>b8</span>");
            line = replace_all(std::move(line), ".b16", ".<span class='OpB16'>b16</span>");
            line = replace_all(std::move(line), ".b32", ".<span class='OpB32'>b32</span>");
            line = replace_all(std::move(line), ".b64", ".<span class='OpB64'>b64</span>");

            line = replace_all(std::move(line), ".v2", ".<span class='OpVec2'>v2</span>");
            line = replace_all(std::move(line), ".v4", ".<span class='OpVec4'>v4</span>");

            line = replace_all(std::move(line), "ld.", "<span class='Memory'>ld</span>.");
            line = replace_all(std::move(line), "st.", "<span class='Memory'>st</span>.");

            size_t idx;
            if ((idx = line.find("&#x2F;&#x2F")) != std::string::npos) {
                line.insert(idx, "<span class='Comment'>");
                line += "</span>";
            }

            // Predicated instructions
            if (!line.empty() && line.front() == '@' && indent) {
                idx = line.find(' ');
                std::string pred = line.substr(1, idx - 1);
                line = "<span class='Pred'>@" + variable(pred, Bool()) + "</span>" + line.substr(idx);
            }

            // Labels (depending on the LLVM version we get L with or without a dollar)
            if (starts_with(line, "$L_") && !indent && (idx = line.find(':')) != std::string::npos) {
                std::string label = line.substr(0, idx);
                line = "<span class='Label'>" + variable(label, "Label") + "</span>:" + line.substr(idx + 1);
            }

            // Highlight operands
            if ((idx = line.find(" \t")) != std::string::npos && line.back() == ';') {
                std::string operands_str = line.substr(idx + 2);
                operands_str = operands_str.substr(0, operands_str.length() - 1);
                std::vector<std::string> operands = split_string(operands_str, ", ");
                operands_str = "";
                for (size_t opidx = 0; opidx < operands.size(); ++opidx) {
                    const std::string &op = operands[opidx];
                    internal_assert(!op.empty());
                    if (opidx != 0) {
                        operands_str += ", ";
                    }
                    if (op.back() == '}') {
                        std::string reg = op.substr(0, op.size() - 1);
                        operands_str += variable(reg, "reg") + '}';
                    } else if (op.front() == '%') {
                        operands_str += variable(op, "");
                    } else if (op.find_first_not_of("-0123456789") == std::string::npos) {
                        operands_str += "<span class='IntImm Imm'>";
                        operands_str += op;
                        operands_str += "</span>";
                    } else if (starts_with(op, "0f") &&
                               op.find_first_not_of("0123456789ABCDEF", 2) == std::string::npos) {
                        operands_str += "<span class='FloatImm Imm'>";
                        operands_str += op;
                        operands_str += "</span>";
                    } else if (op.front() == '[' && op.back() == ']') {
                        size_t idx = op.find('+');
                        if (idx == std::string::npos) {
                            std::string reg = op.substr(1, op.size() - 2);
                            operands_str += '[' + variable(reg, "reg") + ']';
                        } else {
                            std::string reg = op.substr(1, idx - 1);
                            std::string offset = op.substr(idx + 1);
                            offset = offset.substr(0, offset.size() - 1);
                            operands_str += '[' + variable(reg, "reg") + "+";
                            operands_str += "<span class='IntImm Imm'>";
                            operands_str += offset;
                            operands_str += "</span>";
                            operands_str += ']';
                        }
                    } else if (op.front() == '{') {
                        std::string reg = op.substr(1);
                        operands_str += '{' + variable(reg, "reg");
                    } else if (starts_with(op, "$L_")) {
                        // Labels
                        operands_str += "<span class='Label'>" + variable(op, "Label") + "</span>";
                    } else {
                        operands_str += op;
                    }
                }
                operands_str += ";";
                // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
                line = line.substr(0, idx + 2) + operands_str;
            }

            stream << "<span class='line' data-n='" << number << "'";
            if (link_to_ir && is_instruction && ir_line) {
                stream << " data-ir='" << ir_line << "'";
                if (ir_line >= ir_lines.size()) {
                    ir_lines.resize(ir_line + 1, false);
                }
                ir_lines[ir_line] = true;
            }
            stream << ">";
            if (indent) {
                stream << "    ";
            }
            stream << line << "</span>\n";

            // Indent-divs can only be opened after the line is finished.
            if (should_print_open_indent) {
                print_show_hide_btn_end(nullptr);
                print_opening_tag("div", "indent", "", current_id);
            }
        }
        print_closing_tag("div");
    }

private:
    std::ostream &stream;

    // Used to generate unique ids
    int id = 0;

    // Used to track scope during traversal
    Scope<int> scope;

    /* All spans and divs will have an id of the form "x-y", where x
     * is shared among all spans/divs in the same context, and y is unique.
     * These variables are used to track the context within generated HTML */
    std::vector<int> context_stack;
    std::vector<std::string> context_stack_tags;

    int gen_unique_id() {
        return id++;
    }

    // Prints the opening tag for the specified html element. A unique ID is
    // auto-generated unless provided.
    void print_opening_tag(const std::string &tag, const std::string &cls, const std::string &tooltip = "", int id = -1) {
        stream << "<" << tag << " class='" << cls << "'";
        if (!tooltip.empty()) {
            stream << " title='" << tooltip << "'";
        }
        stream << " id='";
        if (id == -1) {
            stream << context_stack.back() << "-";
            stream << gen_unique_id();
        } else {
            stream << id;
        }
        stream << "'>";
        context_stack.push_back(gen_unique_id());
        context_stack_tags.push_back(tag);
    }

    // Prints the closing tag for the specified html element.
    void print_closing_tag(const std::string &tag) {
        internal_assert(!context_stack.empty() && tag == context_stack_tags.back())
            << "closing tag: " << tag << ", stack size: " << context_stack.size() << ", last element: " << (context_stack_tags.empty() ? "null" : context_stack_tags.back());
        context_stack.pop_back();
        context_stack_tags.pop_back();
        stream << "</" + tag + ">";
    }

    // Prints an html element: opening tag, body and closing tag
    void print_html_element(const std::string &tag, const std::string &cls, const std::string &body, const std::string &tooltip = "", int id = -1) {
        print_opening_tag(tag, cls, tooltip, id);
        stream << body;
        print_closing_tag(tag);
    }

    void print_opening_brace() {
        print_html_element("span", "matched OpeningBrace", "{");
    }

    // Prints the opening/closing tags for an anchor that toggles code block view
    void print_show_hide_btn_begin(int id, bool collapsed = false) {
        stream << "<input type=checkbox id='show-hide-btn-" << id << "' class='show-hide-btn'";
        if (collapsed) {
            stream << " checked";
        }
        stream << "/>";
        stream << "<label for='show-hide-btn-" << id << "'>";
    }

    void print_show_hide_btn_end(const IRNode *) {
        stream << "</label><div class='op-btns'></div>";
    }

    std::string variable(const std::string &x, const std::string &tooltip) {
        int id;
        if (const int *i = scope.find(x)) {
            id = *i;
        } else {
            id = gen_unique_id();
            scope.push(x, id);
        }
        std::ostringstream s;
        s << "<b class='variable matched' id='" << id << "-" << gen_unique_id() << "'";
        if (!tooltip.empty()) {
            s << " title='" << tooltip << "'";
        }
        s << ">" << x << "</b>";
        return s.str();
    }

    std::string variable(const std::string &x, Type type) {
        std::ostringstream ss;
        ss << type;
        return variable(x, ss.str());
    }
};

// Writes text as the contents of a JavaScript string literal, which can
// appear in a <script>.
void write_js_string(std::ostream &stream, std::string_view text) {
    stream << '"';
    for (size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (c == '"' || c == '\\') {
            stream << '\\' << c;
        } else if (c == '\t') {
            stream << "\\t";
        } else if ((unsigned char)c < ' ') {
            stream << "\\x" << "0123456789abcdef"[(c >> 4) & 15] << "0123456789abcdef"[c & 15];
        } else if (c == '/' && i > 0 && text[i - 1] == '<') {
            stream << "\\/";
        } else {
            stream << c;
        }
    }
    stream << '"';
}

/** Where the code generated for each line of a stmt went, in some assembly
 * with debug info that points into the stmt (or into a stmt it was lowered
 * from). */
struct AssemblyMap {
    /** The lines of the assembly worth showing: all of them except the debug
     * info, i.e. the directives that carry it and the sections that hold it. */
    std::vector<std::string> lines;
    /** For each shown line, its 1-based line number in the assembly. */
    std::vector<uint32_t> asm_lines;
    /** For each shown line, the 1-based line of the stmt it was generated
     * for, or 0. */
    std::vector<uint32_t> stmt_lines;
};

// The debug info in the assembly says where each instruction came from. To
// line maps such a line and column to the line of the stmt that is shown.
AssemblyMap map_assembly(std::string_view code, const std::function<uint32_t(uint32_t, uint32_t)> &to_line) {
    AssemblyMap result;
    uint32_t current = 0;
    bool in_debug_section = false;
    uint32_t asm_line = 0;
    size_t start = 0;
    while (start < code.size()) {
        size_t end = code.find('\n', start);
        if (end == std::string_view::npos) {
            end = code.size();
        }
        std::string_view line = code.substr(start, end - start);
        start = end + 1;
        asm_line++;

        size_t first = line.find_first_not_of(" \t");
        std::string_view trimmed = first == std::string_view::npos ? std::string_view{} : line.substr(first);

        if (begins_with(trimmed, ".section")) {
            std::string_view name = trimmed.substr(8);
            name = name.substr(std::min(name.size(), name.find_first_not_of(" \t")));
            in_debug_section = begins_with(name, ".debug_");
        } else if (begins_with(trimmed, ".text") || begins_with(trimmed, ".data")) {
            in_debug_section = false;
        }
        if (in_debug_section) {
            continue;
        }

        if (begins_with(trimmed, ".loc\t") || begins_with(trimmed, ".loc ")) {
            // .loc <file> <line> <column> [flags]
            std::istringstream in{std::string(trimmed.substr(4))};
            uint32_t file = 0, l = 0, column = 0;
            in >> file >> l >> column;
            current = l ? to_line(l, column) : 0;
            continue;
        }
        // The comments that say where variables live are debug info too.
        if (trimmed.find("DEBUG_VALUE:") != std::string_view::npos &&
            (begins_with(trimmed, "#") || begins_with(trimmed, "//") ||
             begins_with(trimmed, ";") || begins_with(trimmed, "@"))) {
            continue;
        }
        if (begins_with(trimmed, ".file\t") || begins_with(trimmed, ".file ") ||
            (begins_with(trimmed, ".Ltmp") && trimmed.back() == ':')) {
            continue;
        }
        if (begins_with(trimmed, ".Lfunc_end")) {
            current = 0;
        }

        // Only instructions are code; labels and directives are not.
        const bool is_instruction = first != 0 && !trimmed.empty() &&
                                    trimmed[0] != '.' && trimmed[0] != '#' && trimmed[0] != '/' &&
                                    trimmed.back() != ':';
        if (line.length() > 500) {
            // Very long lines in the assembly are typically the
            // _gpu_kernel_sources or other buffers (such as static LUTs) as a
            // raw ASCII block in the assembly. Let's chop that off to make
            // browsers faster when dealing with this.
            result.lines.emplace_back(std::string(line.substr(0, 200)) + "\" # omitted the remainder of the buffer");
        } else {
            result.lines.emplace_back(line);
        }
        result.asm_lines.push_back(asm_line);
        result.stmt_lines.push_back(is_instruction ? current : 0);
    }
    return result;
}

// Emits the cost indicator of a line: the cost of the line itself, and of the
// block of code it starts.
void print_cost_box(std::ostream &stream, int line_cost, int block_cost, int max_line_cost, int max_block_cost, const std::string &prefix) {
    const int num_cost_buckets = 20;
    const auto compand = [](int v) -> int { return (int)std::sqrt(v * 10); };

    int max_cost = std::max(max_line_cost, max_block_cost);  // This should always be the block cost.
    int cost_bin_size = (compand(max_cost) / num_cost_buckets) + 1;

    int line_costc = std::min(compand(line_cost) / cost_bin_size, num_cost_buckets - 1);
    int block_costc = std::min(compand(block_cost) / cost_bin_size, num_cost_buckets - 1);

    std::string line_cost_class = line_cost == 0 ? "CostColorNone" : "CostColor" + std::to_string(line_costc);
    std::string block_cost_class = block_cost == 0 ? "CostColorNone" : "CostColor" + std::to_string(block_costc);
    if (block_cost == line_cost) {
        block_cost_class += " NoChildCost";
    }

    stream << "<div class='cost-btn tooltip-parent line-" << line_cost_class << " block-" << block_cost_class << "'>"
           << "<span class='tooltip' role='tooltip'>" << prefix << line_cost;
    if (line_cost != block_cost) {
        stream << "<br/>Total " << prefix << block_cost;
    }
    stream << "</span></div>";
}

/** Prints the text of a stmt in HTML, one div per line, with the IR nodes and
 * the styled tokens in it as spans. The text is exactly that of the stmt, so
 * lines and columns are those of the stmt file. */
void print_stmt_lines(std::ostream &stream, const StmtSourceMap &src, IRCostModel &cost_model,
                      const std::vector<bool> &has_asm, const std::vector<bool> &has_device_code) {
    const std::string &text = src.text;

    std::vector<uint32_t> line_starts{0};
    for (uint32_t i = 0; i < (uint32_t)text.size(); i++) {
        if (text[i] == '\n') {
            line_starts.push_back(i + 1);
        }
    }
    if (line_starts.back() == text.size()) {
        line_starts.pop_back();
    }
    const uint32_t num_lines = (uint32_t)line_starts.size();
    // Where a line ends, not including its newline.
    auto end_of_line = [&](uint32_t line) {
        if (line < num_lines) {
            return line_starts[line] - 1;
        }
        return (uint32_t)text.size() - (!text.empty() && text.back() == '\n');
    };
    auto line_text = [&](uint32_t line) {
        uint32_t b = line_starts[line - 1];
        return std::string_view(text).substr(b, end_of_line(line) - b);
    };

    // Blocks of code: from a line that ends in an opening brace, up to the
    // line that starts with the matching closing brace.
    std::vector<uint32_t> block_end(num_lines + 1, 0);
    {
        std::vector<uint32_t> open;
        for (uint32_t l = 1; l <= num_lines; l++) {
            std::string_view t = line_text(l);
            size_t first = t.find_first_not_of(' ');
            size_t last = t.find_last_not_of(' ');
            if (first == std::string_view::npos) {
                continue;
            }
            if (t[first] == '}' && !open.empty()) {
                block_end[open.back()] = l;
                open.pop_back();
            }
            if (t[last] == '{') {
                open.push_back(l);
            }
        }
    }

    // The Stmt that each line is about: the outermost one anchored on it,
    // apart from those that just hold others.
    std::vector<const StmtSourceMap::Location *> line_stmt(num_lines + 1, nullptr);
    for (const auto &l : src.locations) {
        if (l.node->node_type <= StrongestExprNodeType ||
            l.node->node_type == IRNodeType::Block ||
            l.node->node_type == IRNodeType::Fork ||
            l.line > num_lines) {
            continue;
        }
        const StmtSourceMap::Location *&s = line_stmt[l.line];
        if (!s || l.start < s->start) {
            s = &l;
        }
    }

    // Parallel loops and loops over GPU blocks are shaded, and so are loops
    // over GPU threads, in another color. A shade starts at the column of the
    // outermost such loop around the line, or is -1 if there is none.
    std::vector<int> parallel_column(num_lines + 1, -1), thread_column(num_lines + 1, -1);
    for (uint32_t line = 1; line <= num_lines; line++) {
        const StmtSourceMap::Location *l = line_stmt[line];
        if (!l || l->node->node_type != IRNodeType::For) {
            continue;
        }
        const ForType for_type = ((const For *)l->node)->for_type;
        std::vector<int> *column = nullptr;
        if (for_type == ForType::Parallel || for_type == ForType::GPUBlock) {
            column = &parallel_column;
        } else if (for_type == ForType::GPUThread || for_type == ForType::GPULane) {
            column = &thread_column;
        } else {
            continue;
        }
        const int indent = (int)line_text(line).find_first_not_of(' ');
        for (uint32_t l2 = line; l2 <= std::max(line, block_end[line]); l2++) {
            if ((*column)[l2] < 0) {
                (*column)[l2] = indent;
            }
        }
    }

    // Spans to open and close as the text goes by: one per Expr, and one per
    // token.
    struct Event {
        uint32_t offset;
        bool open;
        // Orders events at the same offset: closes before opens, the
        // innermost span closes first and opens last.
        int64_t order;
        int kind;  // 0: an Expr, 1: a token
        uint32_t index;
    };
    std::vector<Event> events;
    for (uint32_t i = 0; i < (uint32_t)src.locations.size(); i++) {
        const auto &l = src.locations[i];
        if (l.node->node_type <= StrongestExprNodeType && l.start < l.end) {
            events.push_back({l.start, true, -(int64_t)l.end, 0, i});
            events.push_back({l.end, false, -(int64_t)l.start, 1, i});
        }
    }
    for (uint32_t i = 0; i < (uint32_t)src.tokens.size(); i++) {
        const auto &t = src.tokens[i];
        if (t.start < t.end) {
            events.push_back({t.start, true, -(int64_t)t.end, 1, i});
            events.push_back({t.end, false, -(int64_t)t.start, 0, i});
        }
    }
    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        return std::make_tuple(a.offset, a.open, a.order, a.kind) < std::make_tuple(b.offset, b.open, b.order, b.kind);
    });

    // The node that each punctuation token belongs to, so that it can be
    // highlighted along with the rest of that node's punctuation: the
    // innermost node that contains it.
    std::vector<uint32_t> token_group(src.tokens.size(), 0);
    {
        std::vector<uint32_t> order(src.locations.size());
        for (uint32_t i = 0; i < (uint32_t)order.size(); i++) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](uint32_t i, uint32_t j) {
            const auto &a = src.locations[i], &b = src.locations[j];
            return a.start != b.start ? a.start < b.start : a.end > b.end;
        });
        std::vector<uint32_t> stack;
        size_t next = 0;
        for (uint32_t t = 0; t < (uint32_t)src.tokens.size(); t++) {
            const auto &token = src.tokens[t];
            while (next < order.size() && src.locations[order[next]].start <= token.start) {
                const auto &l = src.locations[order[next]];
                while (!stack.empty() && src.locations[stack.back()].end <= l.start) {
                    stack.pop_back();
                }
                stack.push_back(order[next++]);
            }
            while (!stack.empty() && src.locations[stack.back()].end < token.end) {
                stack.pop_back();
            }
            token_group[t] = stack.empty() ? 0 : stack.back() + 1;
        }
    }

    auto open_tag = [&](const Event &e) {
        if (e.kind == 0) {
            const auto *node = (const BaseExprNode *)src.locations[e.index].node;
            std::ostringstream type;
            type << node->type;
            stream << "<span class='e' title='" << type.str() << "'>";
            return;
        }
        const auto &t = src.tokens[e.index];
        switch (t.style) {
        case 'k':
            stream << "<span class='kw'>";
            break;
        case 'v':
            stream << "<span class='var'>";
            break;
        case 'b':
            stream << "<span class='buf'>";
            break;
        case 'n':
            stream << "<span class='fn'>";
            break;
        case 't':
            stream << "<span class='type'>";
            break;
        case 'i':
        case 'f':
            stream << "<span class='num'>";
            break;
        case 's':
            stream << "<span class='str'>";
            break;
        case 'h':
            stream << "<span class='hl'>";
            break;
        case 'd':
            stream << "<span class='dim'>";
            break;
        default:
            stream << "<span class='op d" << (int)t.depth << (t.style == 'P' ? " bold" : "")
                   << "' data-g='" << token_group[e.index] << "'>";
            break;
        }
    };

    stream << "<div class='ir-code'>\n";
    std::vector<const Event *> open;
    size_t next_event = 0;
    for (uint32_t line = 1; line <= num_lines; line++) {
        stream << "<div class='l";
        if (parallel_column[line] >= 0) {
            stream << " in-par";
        }
        if (thread_column[line] >= 0) {
            stream << " in-thr";
        }
        stream << "' data-line='" << line << "'";
        if (parallel_column[line] >= 0 || thread_column[line] >= 0) {
            stream << " style='";
            if (parallel_column[line] >= 0) {
                stream << "--par:" << parallel_column[line] << ";";
            }
            if (thread_column[line] >= 0) {
                stream << "--thr:" << thread_column[line] << ";";
            }
            stream << "'";
        }
        if (block_end[line]) {
            stream << " data-block-end='" << block_end[line] << "'";
        }
        if (line < has_asm.size() && has_asm[line]) {
            stream << " data-asm='1'";
        }
        if (line < has_device_code.size() && has_device_code[line]) {
            stream << " data-device='1'";
        }
        stream << ">";

        // The gutter: line number, collapse toggle, costs, device code.
        stream << "<span class='g'><span class='n'>" << line << "</span>";
        stream << (block_end[line] ? "<span class='t'></span>" : "<span class='t none'></span>");
        stream << "<span class='costs'>";
        const StmtSourceMap::Location *s = line_stmt[line];
        if (s && cost_model.has_cost(s->node)) {
            const IRNode *op = s->node;
            bool own_cost_only = op->node_type == IRNodeType::LetStmt || op->node_type == IRNodeType::Allocate;
            int line_cost = cost_model.get_compute_cost(op, false);
            int block_cost = own_cost_only ? line_cost : cost_model.get_compute_cost(op, true);
            print_cost_box(stream, line_cost, block_cost, cost_model.get_max_compute_cost(false), cost_model.get_max_compute_cost(true), "Op Count: ");
            line_cost = cost_model.get_data_movement_cost(op, false);
            block_cost = own_cost_only ? line_cost : cost_model.get_data_movement_cost(op, true);
            print_cost_box(stream, line_cost, block_cost, cost_model.get_max_data_movement_cost(false), cost_model.get_max_data_movement_cost(true), "Bits Moved: ");
        }
        stream << "</span></span>";

        stream << "<span class='c'>";
        // Reopen the spans that continue from the previous line.
        for (const Event *e : open) {
            open_tag(*e);
        }

        const uint32_t line_end = end_of_line(line);
        for (uint32_t offset = line_starts[line - 1]; offset <= line_end; offset++) {
            while (next_event < events.size() && events[next_event].offset == offset) {
                const Event &e = events[next_event++];
                if (e.open) {
                    open_tag(e);
                    open.push_back(&e);
                    continue;
                }
                // Close the span, and any that it contains that are
                // (unexpectedly) still open.
                auto it = std::find_if(open.rbegin(), open.rend(), [&](const Event *o) {
                    return o->kind == (e.kind == 0 ? 1 : 0) && o->index == e.index;
                });
                if (it == open.rend()) {
                    continue;
                }
                size_t keep = open.size() - 1 - (it - open.rbegin());
                for (size_t i = keep; i < open.size(); i++) {
                    stream << "</span>";
                }
                std::vector<const Event *> reopen(open.begin() + keep + 1, open.end());
                open.resize(keep);
                for (const Event *o : reopen) {
                    open_tag(*o);
                    open.push_back(o);
                }
            }
            if (offset == line_end) {
                break;
            }
            switch (char c = text[offset]) {
            case '&':
                stream << "&amp;";
                break;
            case '<':
                stream << "&lt;";
                break;
            case '>':
                stream << "&gt;";
                break;
            default:
                if ((unsigned char)c < ' ' && c != '\t') {
                    // Show control characters (e.g. the terminator of an
                    // embedded string) as the symbols for them.
                    stream << "&#" << (0x2400 + (int)c) << ";";
                } else {
                    stream << c;
                }
            }
        }
        for (size_t i = 0; i < open.size(); i++) {
            stream << "</span>";
        }
        stream << "</span>";
        stream << "</div>\n";
    }
    stream << "</div>\n";
}

/** PipelineHTMLInspector Class
 * Generates the output html page. Currently the html page has
 * three key tabs: IR code, Visualized pipeline and the generated
 * assembly.
 */
class PipelineHTMLInspector {
public:
    // Construct the visualizer and point it to the output file
    // Shows the conceptual stmt if show_conceptual is set, otherwise stmt.
    // The debug info of the assembly points into the conceptual stmt if
    // assembly_in_conceptual is set, otherwise into stmt.
    explicit PipelineHTMLInspector(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename,
                                   const StmtSourceMap &stmt,
                                   const StmtSourceMap *conceptual_stmt,
                                   bool show_conceptual,
                                   bool assembly_in_conceptual)
        : stmt(stmt), conceptual_stmt(show_conceptual ? conceptual_stmt : nullptr),
          assembly_conceptual_stmt(assembly_in_conceptual ? conceptual_stmt : nullptr) {
        // Open output file
        stream.open(html_output_filename.c_str());

        // Load assembly code -- if not explicit specified, assume it will have matching pathname
        // as our output file, with a different extension.
        if (assembly_input_filename.empty()) {
            // get_output_info() is the One True Source Of Truth for expected file suffixes,
            // so use that rather than hardcoding it here.
            const auto info = get_output_info(m.target());
            const std::string stmt_html_extension = info.at(OutputFileType::stmt_html).extension;
            const size_t pos = html_output_filename.rfind(stmt_html_extension);
            user_assert(pos != std::string::npos)
                << "Unable to find expected extension (" << html_output_filename
                << ") in filename (" << html_output_filename << ")\n";

            const std::string asm_extension = info.at(OutputFileType::assembly).extension;
            const std::string asm_file = html_output_filename.substr(0, pos) + asm_extension;
            load_asm_code(asm_file);
        } else {
            load_asm_code(assembly_input_filename);
        }
    }

    // Generate the html visualization of the input module
    void generate_html(const Module &m) {
        // The debug info in the assembly says which line of the stmt each
        // instruction was generated for.
        if (conceptual_stmt && !assembly_conceptual_stmt) {
            // Through the nodes the stmt has in common with the conceptual
            // stmt it was lowered from, or their nearest enclosing nodes.
            std::vector<const StmtSourceMap::Location *> from_conceptual = map_locations(stmt, *conceptual_stmt);
            host_asm = map_assembly(asm_buffer, [&](uint32_t line, uint32_t column) {
                const StmtSourceMap::Location *at = stmt.at(line, column);
                const StmtSourceMap::Location *c = at ? from_conceptual[at - stmt.locations.data()] : nullptr;
                return c ? c->line : 0;
            });
        } else if (!conceptual_stmt && assembly_conceptual_stmt) {
            // Through the nodes the conceptual stmt has in common with the
            // stmt it was lowered to.
            host_asm = map_assembly(asm_buffer, [&](uint32_t line, uint32_t column) {
                const StmtSourceMap::Location *at = assembly_conceptual_stmt->at(line, column);
                const StmtSourceMap::Location *f = at ? stmt.find(at->node, 0) : nullptr;
                return f ? f->line : 0;
            });
        } else {
            host_asm = map_assembly(asm_buffer, [](uint32_t line, uint32_t) { return line; });
        }

        // The device code, with debug info, points into the conceptual
        // stmt, which still holds the code of the kernels.
        Buffer<> device_code_buf = m.get_device_code_buffer();
        if (device_code_buf.defined()) {
            generate_device_code_pane(device_code_buf);
        }

        // Run the cost model over this module to pre-compute all
        // node costs
        if (conceptual_stmt) {
            cost_model.compute_conceptual_costs(m);
        } else {
            cost_model.compute_all_costs(m);
        }
        cost_model.finalize_cost_computation();

        // Generate html page
        stream << "<!DOCTYPE html>\n";
        stream << "<html lang='en'>\n";
        generate_head(m);
        generate_body(m);
        stream << "</html>";
    }

private:
    // Handle to output file stream
    std::ofstream stream;

    // Holds cost information for visualized program
    IRCostModel cost_model;

    // The IR as it is printed in the stmt files. The conceptual stmt is set
    // if it is shown.
    const StmtSourceMap &stmt;
    const StmtSourceMap *conceptual_stmt;

    // The conceptual stmt, if the debug info of the assembly points into it.
    const StmtSourceMap *assembly_conceptual_stmt;

    /* Methods for generating the <head> section of the html file */
    void generate_head(const Module &m) {
        stream << "<head>\n";
        stream << "<title>Halide Module: " << m.name() << "</title>\n";
        stream << halide_html_template_StmtToHTML_dependencies_html;
#if INLINE_TEMPLATES
        stream << "<style type='text/css'>\n"
               << halide_html_template_StmtToHTML_css
               << "\n</style>\n";
#else
        std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path() / "irvisualizer";
        debug(1) << "Will link CSS in directory: " << dir << "\n";
        internal_assert(std::filesystem::exists(dir));
        stream << "<link rel='stylesheet' href='file://" << (dir / "html_template_StmtToHTML.css").string() << "'>\n";
#endif
        stream << "</head>\n";
    }

    /* Methods for generating the <body> section of the html file */
    void generate_body(const Module &m) {
        stream << "<body>\n";
        stream << "  <div id='page-container'>\n";
        generate_toolbar(m);
        stream << "     <div id='content'>\n";
        generate_visualization_panes(m);
        stream << "     </div>\n";
        stream << "  </div>\n";

        // The shown lines of the host assembly, which the page renders as
        // they come into view: their text, their line numbers in the assembly
        // file as the steps from one to the next, and the line of IR each was
        // generated for.
        stream << "<script>\nconst asmLines = [";
        for (size_t i = 0; i < host_asm.lines.size(); i++) {
            stream << (i ? ",\n" : "");
            write_js_string(stream, host_asm.lines[i]);
        }
        stream << "];\nconst asmLineSteps = [";
        for (size_t i = 0; i < host_asm.asm_lines.size(); i++) {
            stream << (i ? "," : "") << host_asm.asm_lines[i] - (i ? host_asm.asm_lines[i - 1] : 0);
        }
        stream << "];\nconst asmIRLines = [";
        for (size_t i = 0; i < host_asm.stmt_lines.size(); i++) {
            stream << (i ? "," : "") << host_asm.stmt_lines[i];
        }
        // How comments and immediates look in the assembly of this target.
        const Target &t = m.target();
        const bool arm32 = t.arch == Target::ARM && t.bits == 32;
        const char *comment = (t.arch == Target::ARM && !arm32) || t.arch == Target::Hexagon ? "//" :
                              arm32                                                          ? "@" :
                                                                                               "#";
        stream << "];\nconst asmSyntax = {comment: \"" << comment << "\", immediate: \""
               << (t.arch == Target::X86 ? "$" : "#") << "\", x86: " << (t.arch == Target::X86 ? "true" : "false")
               << "};\n</script>\n";

#if INLINE_TEMPLATES
        stream << "<script>\n"
               << halide_html_template_StmtToHTML_js
               << "</script>";
#else
        std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path() / "irvisualizer";
        debug(1) << "Will link Javascript in directory: " << dir << "\n";
        internal_assert(std::filesystem::exists(dir));
        stream << "<script src='file://" << (dir / "html_template_StmtToHTML.js").string() << "'></script>\n";
#endif
        stream << "</body>";
    }

    void generate_toolbar(const Module &m) {
        stream << "<div id='toolbar'>\n";

        // IR settings
        stream << "<form id='form-ir-settings' name='form-ir-settings'>IR:\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir-wrap' checked />Wrap</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir-line-nums' checked />Line numbers</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir-costs' />Costs</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-rainbow-parens' />Rainbow Parentheses</label>\n";
        stream << "</form>\n";

        // Which panes to show
        stream << "<form id='form-panes' name='form-panes'>Panes:\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir' checked />Show IR</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-assembly' checked />Show Assembly</label>\n";
        Buffer<> device_code_buf = m.get_device_code_buffer();
        if (device_code_buf.defined()) {
            stream << "   <label><input type='checkbox' name='checkbox-show-device-code' checked />Show Device Code</label>\n";
        }
        stream << "</form>\n";

        // Color theme
        stream << "<form id='form-theme' name='form-theme'>Theme:\n";
        stream << "    <label><input type='radio' name='theme' value='auto' checked />Auto</label>\n";
        stream << "    <label><input type='radio' name='theme' value='classic-dark' />Classic Dark</label>\n";
        stream << "    <label><input type='radio' name='theme' value='classic-light' />Classic Light</label>\n";
        stream << "    <label><input type='radio' name='theme' value='gruvbox-dark' />Gruvbox Dark</label>\n";
        stream << "<label><input type='radio' name='theme' value='gruvbox-light' />Gruvbox Light</label>\n";

        stream << "</form>\n";
        stream << "</div>\n";
    }

    // Generate the three visualization panes
    void generate_visualization_panes(const Module &m) {
        int pane_count = 0;
        stream << "<div id='visualization-panes'>\n";
        stream << "<div id='resizer-preview' style='display:none;'></div>\n";
        generate_ir_pane(m);
        generate_resize_bar(pane_count++);
        generate_host_assembly_pane(m);
        if (!device_code_pane.empty()) {
            generate_resize_bar(pane_count++);
            stream << device_code_pane;
        }

        stream << "</div>\n";
    }

    // Generate pane: Lowered IR code with syntax highlighting in HTML
    void generate_ir_pane(const Module &m) {
        stream << "<div id='ir-code-pane' class='pane" << (conceptual_stmt ? " conceptual" : "") << "'>\n";
        std::vector<bool> has_asm;
        for (uint32_t line : host_asm.stmt_lines) {
            if (line >= has_asm.size()) {
                has_asm.resize(line + 1, false);
            }
            has_asm[line] = true;
        }
        print_stmt_lines(stream, conceptual_stmt ? *conceptual_stmt : stmt, cost_model, has_asm, device_code_ir_lines);
        stream << "</div>\n";
    }

    // Generate pane: Generated host assembly code, rendered by the page
    void generate_host_assembly_pane(const Module &m) {
        stream << "<div id='host-assembly-pane' class='pane'>\n";
        stream << "<div id='asm-code'>\n";
        stream << "<div id='asm-highlights'></div>\n";
        stream << "<div id='assemblyContent'></div>\n";
        stream << "</div>\n";
        stream << "</div>\n";
    }

    // Generate pane: Generated device code, into device_code_pane
    void generate_device_code_pane(const Buffer<> &buf) {
        std::ostringstream pane;
        pane << "<div id='device-code-pane' class='pane'>\n";
        int length = buf.size_in_bytes();
        while (length > 0 && ((const char *)buf.data())[length - 1] == '\0') {
            length--;
        }
        std::string str((const char *)buf.data(), length);
        if (starts_with(buf.name(), "cuda_")) {
            DeviceCodePrinter(pane).print_cuda_gpu_source_kernels(str, conceptual_stmt != nullptr, device_code_ir_lines);
        } else {
            std::istringstream ss{str};
            pane << "<div class='code'>\n";
            int number = 0;
            for (std::string line; std::getline(ss, line);) {
                pane << "<span class='line' data-n='" << ++number << "'>" << escape_html(line) << "</span>\n";
            }
            pane << "\n</div>\n";
        }
        pane << "</div>\n";
        device_code_pane = std::move(pane).str();
    }

    // Generate a resizing bar to control the width of code and visualization panes
    void generate_resize_bar(int num) {
        stream << "<div class='resize-bar' id='resize-bar-" << num << "'>\n";
        stream << " <div class='collapse-btns'>\n";
        stream << "  <div>\n";
        stream << "   <button class='collapse-left' onclick='collapseTab(" << num << ")' title='Collapse pane on the left'>\n";
        stream << "   </button>\n";
        stream << "  </div>\n";
        stream << "  <div>\n";
        stream << "    <button class='collapse-right' onclick='collapseTab(" << (num + 1) << ")' title='Collapse pane on the right'>\n";
        stream << "    </button>\n";
        stream << "  </div>\n";
        stream << " </div>\n";
        stream << "</div>\n";
    }

    /* Misc helper methods */

    // Load assembly code from file
    std::string asm_buffer;
    AssemblyMap host_asm;

    // The device code pane, and the lines of IR it links to.
    std::string device_code_pane;
    std::vector<bool> device_code_ir_lines;

    void load_asm_code(const std::string &asm_file) {
        user_assert(file_exists(asm_file)) << "Unable to open assembly file: " << asm_file << "\n";
        std::ifstream assembly(asm_file.c_str());
        std::stringstream ss;
        ss << assembly.rdbuf();
        asm_buffer = std::move(ss).str();
    }
};

}  // namespace

// The external interface to this module
void print_to_stmt_html(const std::string &html_output_filename,
                        const Module &m,
                        const std::string &assembly_input_filename,
                        const StmtSourceMap &stmt,
                        const StmtSourceMap *assembly_conceptual_stmt) {
    PipelineHTMLInspector inspector(html_output_filename, m, assembly_input_filename, stmt,
                                    assembly_conceptual_stmt, false, assembly_conceptual_stmt != nullptr);
    inspector.generate_html(m);
    debug(1) << "Done generating HTML IR Inspector - printed to: " << html_output_filename << "\n";
}

void print_to_conceptual_stmt_html(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename,
                                   const StmtSourceMap &stmt,
                                   const StmtSourceMap &conceptual_stmt,
                                   bool assembly_in_conceptual_stmt) {
    PipelineHTMLInspector inspector(html_output_filename, m, assembly_input_filename, stmt,
                                    &conceptual_stmt, true, assembly_in_conceptual_stmt);
    inspector.generate_html(m);
    debug(1) << "Done generating HTML Conceptual IR Inspector - printed to: " << html_output_filename << "\n";
}

}  // namespace Internal
}  // namespace Halide
