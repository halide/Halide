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

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
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

namespace Halide {
namespace Internal {

extern "C" unsigned char halide_html_template_StmtToHTML_dependencies_html[];
extern "C" unsigned char halide_html_template_StmtToHTML_css[];
extern "C" unsigned char halide_html_template_StmtToHTML_js[];

// Classes defined within this file
class CostModel;
class AssemblyInfo;
template<typename T>
class HTMLCodePrinter;
class PipelineHTMLInspector;

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
        set_compute_costs(op, 0, {op->min.get(), op->extent.get(), op->body.get()}, {op->min.get(), op->extent.get()});
        set_data_costs(op, 0, {op->min.get(), op->extent.get(), op->body.get()}, {op->min.get(), op->extent.get()});
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

/** GetAssemblyInfo
 * Used to map some Halide IR nodes to line-numbers in the
 * assembly file containing the corresponding generated code.
 */
class AssemblyInfo : public IRVisitor {
public:
    AssemblyInfo() = default;

    void gather_nodes_from_functions(const Module &m) {
        // Traverse the module to populate the list of
        // nodes we need to map and generate their assembly
        // markers (comments that appear in the assembly code
        // associating the code with this node)
        ids_are_known = true;
        for (const auto &fn : m.functions()) {
            fn.body.accept(this);
        }
    }

    void gather_nodes_from_conceptual_stmt(const Module &m) {
        // Traverse the module's conceptual Stmt to populate the list of
        // nodes we need to map and generate their assembly
        // markers (comments that appear in the assembly code
        // associating the code with this node)
        ids_are_known = false;
        m.get_conceptual_stmt().accept(this);
    }

    void generate(const std::string &code) {
        // Find markers in asm code
        std::istringstream asm_stream(code);
        std::string line;
        int lno = 1;
        while (getline(asm_stream, line)) {
            // Try all markers
            std::vector<uint64_t> matched_nodes;
            for (auto const &[node, marker] : markers) {
                if (std::regex_search(line, marker)) {
                    // Save line number
                    lnos[node] = lno;
                    // Save this node's id
                    matched_nodes.push_back(node);
                }
            }
            // We map to the first match, stop
            // checking matched nodes
            for (auto const &node : matched_nodes) {
                markers.erase(node);
            }

            lno++;
        }
    }

    int get_asm_lno(uint64_t node_id) {
        if (lnos.count(node_id)) {
            return lnos[node_id];
        }
        return -1;
    }

    std::string get_label(uint64_t node_id) {
        if (labels.count(node_id)) {
            return labels[node_id];
        }
        return "(label not found)";
    }

private:
    // Generate asm markers for Halide loops
    bool ids_are_known{true};
    int loop_id = 0;
    int gen_loop_id() {
        return ++loop_id;
    }

    std::string gen_loop_asm_marker(int id, const std::string &loop_var) {
        std::regex dollar("\\$");
        std::string marker = "%\"";
        if (ids_are_known) {
            marker += std::to_string(id);
        } else {
            marker += "\\d+";
        }
        marker += "_for_" + loop_var;
        marker = std::regex_replace(marker, dollar, "\\$");
        return marker;
    }

    // Generate asm markers for Halide producer/consumer ndoes
    int prodcons_id = 0;
    int gen_prodcons_id() {
        return ++prodcons_id;
    }

    std::string gen_prodcons_asm_marker(int id, const std::string &var, bool is_producer) {
        std::regex dollar("\\$");
        std::string marker = "%\"";
        if (ids_are_known) {
            marker += std::to_string(id);
        } else {
            marker += "\\d+";
        }
        marker += (is_producer ? "_produce_" : "_consume_") + var;
        marker = std::regex_replace(marker, dollar, "\\$");
        return marker;
    }

    // Mapping of IR nodes to their asm markers
    std::map<uint64_t, std::regex> markers;
    std::map<uint64_t, std::string> labels;

    // Mapping of IR nodes to their asm line numbers
    std::map<uint64_t, int> lnos;

    using IRVisitor::visit;

    void visit(const ProducerConsumer *op) override {
        // Generate asm marker
        std::string marker = gen_prodcons_asm_marker(gen_prodcons_id(), op->name, op->is_producer);
        markers[(uint64_t)op] = std::regex(marker);
        labels[(uint64_t)op] = marker;
        // Continue traversal
        IRVisitor::visit(op);
    }

    void visit(const For *op) override {
        // Generate asm marker
        std::string marker = gen_loop_asm_marker(gen_loop_id(), op->name);
        markers[(uint64_t)op] = std::regex(marker);
        labels[(uint64_t)op] = marker;
        // Continue traversal
        IRVisitor::visit(op);
    }
};

/** HTMLCodePrinter
 * Prints IR code in HTML. Very similar to generating a stmt
 * file, except that the generated html is more interactive.
 */
template<typename T>
class HTMLCodePrinter : public IRVisitor {
public:
    HTMLCodePrinter(T &os, std::map<const IRNode *, int> &nids, bool enable_assembly_features)
        : stream(os), node_ids(nids), context_stack(1, 0),
          enable_assembly_features(enable_assembly_features) {
    }

    // Make class non-copyable and non-moveable
    HTMLCodePrinter(const HTMLCodePrinter &) = delete;
    HTMLCodePrinter &operator=(const HTMLCodePrinter &) = delete;

    void init_cost_info(IRCostModel cm) {
        cost_model = std::move(cm);
    }

    void print_conceptual_stmt(const Module &m, AssemblyInfo host_asm_info, AssemblyInfo device_asm_info) {
        host_assembly_info = std::move(host_asm_info);
        device_assembly_info = std::move(device_asm_info);

        // Generate a unique ID for this module
        int id = gen_unique_id();

        // Enter new scope for this module
        scope.push(m.name(), id);

        // Open div to hold this module
        print_opening_tag("div", "Module");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "module");
        print_text(" name=" + m.name() + ", target=" + m.target().to_string());
        print_closing_tag("span");

        // Open code block to hold module body
        print_opening_brace();
        print_show_hide_btn_end(nullptr);

        // Open indented div to hold body code
        print_opening_tag("div", "indent ModuleBody", id);

        print(m.get_conceptual_stmt());

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding module body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this module
        print_closing_tag("div");

        // Pop out to outer scope
        scope.pop(m.name());
    }

    void print(const Module &m, AssemblyInfo host_asm_info, AssemblyInfo device_asm_info) {
        host_assembly_info = std::move(host_asm_info);
        device_assembly_info = std::move(device_asm_info);

        // Generate a unique ID for this module
        int id = gen_unique_id();

        // Enter new scope for this module
        scope.push(m.name(), id);

        // The implementation doesn't need to support submodules:
        // we only call this for Modules that have already had their submodules
        // resolved.
        internal_assert(m.submodules().empty()) << "StmtToHTML does not support submodules.";

        // Open div to hold this module
        print_opening_tag("div", "Module");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "module");
        print_text(" name=" + m.name() + ", target=" + m.target().to_string());
        print_closing_tag("span");

        // Open code block to hold module body
        print_opening_brace();
        print_show_hide_btn_end(nullptr);

        // Open indented div to hold body code
        print_opening_tag("div", "indent ModuleBody", id);

        // Print module buffers
        for (const auto &buf : m.buffers()) {
            print(buf);
        }

        // Print module functions
        for (const auto &fn : m.functions()) {
            print(fn);
        }

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding module body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this module
        print_closing_tag("div");

        // Pop out to outer scope
        scope.pop(m.name());
    }

    inline std::string escape_html(std::string src) {
        src = replace_all(src, "&", "&amp;");
        src = replace_all(src, "<", "&lt;");
        src = replace_all(src, ">", "&gt;");
        src = replace_all(src, "\"", "&quot;");
        src = replace_all(src, "/", "&#x2F;");
        src = replace_all(src, "'", "&#39;");
        return src;
    }

    // CUDA kernels are embedded into modules as PTX assembly. This
    // routine pretty - prints that assembly format.
    void print_cuda_gpu_source_kernels(const std::string &str) {
        print_opening_tag("div", "code ptx");

        int current_id = -1;
        bool in_braces = false;
        bool in_func_signature = false;

        std::string current_kernel;
        std::istringstream ss(str);

        for (std::string line; std::getline(ss, line);) {
            if (line.empty()) {
                stream << "<span class='line'></span>\n";
                continue;
            }
            line = escape_html(line);

            bool should_print_open_indent = false;

            if (starts_with(line, ".visible .entry")) {
                std::vector<std::string> parts = split_string(line, " ");
                if (parts.size() == 3) {
                    in_func_signature = true;
                    current_id = gen_unique_id();
                    print_show_hide_btn_begin(current_id);
                    std::string kernel_name = parts[2].substr(0, parts[2].length() - 1);
                    line = "<span class='keyword'>.visible</span> <span class='keyword'>.entry</span> ";
                    line += variable(kernel_name) + " <span class='matched'>(</span>";
                    current_kernel = kernel_name;
                }
            } else if (starts_with(line, ")") && in_func_signature) {
                in_func_signature = false;
                line = "<span class='matched'>)</span>" + line.substr(1);
            } else if (starts_with(line, "{") && !in_braces) {
                print_opening_brace();
                in_braces = true;
                internal_assert(current_id != -1);
                should_print_open_indent = true;
                current_id = -1;
                line = line.substr(1);
                scope.push(current_kernel, gen_unique_id());
            } else if (starts_with(line, "}") && in_braces) {
                print_closing_tag("div");
                line = "<span class='matched'>}</span>" + line.substr(1);
                in_braces = false;
                scope.pop(current_kernel);
            }

            bool indent = false;

            if (line[0] == '\t') {
                // Replace first tab with four spaces.
                line = line.substr(1);
                indent = true;
            }

            line = replace_all(line, ".f32", ".<span class='OpF32'>f32</span>");
            line = replace_all(line, ".f64", ".<span class='OpF64'>f64</span>");

            line = replace_all(line, ".s8", ".<span class='OpI8'>s8</span>");
            line = replace_all(line, ".s16", ".<span class='OpI16'>s16</span>");
            line = replace_all(line, ".s32", ".<span class='OpI32'>s32</span>");
            line = replace_all(line, ".s64", ".<span class='OpI64'>s64</span>");

            line = replace_all(line, ".u8", ".<span class='OpI8'>u8</span>");
            line = replace_all(line, ".u16", ".<span class='OpI16'>u16</span>");
            line = replace_all(line, ".u32", ".<span class='OpI32'>u32</span>");
            line = replace_all(line, ".u64", ".<span class='OpI64'>u64</span>");

            line = replace_all(line, ".b8", ".<span class='OpB8'>b8</span>");
            line = replace_all(line, ".b16", ".<span class='OpB16'>b16</span>");
            line = replace_all(line, ".b32", ".<span class='OpB32'>b32</span>");
            line = replace_all(line, ".b64", ".<span class='OpB64'>b64</span>");

            line = replace_all(line, ".v2", ".<span class='OpVec2'>v2</span>");
            line = replace_all(line, ".v4", ".<span class='OpVec4'>v4</span>");

            line = replace_all(line, "ld.", "<span class='Memory'>ld</span>.");
            line = replace_all(line, "st.", "<span class='Memory'>st</span>.");

            size_t idx;
            if ((idx = line.find("&#x2F;&#x2F")) != std::string::npos) {
                line.insert(idx, "<span class='Comment'>");
                line += "</span>";
            }

            // Predicated instructions
            if (line.front() == '@' && indent) {
                idx = line.find(' ');
                std::string pred = line.substr(1, idx - 1);
                line = "<span class='Pred'>@" + variable(pred) + "</span>" + line.substr(idx);
            }

            // Labels (depending on the LLVM version we get L with or without a dollar)
            if (starts_with(line, "$L_") && !indent && (idx = line.find(':')) != std::string::npos) {
                std::string label = line.substr(0, idx);
                line = "<span class='Label'>" + variable(label) + "</span>:" + line.substr(idx + 1);
            }

            // Highlight operands
            if ((idx = line.find(" \t")) != std::string::npos && line.back() == ';') {
                std::string operands_str = line.substr(idx + 2);
                operands_str = operands_str.substr(0, operands_str.length() - 1);
                std::vector<std::string> operands = split_string(operands_str, ", ");
                operands_str = "";
                for (size_t opidx = 0; opidx < operands.size(); ++opidx) {
                    std::string op = operands[opidx];
                    internal_assert(!op.empty());
                    if (opidx != 0) {
                        operands_str += ", ";
                    }
                    if (op.back() == '}') {
                        std::string reg = op.substr(0, op.size() - 1);
                        operands_str += variable(reg) + '}';
                    } else if (op.front() == '%') {
                        operands_str += variable(op);
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
                            operands_str += '[' + variable(reg) + ']';
                        } else {
                            std::string reg = op.substr(1, idx - 1);
                            std::string offset = op.substr(idx + 1);
                            offset = offset.substr(0, offset.size() - 1);
                            operands_str += '[' + variable(reg) + "+";
                            operands_str += "<span class='IntImm Imm'>";
                            operands_str += offset;
                            operands_str += "</span>";
                            operands_str += ']';
                        }
                    } else if (op.front() == '{') {
                        std::string reg = op.substr(1);
                        operands_str += '{' + variable(reg);
                    } else if (starts_with(op, "$L_")) {
                        // Labels
                        operands_str += "<span class='Label'>" + variable(op) + "</span>";
                    } else {
                        operands_str += op;
                    }
                }
                operands_str += ";";
                line = line.substr(0, idx + 2) + operands_str;
            }

            stream << "<span class='line'>";
            if (indent) {
                stream << "    ";
            }
            stream << line << "</span>\n";

            // Indent-divs can only be opened after the line is finished.
            if (should_print_open_indent) {
                print_show_hide_btn_end(nullptr);
                print_opening_tag("div", "indent", current_id);
            }
        }
        print_closing_tag("div");
    }

private:
    // Handle to output file stream
    T &stream;

    // Used to generate unique ids
    int id = 0;
    std::map<const IRNode *, int> &node_ids;

    // Used to track scope during IR traversal
    Scope<int> scope;

    /* All spans and divs will have an id of the form "x-y", where x
     * is shared among all spans/divs in the same context, and y is unique.
     * These variables are used to track the context within generated HTML */
    std::vector<int> context_stack;
    std::vector<std::string> context_stack_tags;

    // Holds cost information for visualized program
    IRCostModel cost_model;
    AssemblyInfo host_assembly_info;
    AssemblyInfo device_assembly_info;
    bool enable_assembly_features;

    /* Private print functions to handle various IR types */
    void print(const Buffer<> &buf) {
        // Open div to hold this buffer
        print_opening_tag("div", "Buffer");

        // Print buffer name and move on
        print_html_element("span", "keyword", "buffer ");
        print_variable(buf.name());

        // Close div holding this buffer
        print_closing_tag("div");
    }

    void print(const LoweredFunc &fn) {
        // Generate a unique ID for this function
        int id = gen_unique_id();

        // Enter new scope for this function
        scope.push(fn.name, id);

        // Open div to hold this buffer
        print_opening_tag("div", "Function");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text (fn name and args)
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword ", "func ", "lowered-func-" + fn.name);
        print_text(fn.name + "(");
        print_closing_tag("span");
        print_fndecl_args(fn.args);
        print_html_element("span", "matched", ")");

        // Open code block to hold function body
        print_opening_brace();
        print_show_hide_btn_end(nullptr);

        // Open indented div to hold body code
        print_opening_tag("div", "indent FunctionBody", id);

        // Print function body
        print(fn.body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding func body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this function
        print_closing_tag("div");

        // Pop out to outer scope
        scope.pop(fn.name);
    }

    void print(const Expr &ir) {
        ir.accept(this);
    }

    void print(const Stmt &ir) {
        ir.accept(this);
    }

    /* Methods used to emit common HTML patterns */

    // Prints the opening tag for the specified html element. A unique ID is
    // auto-generated unless provided.
    void print_opening_tag(const std::string &tag, const std::string &cls, int id = -1) {
        stream << "<" << tag << " class='" << cls << "' id='";
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

    void print_opening_tag(const std::string &tag, const std::string &cls, std::string id) {
        stream << "<" << tag << " class='" << cls << "' id='" << id << "'>";
        context_stack.push_back(gen_unique_id());
        context_stack_tags.push_back(tag);
    }

    // Prints the closing tag for the specified html element.
    void print_closing_tag(const std::string &tag) {
        internal_assert(!context_stack.empty() && tag == context_stack_tags.back())
            << tag << " " << context_stack.empty() << " " << context_stack_tags.back();
        context_stack.pop_back();
        context_stack_tags.pop_back();
        stream << "</" + tag + ">";
    }

    // Prints an html element: opening tag, body and closing tag
    void print_html_element(const std::string &tag, const std::string &cls, const std::string &body, int id = -1) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    void print_html_element(const std::string &tag, const std::string &cls, const std::string &body, std::string id) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    void print_opening_brace() {
        print_html_element("span", "matched OpeningBrace", "{");
    }

    void print_closing_brace() {
        print_html_element("span", "matched ClosingBrace", "}");
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

    void print_show_hide_btn_end(const IRNode *op) {
        stream << "</label><div class='op-btns'>";
        if (op) {
            print_assembly_button(op);
        }
        stream << "</div>";
    }

    // Prints newline to stream
    void print_ln() {
        stream << '\n';
    }

    // Prints a variable to stream
    void print_variable(const std::string &x) {
        stream << variable(x);
    }

    std::string variable(const std::string &x) {
        int id;
        if (scope.contains(x)) {
            id = scope.get(x);
        } else {
            id = gen_unique_id();
            scope.push(x, id);
        }
        std::ostringstream s;
        s << "<b class='variable matched' id='" << id << "-" << gen_unique_id() << "'>";
        s << x;
        s << "</b>";
        return s.str();
    }

    // Prints text to stream
    void print_text(const std::string &x) {
        stream << x;
    }

    // Prints a button to sync text with visualization
    void print_assembly_button(const void *op) {
        if (!enable_assembly_features) {
            return;
        }
        {
            int asm_lno = host_assembly_info.get_asm_lno((uint64_t)op);
            if (asm_lno != -1) {
                stream << "<div class='icon-btn jump-to-host-asm-btn tooltip-parent' onclick='scrollToHostAsm(" << asm_lno << ")'>"
                       << "<span class='tooltip'>Jump to Host Assembly"
                       << "<span>" << host_assembly_info.get_label((uint64_t)op) << "</span></span>"
                       << "</div>";
            }
        }
        {
            int asm_lno = device_assembly_info.get_asm_lno((uint64_t)op);
            if (asm_lno != -1) {
                stream << "<div class='icon-btn jump-to-device-code-btn tooltip-parent' onclick='scrollToDeviceCode(" << asm_lno << ")'>"
                       << "<span class='tooltip'>Jump to Device Code"
                       << "<span>" << device_assembly_info.get_label((uint64_t)op) << "</span></span>"
                       << "</div>";
            }
        }
    }

    // Prints the args in a function declaration
    void print_fndecl_args(const std::vector<LoweredArgument> &args) {
        bool print_delim = false;
        for (const auto &arg : args) {
            if (print_delim) {
                print_html_element("span", "matched", ",");
                print_text(" ");
            }
            print_variable(arg.name);
            print_delim = true;
        }
    }

    /* Helper functions for printing IR nodes */
    void print_constant(std::string cls, Expr c) {
        print_opening_tag("span", cls);
        stream << c;
        print_closing_tag("span");
    }

    void print_type(Type t) {
        print_opening_tag("span", "Type");
        stream << t;
        print_closing_tag("span");
    }

    void print_binary_op(const Expr &a, const Expr &b, std::string op) {
        print_opening_tag("span", "BinaryOp");
        print_html_element("span", "matched", "(");
        print(a);
        print_text(" ");
        print_html_element("span", "matched Operator", op);
        print_text(" ");
        print(b);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void print_function_call(std::string fn_name, const std::vector<Expr> &args, int id) {
        print_opening_tag("span", "", "fn-call-" + std::to_string(id));
        print_function_call(fn_name, args);
        print_closing_tag("span");
    }

    void print_function_call(std::string fn_name, const std::vector<Expr> &args) {
        print_opening_tag("span", "matched");
        print_html_element("span", "Symbol matched", fn_name);
        print_text("(");
        print_closing_tag("span");
        bool print_delim = false;
        for (const auto &arg : args) {
            if (print_delim) {
                print_html_element("span", "matched", ", ");
            }
            print(arg);
            print_delim = true;
        }
        print_html_element("span", "matched", ")");
    }

    // To avoid generating ridiculously deep DOMs, we flatten blocks here.
    void print_block_stmt(const Stmt &stmt) {
        if (const Block *b = stmt.as<Block>()) {
            print_block_stmt(b->first);
            print_block_stmt(b->rest);
        } else if (stmt.defined()) {
            print(stmt);
        }
    }

    // We also flatten forks
    void visit_fork_stmt(const Stmt &stmt) {
        if (const Fork *f = stmt.as<Fork>()) {
            visit_fork_stmt(f->first);
            visit_fork_stmt(f->rest);
        } else if (stmt.defined()) {
            // Give task a unique id
            int id = gen_unique_id();

            // Start a dive to hold code for this task
            print_opening_tag("div", "ForkTask");

            // Generate the show hide icon/text buttons
            print_show_hide_btn_begin(id);

            // -- print text
            print_html_element("span", "keyword matched", "task");

            // Open code block to hold task body
            print_opening_brace();
            print_show_hide_btn_end(nullptr);

            // Open indented div to hold body code
            print_opening_tag("div", "indent ForkTask", id);

            // Print task body
            print(stmt);

            // Close indented div holding body code
            print_closing_tag("div");

            // Close code block holding task body
            print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

            // Close div holding this fork task
            print_closing_tag("div");
        }
    }

    // Prints compute and data cost buttons/indicators
    void print_cost_buttons(const IRNode *op) {
        int id = gen_node_id(op);
        print_cost_buttons(op, id);
    }

    void print_cost_buttons(const IRNode *op, int id) {
        print_opening_tag("div", "node-cost");
        print_compute_cost(op, id);
        print_data_movement_cost(op, id);
        print_closing_tag("div");
    }

    // Prints the button/indicator for the compute cost of a line in the program
    void print_compute_cost(const IRNode *op, int id) {
        int max_line_cost = cost_model.get_max_compute_cost(false);
        int max_block_cost = cost_model.get_max_compute_cost(true);
        int line_cost = cost_model.get_compute_cost(op, false);
        int block_cost = cost_model.get_compute_cost(op, true);
        if (dynamic_cast<const LetStmt *>(op) || dynamic_cast<const Allocate *>(op)) {
            block_cost = line_cost;
        }
        std::string _id = "cc-" + std::to_string(id);
        print_cost_btn(line_cost, block_cost, max_line_cost, max_block_cost, _id, "Op Count: ");
    }

    // Prints the button/indicator for the data movement cost of a line in the program
    void print_data_movement_cost(const IRNode *op, int id) {
        int max_line_cost = cost_model.get_max_data_movement_cost(false);
        int max_block_cost = cost_model.get_max_data_movement_cost(true);
        int line_cost = cost_model.get_data_movement_cost(op, false);
        int block_cost = cost_model.get_data_movement_cost(op, true);
        if (dynamic_cast<const LetStmt *>(op) || dynamic_cast<const Allocate *>(op)) {
            block_cost = line_cost;
        }
        std::string _id = "dc-" + std::to_string(id);
        print_cost_btn(line_cost, block_cost, max_line_cost, max_block_cost, _id, "Bits Moved: ");
    }

    // Prints a cost button/indicator
    void print_cost_btn(int line_cost, int block_cost, int max_line_cost, int max_block_cost, std::string id, std::string prefix) {
        const int num_cost_buckets = 20;
        const auto compand = [](int v) -> int { return (int)std::sqrt(v * 10); };

        int max_cost = std::max(max_line_cost, max_block_cost);  // This should always be the block cost.
        int line_cost_bin_size = (compand(max_cost) / num_cost_buckets) + 1;
        int block_cost_bin_size = (compand(max_cost) / num_cost_buckets) + 1;

        int line_costc = compand(line_cost) / line_cost_bin_size;
        int block_costc = compand(block_cost) / block_cost_bin_size;

        if (line_costc >= num_cost_buckets) {
            line_costc = num_cost_buckets - 1;
        }
        if (block_costc >= num_cost_buckets) {
            block_costc = num_cost_buckets - 1;
        }

        std::string line_cost_class;
        std::string block_cost_class;
        if (line_cost == 0) {
            line_cost_class = "CostColorNone";
        } else {
            line_cost_class = "CostColor" + std::to_string(line_costc);
        }
        if (block_cost == 0) {
            block_cost_class = "CostColorNone";
        } else {
            block_cost_class = "CostColor" + std::to_string(block_costc);
        }
        if (block_cost == line_cost) {
            block_cost_class += " NoChildCost";
        }

        stream << "<div id='" << id << "' "
               << "class='cost-btn tooltip-parent line-" << line_cost_class << " block-" << block_cost_class << "' "
               << ">";

        stream << "<span class='tooltip' role='tooltip'>"
               << prefix << line_cost;
        if (line_cost != block_cost) {
            stream << "<br/>Total " << prefix << block_cost;
        }
        stream << "</span>";

        stream << "</div>";
    }

    /* Misc utility methods */
    int gen_unique_id() {
        return id++;
    }

    int gen_node_id(const IRNode *node) {
        if (node_ids.count(node) == 0) {
            node_ids[node] = gen_unique_id();
        }
        return node_ids[node];
    }

    /* All visitor functions inherited from IRVisitor */

    void visit(const IntImm *op) override {
        print_constant("IntImm Imm", Expr(op));
    }

    void visit(const UIntImm *op) override {
        print_constant("UIntImm Imm", Expr(op));
    }

    void visit(const FloatImm *op) override {
        print_constant("FloatImm Imm", Expr(op));
    }

    void visit(const StringImm *op) override {
        print_constant("StringImm Imm", Expr(op));
    }

    void visit(const Variable *op) override {
        print_variable(op->name);
    }

    void visit(const Cast *op) override {
        print_opening_tag("span", "Cast");
        print_opening_tag("span", "matched");
        print_type(op->type);
        print_text("(");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void visit(const Reinterpret *op) override {
        print_opening_tag("span", "Reinterpret");
        print_opening_tag("span", "matched");
        print_type(op->type);
        print_text("(");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void visit(const Add *op) override {
        print_binary_op(op->a, op->b, "+");
    }

    void visit(const Sub *op) override {
        print_binary_op(op->a, op->b, "-");
    }

    void visit(const Mul *op) override {
        print_binary_op(op->a, op->b, "*");
    }

    void visit(const Div *op) override {
        print_binary_op(op->a, op->b, "/");
    }

    void visit(const Mod *op) override {
        print_binary_op(op->a, op->b, "%");
    }

    void visit(const Min *op) override {
        print_opening_tag("span", "Min");
        print_function_call("min", {op->a, op->b});
        print_closing_tag("span");
    }

    void visit(const Max *op) override {
        print_opening_tag("span", "Max");
        print_function_call("max", {op->a, op->b});
        print_closing_tag("span");
    }

    void visit(const EQ *op) override {
        print_binary_op(op->a, op->b, "==");
    }

    void visit(const NE *op) override {
        print_binary_op(op->a, op->b, "!=");
    }

    void visit(const LT *op) override {
        print_binary_op(op->a, op->b, "&lt;");
    }

    void visit(const LE *op) override {
        print_binary_op(op->a, op->b, "&lt;=");
    }

    void visit(const GT *op) override {
        print_binary_op(op->a, op->b, "&gt;");
    }

    void visit(const GE *op) override {
        print_binary_op(op->a, op->b, "&gt;=");
    }

    void visit(const And *op) override {
        print_binary_op(op->a, op->b, "&amp;&amp;");
    }

    void visit(const Or *op) override {
        print_binary_op(op->a, op->b, "||");
    }

    void visit(const Not *op) override {
        print_opening_tag("span", "Not");
        print_text("!");
        print(op->a);
        print_closing_tag("span");
    }

    void visit(const Select *op) override {
        print_opening_tag("span", "Select");
        print_function_call("select", {op->condition, op->true_value, op->false_value});
        print_closing_tag("span");
    }

    void visit(const Load *op) override {
        int id = gen_node_id(op);
        print_opening_tag("span", "Load", "load-" + std::to_string(id));
        print_opening_tag("span", "matched");
        print_variable(op->name);
        print_text("[");
        print_closing_tag("span");
        print(op->index);
        print_html_element("span", "matched", "]");
        if (!is_const_one(op->predicate)) {
            print_html_element("span", "keyword", " if ");
            print(op->predicate);
        }
        print_closing_tag("span");
    }

    void visit(const Ramp *op) override {
        print_opening_tag("span", "Ramp");
        print_function_call("ramp", {op->base, op->stride, Expr(op->lanes)});
        print_closing_tag("span");
    }

    void visit(const Broadcast *op) override {
        print_opening_tag("span", "Broadcast");
        print_opening_tag("span", "matched");
        print_text("x" + std::to_string(op->lanes) + "(");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void visit(const Call *op) override {
        int id = gen_node_id(op);
        print_opening_tag("span", "Call");
        print_function_call(op->name, op->args, id);
        print_closing_tag("span");
    }

    void visit(const Let *op) override {
        scope.push(op->name, gen_unique_id());
        print_opening_tag("span", "Let");
        print_opening_tag("span", "matched");
        print_text("(");
        print_html_element("span", "keyword", "let ");
        print_variable(op->name);
        print_html_element("span", "Operator Assign", " = ");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched keyword", " in ");
        print(op->body);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
        scope.pop(op->name);
    }

    void visit(const LetStmt *op) override {
        int id = gen_node_id(op);
        scope.push(op->name, gen_unique_id());
        print_opening_tag("div", "LetStmt");
        print_cost_buttons(op);
        print_opening_tag("div", "WrapLine");
        print_opening_tag("span", "cost-highlight", "cost-bg-" + std::to_string(id));
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "let ");
        print_variable(op->name);
        print_html_element("span", "Operator Assign", " = ");
        print_closing_tag("span");  // matched
        print(op->value);
        print_closing_tag("span");  // Cost-Highlight
        print_closing_tag("div");   // WrapLine
        print_closing_tag("div");
        print_ln();
        // Technically, the body of the LetStmt is a child node in the IR
        // tree, but moving it out of the <div> doesn't make any difference to
        // the rendering, and significantly reduces DOM depth.
        print(op->body);
        scope.pop(op->name);
    }

    void visit(const AssertStmt *op) override {
        print_opening_tag("div", "AssertStmt WrapLine");
        print_cost_buttons(op);
        print_function_call("assert", {op->condition, op->message});
        print_closing_tag("div");
        print_ln();
    }

    void visit(const ProducerConsumer *op) override {
        // Give this Producer/Consumer a unique id
        int id = gen_node_id(op);

        // Push a new scope
        scope.push(op->name, id);

        // Start a dive to hold code for this Producer/Consumer
        print_opening_tag("div", op->is_producer ? "Produce" : "Consumer");

        // Print cost buttons
        print_cost_buttons(op, id);

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", op->is_producer ? "produce " : "consume ",
                           "prodcons-" + std::to_string(id));
        print_variable(op->name);
        print_closing_tag("span");

        // Open code block to hold function body
        print_opening_brace();
        print_show_hide_btn_end(op);

        // Open indented div to hold body code
        print_opening_tag("div", "indent ProducerConsumerBody", id);

        // Print producer/consumer body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding producer/consumer body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this producer/consumer
        print_closing_tag("div");

        print_ln();

        // Pop out of loop scope
        scope.pop(op->name);
    }

    std::string ForType_to_string(ForType type) {
        std::ostringstream ss;
        ss << type;
        return ss.str();
    }

    void visit(const For *op) override {
        // Give this loop a unique id
        int id = gen_node_id(op);

        // Push scope
        scope.push(op->name, id);

        // Start a dive to hold code for this allocate
        print_opening_tag("div", "For for-type-" + ForType_to_string(op->for_type));

        // Print cost buttons
        print_cost_buttons(op, id);

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_opening_tag("span", "keyword", "loop-" + std::to_string(id));
        stream << op->for_type << op->device_api;
        print_closing_tag("span");
        print_text(" (");
        print_closing_tag("span");
        print_variable(op->name);
        print_html_element("span", "matched", ", ");
        print(op->min);
        print_html_element("span", "matched", ", ");
        print(op->extent);
        print_html_element("span", "matched", ")");

        // Open code block to hold function body
        print_opening_brace();
        print_show_hide_btn_end(op);

        // Open indented div to hold body code
        print_opening_tag("div", "indent ForBody", id);

        // Print loop body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding loop body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this for loop
        print_closing_tag("div");

        print_ln();

        // Pop out of loop scope
        scope.pop(op->name);
    }

    void visit(const Acquire *op) override {
        // Give this acquire a unique id
        int id = gen_node_id(op);

        // Start a dive to hold code for this acquire
        print_opening_tag("div", "Acquire");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "acquire", "acquire-" + std::to_string(id));
        print_text(" (");
        print_closing_tag("span");
        print(op->semaphore);
        print_html_element("span", "matched", ", ");
        print(op->count);
        print_html_element("span", "matched", ")");

        // Open code block to hold function body
        print_opening_brace();
        print_show_hide_btn_end(op);

        // Open indented div to hold body code
        print_opening_tag("div", "indent AcquireBody", id);

        // Print aquire body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding acquire body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this acquire
        print_closing_tag("div");

        print_ln();
    }

    void visit(const Store *op) override {
        int id = gen_node_id(op);

        // Start a dive to hold code for this acquire
        print_opening_tag("div", "Store WrapLine");

        // Print cost buttons
        print_cost_buttons(op);

        // Print store target
        print_opening_tag("span", "matched");
        print_opening_tag("span", "", "store-" + std::to_string(id));
        print_variable(op->name);
        print_text("[");
        print_closing_tag("span");
        print_closing_tag("span");
        print(op->index);
        print_html_element("span", "matched", "]");
        print_html_element("span", "Operator Assign Matched", " = ");

        // Print store value
        print_opening_tag("span", "StoreValue");
        print(op->value);
        if (!is_const_one(op->predicate)) {
            print_html_element("span", "keyword", " if ");
            print(op->predicate);
        }
        print_closing_tag("span");

        // Close div holding this store
        print_closing_tag("div");

        print_ln();
    }

    void visit(const Provide *op) override {
        print_opening_tag("div", "Provide WrapLine");
        print_function_call(op->name, op->args);
        if (op->values.size() > 1) {
            print_html_element("span", "matched", " = {");
            for (size_t i = 0; i < op->args.size(); i++) {
                if (i > 0) {
                    print_html_element("span", "matched", ", ");
                }
                print(op->args[i]);
            }
            print_html_element("span", "matched", "}");
        } else {
            print(op->values[0]);
        }
        print_closing_tag("div");

        print_ln();
    }

    void visit(const Allocate *op) override {
        int id = gen_node_id(op);

        // Push scope
        scope.push(op->name, gen_unique_id());

        // Start a dive to hold code for this allocate
        print_opening_tag("div", "Allocate");

        // Print cost buttons
        print_cost_buttons(op);

        //  Print allocation name, type and extents
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "allocate ", "allocate-" + std::to_string(id));
        print_variable(op->name);
        print_text("[");
        print_closing_tag("span");
        print_type(op->type);
        for (const auto &extent : op->extents) {
            print_text(" * ");
            print(extent);
        }
        print_html_element("span", "matched", "]");

        // Print memory type
        if (op->memory_type != MemoryType::Auto) {
            print_html_element("span", "keyword", " in ");
            stream << op->memory_type;
        }

        // Print allocation condition
        if (!is_const_one(op->condition)) {
            print_html_element("span", "keyword", " if ");
            print(op->condition);
        }

        // Print custom new and free expressions
        if (op->new_expr.defined()) {
            print_opening_tag("span", "matched");
            print_html_element("span", "keyword", "custom_new");
            print_text(" {");
            print_closing_tag("span");
            print(op->new_expr);
            print_html_element("span", "matched", "}");
        }

        if (!op->free_function.empty()) {
            print_opening_tag("span", "matched");
            print_html_element("span", "keyword", "custom_free");
            print_text(" {");
            print_closing_tag("span");
            print_text(" " + op->free_function + "(); ");
            print_html_element("span", "matched", "}");
        }

        // Print allocation body
        print_ln();
        print_opening_tag("div", "AllocateBody");
        print(op->body);
        print_closing_tag("div");

        // Close dive holding the allocate
        print_closing_tag("div");

        print_ln();

        // Pop out of allocate scope
        scope.pop(op->name);
    }

    void visit(const Free *op) override {
        print_opening_tag("div", "Free WrapLine");
        print_cost_buttons(op);
        print_html_element("span", "keyword", "free ");
        print_variable(op->name);
        print_closing_tag("div");
        print_ln();
    }

    void visit(const Realize *op) override {
        // Give this acquire a unique id
        int id = gen_node_id(op);

        // Push scope
        scope.push(op->name, id);

        // Start a dive to hold code for this realize
        print_opening_tag("div", "Realize");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "realize", "realize-" + std::to_string(id));
        print_variable(op->name);
        print_text(" (");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_html_element("span", "matched", "[");
            print(op->bounds[i].min);
            print_html_element("span", "matched", ", ");
            print(op->bounds[i].extent);
            print_html_element("span", "matched", "]");
            if (i < op->bounds.size() - 1) {
                print_html_element("span", "matched", ", ");
            }
        }
        print_html_element("span", "matched", ")");

        // Print predicate
        if (!is_const_one(op->condition)) {
            print_html_element("span", "keyword", " if ");
            print(op->condition);
        }

        // Open code block to hold function body
        print_opening_brace();
        print_show_hide_btn_end(op);

        // Open indented div to hold body code
        print_opening_tag("div", "indent RealizeBody", id);

        // Print realization body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding realize body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this realize
        print_closing_tag("div");

        // Pop out of this scope
        scope.pop(op->name);
    }

    void visit(const Block *op) override {
        print_opening_tag("div", "Block");
        print_block_stmt(op->first);
        print_block_stmt(op->rest);
        print_closing_tag("div");
        print_ln();
    }

    void visit(const Fork *op) override {
        // Give this acquire a unique id
        int id = gen_node_id(op);

        // Start a dive to hold code for this realize
        print_opening_tag("div", "Fork");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_html_element("span", "keyword matched", "fork");

        // Open code block to hold fork body
        print_opening_brace();
        print_show_hide_btn_end(op);

        // Open indented div to hold body code
        print_opening_tag("div", "indent ForkBody", id);

        // Print fork body
        visit_fork_stmt(op->first);
        visit_fork_stmt(op->rest);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding fork body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this fork
        print_closing_tag("div");
        print_ln();
    }

    void visit(const IfThenElse *op) override {
        // Give this acquire a unique id
        int then_block_id = gen_unique_id();
        int then_node_id = gen_node_id(op->then_case.get());
        int last_then_block_id = -1;

        // Start a div to hold code for this conditional
        print_opening_tag("div", "IfThenElse");

        // Print cost buttons
        print_cost_buttons(op, then_block_id);

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(then_block_id);

        // Print the actual "if (...) {"
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword IfSpan", "if", "cond-" + std::to_string(then_node_id));
        print_text(" (");
        print_closing_tag("span");
        print(op->condition);
        print_html_element("span", "matched", ")");

        // Flatten nested if's in the else case as an
        // `if-then-else_if-else` sequence
        while (true) {
            /* Handle the `then` case */

            // Open code block to hold `then` case
            print_opening_brace();
            print_show_hide_btn_end(op);

            // Open indented div to hold code for the `then` case
            print_opening_tag("div", "indent ThenBody", then_block_id);
            print(op->then_case);
            print_closing_tag("div");
            print_ln();
            last_then_block_id = then_block_id;

            // If there is no `else` case, we are done!
            if (!op->else_case.defined()) {

                // Close code block holding `then` case
                print_html_element("span", "matched ClosingBrace cb-" + std::to_string(then_block_id), "}");

                break;
            }

            /* Handle the `else` case */

            // If the else-case is another if-then-else, flatten it
            if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
                // Generate a new id for the `else-if` case
                then_block_id = gen_unique_id();
                then_node_id = gen_node_id(nested_if->then_case.get());

                // Print cost buttons
                print_cost_buttons(op, then_block_id);

                // Generate the show hide icon/text buttons
                print_show_hide_btn_begin(then_block_id);

                // Close code block with a "}" from the previous block, *after* we have printed the new collapser button.
                internal_assert(last_then_block_id != -1);
                print_html_element("span", "matched ClosingBrace cb-" + std::to_string(last_then_block_id), "}");

                // Print the actual "} else if (...) {" condition statement
                print_opening_tag("span", "matched");
                print_html_element("span", "keyword IfSpan", " else if", "cond-" + std::to_string(then_node_id));
                print_text(" (");
                print_closing_tag("span");
                print(nested_if->condition);
                print_html_element("span", "matched", ")");

                // Update op to the nested if for next loop iteration
                op = nested_if;
                last_then_block_id = then_block_id;
            } else {  // Otherwise, print it and we are done!

                int else_block_id = gen_unique_id();
                int else_node_id = gen_node_id(op->else_case.get());

                // Print cost buttons
                print_cost_buttons(op, else_block_id);

                // Generate the show hide icon/text buttons
                print_show_hide_btn_begin(else_block_id);

                // Close code block with a "}" from the previous block, *after* we have printed the new collapser button.
                internal_assert(last_then_block_id != -1);
                print_html_element("span", "matched ClosingBrace cb-" + std::to_string(last_then_block_id), "}");

                // -- print text
                print_opening_tag("span", "matched");
                print_html_element("span", "keyword IfSpan", " else", "cond-" + std::to_string(else_node_id));
                print_closing_tag("span");

                // Open code block to hold `else` case
                print_opening_brace();
                print_show_hide_btn_end(op);

                // Open indented div to hold code for the `then` case
                print_opening_tag("div", "indent ElseBody", else_block_id);

                // Print `else` case body
                print(op->else_case);

                // Close indented div holding `else` case
                print_closing_tag("div");
                print_ln();

                // Close code block holding `else` case
                print_html_element("span", "matched ClosingBrace cb-" + std::to_string(else_block_id), "}");

                break;
            }
        }

        // Close div holding the conditional
        print_closing_tag("div");
        print_ln();
    }

    void visit(const Evaluate *op) override {
        print_opening_tag("div", "Block Evaluate");
        // Print cost buttons
        print_cost_buttons(op);
        print(op->value);
        print_closing_tag("div");
        print_ln();
    }

    void visit(const Shuffle *op) override {
        if (op->is_concat()) {
            print_function_call("concat_vectors", op->vectors);
        } else if (op->is_interleave()) {
            print_function_call("interleave_vectors", op->vectors);
        } else if (op->is_extract_element()) {
            std::vector<Expr> args = op->vectors;
            args.emplace_back(op->slice_begin());
            print_function_call("extract_element", args);
        } else if (op->is_slice()) {
            std::vector<Expr> args = op->vectors;
            args.emplace_back(op->slice_begin());
            args.emplace_back(op->slice_stride());
            args.emplace_back(static_cast<int>(op->indices.size()));
            print_function_call("slice_vectors", args);
        } else {
            std::vector<Expr> args = op->vectors;
            for (int i : op->indices) {
                args.emplace_back(i);
            }
            print_function_call("shuffle", args);
        }
    }

    void visit(const VectorReduce *op) override {
        print_opening_tag("span", "VectorReduce");
        print_text("(");
        print_type(op->type);
        print_text(")");
        std::ostringstream op_ss;
        op_ss << op->op;
        print_function_call("vector_reduce_" + op_ss.str(), {op->value});
        print_closing_tag("span");
        print_ln();
    }

    void visit(const Prefetch *op) override {
        print_opening_tag("div", "Prefetch");

        // Print cost buttons
        print_cost_buttons(op);

        // Print prefetch
        print_html_element("span", "matched keyword", "prefetch ");
        print_variable(op->name);
        print_html_element("span", "matched", "(");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_html_element("span", "matched", "[");
            print(op->bounds[i].min);
            print_html_element("span", "matched", ",");
            print(op->bounds[i].extent);
            print_html_element("span", "matched", "]");
            if (i < op->bounds.size() - 1) {
                print_html_element("span", "matched", ", ");
            }
        }
        print_html_element("span", "matched", ")");

        // Print condition
        if (!is_const_one(op->condition)) {
            print_html_element("span", "keyword", " if ");
            print(op->condition);
        }

        // Print prefetch body
        print_opening_tag("div", "indent PrefetchBody");
        print(op->body);
        print_closing_tag("div");

        print_closing_tag("div");
        print_ln();
    }

    void visit(const Atomic *op) override {
        // Give this node a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this atomic
        print_opening_tag("div", "Atomic");

        // Generate the show hide icon/text buttons
        print_show_hide_btn_begin(id);

        // -- print text
        print_html_element("span", "matched keyword", "atomic");
        if (!op->mutex_name.empty()) {
            print_html_element("span", "matched", "(");
            print_html_element("span", "Symbol", op->mutex_name);
            print_html_element("span", "matched", ")");
        }

        // Open code block to hold atomic body
        print_opening_brace();
        print_show_hide_btn_end(op);

        // Open indented div to hold atomic code
        print_opening_tag("div", "indent AtomicBody", id);

        // Print atomic body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding fork body
        print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

        // Close div holding this atomic
        print_closing_tag("div");
        print_ln();
    }
};

/** PipelineHTMLInspector Class
 * Generates the output html page. Currently the html page has
 * three key tabs: IR code, Visualized pipeline and the generated
 * assembly.
 */
class PipelineHTMLInspector {
public:
    // Construct the visualizer and point it to the output file
    explicit PipelineHTMLInspector(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename,
                                   bool use_conceptual_stmt_ir)
        : use_conceptual_stmt_ir(use_conceptual_stmt_ir),
          html_code_printer(stream, node_ids, true) {
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
        // Before we generate any html, we annotate IR nodes with
        // line numbers containing corresponding assembly code. This
        // code is based on darya-ver's original implementation. We
        // use comments in the generated assembly to infer association
        // between Halide IR and assembly -- unclear how reliable this is.
        host_asm_info.gather_nodes_from_functions(m);
        host_asm_info.generate(asm_stream.str());

        Buffer<> device_code_buf = m.get_device_code_buffer();
        if (device_code_buf.defined()) {
            std::string device_assembly((char *)device_code_buf.data(),
                                        ((char *)device_code_buf.data() + device_code_buf.size_in_bytes()));
            debug(1) << "Generating device AssemblyInfo\n";
            // TODO(mcourteaux): This doesn't generate anything useful, as the
            // LLVM comments are only added later in the LLVM CodeGen IRVisitor.
            // This conceptual Stmt hasn't seen this seen this
            device_asm_info.gather_nodes_from_conceptual_stmt(m);
            device_asm_info.generate(device_assembly);
        } else {
            debug(1) << "No device code buffer found.\n";
        }

        // Run the cost model over this module to pre-compute all
        // node costs
        if (use_conceptual_stmt_ir) {
            cost_model.compute_conceptual_costs(m);
        } else {
            cost_model.compute_all_costs(m);
        }
        cost_model.finalize_cost_computation();
        html_code_printer.init_cost_info(cost_model);

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

    // Annotate AST nodes with unique IDs
    std::map<const IRNode *, int> node_ids;

    // Used to translate IR to code in HTML
    bool use_conceptual_stmt_ir;
    HTMLCodePrinter<std::ofstream> html_code_printer;

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
        generate_visualization_panes(m);
        stream << "  </div>\n";
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

    // Generate the three visualization panes
    void generate_visualization_panes(const Module &m) {
        int pane_count = 0;
        stream << "<div id='visualization-panes'>\n";
        stream << "<div id='resizer-preview' style='display:none;'></div>\n";
        generate_ir_pane(m);
        generate_resize_bar(pane_count++);
        generate_host_assembly_pane(m);
        Buffer<> device_code_buf = m.get_device_code_buffer();
        if (device_code_buf.defined()) {
            generate_resize_bar(pane_count++);
            generate_device_code_pane(device_code_buf);
        }

        stream << "</div>\n";
    }

    // Generate pane: Lowered IR code with syntax highlighting in HTML
    void generate_ir_pane(const Module &m) {
        if (use_conceptual_stmt_ir) {
            stream << "<div id='ir-code-pane' class='pane conceptual'>\n";
            html_code_printer.print_conceptual_stmt(m, host_asm_info, device_asm_info);
            stream << "</div>\n";
        } else {
            stream << "<div id='ir-code-pane' class='pane'>\n";
            html_code_printer.print(m, host_asm_info, device_asm_info);
            stream << "</div>\n";
        }
    }

    // Generate pane: Generated host assembly code
    void generate_host_assembly_pane(const Module &m) {
        stream << "<div id='host-assembly-pane' class='pane'>\n";
        stream << "<div id='assemblyContent' class='shj-lang-asm'>\n";
        stream << "<pre>\n";
        std::istringstream ss{asm_stream.str()};
        for (std::string line; std::getline(ss, line);) {
            if (line.length() > 500) {
                // Very long lines in the assembly are typically the _gpu_kernel_sources
                // as a raw ASCII block in the assembly. Let's chop that off to make
                // browsers faster when dealing with this.
                line = line.substr(0, 100) + "\" # omitted the remainder of the ASCII buffer";
            }
            stream << html_code_printer.escape_html(line) << "\n";
        }
        stream << "\n";
        stream << "</pre>\n";
        stream << "</div>\n";
        stream << "</div>\n";
    }

    // Generate pane: Generated device code
    void generate_device_code_pane(const Buffer<> &buf) {
        stream << "<div id='device-code-pane' class='pane'>\n";
        int length = buf.size_in_bytes();
        while (length > 0 && ((const char *)buf.data())[length - 1] == '\0') {
            length--;
        }
        std::string str((const char *)buf.data(), length);
        if (starts_with(buf.name(), "cuda_")) {
            html_code_printer.print_cuda_gpu_source_kernels(str);
        } else {
            std::istringstream ss{str};
            stream << "<div class='code'>\n";
            for (std::string line; std::getline(ss, line);) {
                stream << "<span class='line'>" << html_code_printer.escape_html(line) << "</span>\n";
                // stream << html_code_printer.escape_html(line) << "\n";
            }
            stream << "\n</div>\n";
        }
        stream << "</div>\n";
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
    std::ostringstream asm_stream;
    AssemblyInfo host_asm_info;
    AssemblyInfo device_asm_info;

    void load_asm_code(const std::string &asm_file) {
        user_assert(file_exists(asm_file)) << "Unable to open assembly file: " << asm_file << "\n";

        // Open assembly file
        std::ifstream assembly;
        assembly.open(asm_file.c_str());

        // Slurp the code into asm_stream
        std::string line;
        while (getline(assembly, line)) {
            asm_stream << line << "\n";
        }
    }
};

// The external interface to this module
void print_to_stmt_html(const std::string &html_output_filename,
                        const Module &m,
                        const std::string &assembly_input_filename) {
    PipelineHTMLInspector inspector(html_output_filename, m, assembly_input_filename, false);
    inspector.generate_html(m);
    debug(1) << "Done generating HTML IR Inspector - printed to: " << html_output_filename << "\n";
}

void print_to_conceptual_stmt_html(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename) {
    PipelineHTMLInspector inspector(html_output_filename, m, assembly_input_filename, true);
    inspector.generate_html(m);
    debug(1) << "Done generating HTML Conceptual IR Inspector - printed to: " << html_output_filename << "\n";
}

}  // namespace Internal
}  // namespace Halide
