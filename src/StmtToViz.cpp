#include "StmtToViz.h"
#include "Debug.h"
#include "Error.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "IRPrinter.h"
#include "Module.h"
#include "Scope.h"
#include "Substitute.h"
#include "Util.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <utility>
#include <regex>

namespace Halide {
namespace Internal {

using std::istringstream;
using std::ostringstream;
using std::string;

// Classes define within this file
class CostModel;
class AssemblyInfo;
template<typename T> class HTMLCodePrinter;
class HTMLVisualizationPrinter;
class IRVisualizer;

/********************** IRCostModel **********************/
// A basic cost model for Halide IR. Estimates computation
// cost through simple op-counting and data-movement cost 
// by counting the number of bits being moved.
class IRCostModel : public IRVisitor {
public:
    IRCostModel()
        : _max_compute_cost(-1), _max_data_cost(-1), _max_compute_cost_inclusive(-1), 
        _max_data_cost_inclusive(-1)
    {
    }

    // Pre-compute all costs to avoid repeated work
    void comput_all_costs(const Module &m) {
        // Compute all node costs
        for (const auto &fn : m.functions()) {
            fn.body.accept(this);
        }

        // Compute the max cost for each category
        _max_compute_cost = -1;
        for (auto const &[node, cost] : _compute_cost) {
            _max_compute_cost = std::max(cost, _max_compute_cost);
        }

        _max_data_cost = -1;
        for (auto const &[node, cost] : _data_cost) {
            _max_data_cost = std::max(cost, _max_data_cost);
        }

        _max_compute_cost_inclusive = -1;
        for (auto const &[node, cost] : _compute_cost_inclusive) {
            _max_compute_cost_inclusive = std::max(cost, _max_compute_cost_inclusive);
        }

        _max_data_cost_inclusive = -1;
        for (auto const &[node, cost] : _data_cost_inclusive) {
            _max_data_cost_inclusive = std::max(cost, _max_data_cost_inclusive);
        }
    }

    // Returns the compute cost of a node (estimated using simple op-counting)
    int get_compute_cost(const IRNode *node, bool _include_subtree_cost) {
        internal_assert(node != nullptr) << "IRCostModel::get_compute_cost(): node is nullptr\n";

        int cost = -1;
        if (_compute_cost.count(node)) {
            cost = _include_subtree_cost ? _compute_cost_inclusive[node] : _compute_cost[node];
        }
        else 
            internal_assert(false) << "IRCostModel::get_compute_cost(): cost lookup failed\n";

        internal_assert(cost >= 0) << "Cost must not be negative.\n";
        return cost;
    }

    // Returns the data movement cost of a node (the number of bits moved in load/store/shuffle ops)
    int get_datamovement_cost(const IRNode *node, bool _include_subtree_cost) {
        internal_assert(node != nullptr) << "IRCostModel::get_datamovement_cost(): node is nullptr\n";

        int cost = -1;
        if (_compute_cost.count(node)) {
            cost = _include_subtree_cost ? _data_cost_inclusive[node] : _data_cost[node];
        } else
            internal_assert(false) << "IRCostModel::get_datamovement_cost(): cost lookup failed\n";

        internal_assert(cost >= 0) << "Cost cost must not be negative.\n";
        return cost;
    }
    
    // Returns the max compute cost of any node in the program
    int get_max_compute_cost(bool _include_subtree_cost) {
        return _include_subtree_cost ? _max_compute_cost_inclusive : _max_compute_cost;
    }

    // Returns the max data movement cost of any node in the program
    int get_max_datamovement_cost(bool _include_subtree_cost) {
        return _include_subtree_cost ? _max_data_cost_inclusive : _max_data_cost;
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
    std::unordered_map<const IRNode *, int> _compute_cost;
    std::unordered_map<const IRNode *, int> _data_cost;

    std::unordered_map<const IRNode *, int> _compute_cost_inclusive;
    std::unordered_map<const IRNode *, int> _data_cost_inclusive;

    // We also track the max costs to determine the cost color
    // intensity for a given line of code
    int _max_compute_cost;
    int _max_data_cost;

    int _max_compute_cost_inclusive;
    int _max_data_cost_inclusive;

    /* Utility functions to store node costs in the cost database */
    void set_compute_costs(const IRNode *node, int node_cost, std::vector<const IRNode *> child_nodes) {
        set_compute_costs(node, node_cost, child_nodes, child_nodes);
    }

    void set_compute_costs(const IRNode *node, int node_cost, std::vector<const IRNode *> child_nodes, std::vector<const IRNode *> inline_child_nodes) {
        int subtree_cost = 0;
        for (const IRNode *child_node : child_nodes) {
            // Certain child nodes can be null. Ex: else-case
            // in an if statement
            if (child_node)
                subtree_cost += get_compute_cost(child_node, true);
        }

        int line_cost = node_cost;
        for (const IRNode *child_node : inline_child_nodes) {
            if (child_node)
                line_cost += get_compute_cost(child_node, true);
        }
        
        _compute_cost[node] = line_cost;
        _compute_cost_inclusive[node] = node_cost + subtree_cost;
    }

    void set_data_costs(const IRNode *node, int node_cost, std::vector<const IRNode *> child_nodes) {
        set_data_costs(node, node_cost, child_nodes, child_nodes);
    }

    void set_data_costs(const IRNode *node, int node_cost, std::vector<const IRNode *> child_nodes, std::vector<const IRNode *> inline_child_nodes) {
        int subtree_cost = 0;
        for (const IRNode *child_node : child_nodes) {
            // Certain child nodes can be null. Ex: else-case
            // in an if statement
            if (child_node)
                subtree_cost += get_datamovement_cost(child_node, true);
        }
        
        int line_cost = node_cost;
        for (const IRNode *child_node : inline_child_nodes) {
            if (child_node)
                line_cost += get_datamovement_cost(child_node, true);
        }
        
        _data_cost[node] = line_cost;
        _data_cost_inclusive[node] = node_cost + subtree_cost;
    }

private:
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
        for (auto arg : op->args)
            args.push_back(arg.get());
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
        for (auto arg : op->vectors)
            args.push_back(arg.get());
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
        for (auto arg : op->values)
            args.push_back(arg.get());
        for (auto arg : op->args)
            args.push_back(arg.get());
        args.push_back(op->predicate.get());
        set_compute_costs(op, 0, args, {});
        set_data_costs(op, 0, args, {});
    }

    void visit(const Allocate *op) override {
        // We do not model allocation/de-allocation costs
        IRVisitor::visit(op);
        std::vector<const IRNode *> args_inline;
        for (auto arg : op->extents)
            args_inline.push_back(arg.get());  
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
        for (auto arg : op->bounds)
            args_inline.push_back(arg.min.get());
        args_inline.push_back(op->condition.get());
        std::vector<const IRNode *> args = args_inline;
        args.push_back(op->body.get());
        set_compute_costs(op, 0, args);
        int elem_size = 0;
        for (auto etype : op->types)
            elem_size += etype.bits() * etype.lanes();
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

/******************* GetAssemblyInfo *******************/
// Used to map some Halide IR nodes to line-numbers in the
// assembly file marking the corresponding generated code.
class AssemblyInfo : public IRVisitor {
public:
    AssemblyInfo()
        : _loop_id(0), _prodcons_id(0) {
    }

    void generate(string code, const Module &m) {
        // Traverse the module to populate the list of
        // nodes we need to map and generate their assembly
        // markers (comments that appear in the assembly code
        // associating the code with this node)
        for (const auto &fn : m.functions()) {
            fn.body.accept(this);
        }
        
        // Find markers in asm code
        istringstream _asm(code);
        string line;
        int lno = 1;
        while (getline(_asm, line)) {
            // Try all markers
            std::vector<uint64_t> matched_nodes;
            for (auto const &[node, regex] : _markers) {
                if (std::regex_search(line, regex)) {
                    // Save line number
                    _lnos[node] = lno;
                    // Save this node's id
                    matched_nodes.push_back(node);
                }
            }
            // We map to the first match, stop
            // checking matched nodes
            for (auto const node : matched_nodes)
                _markers.erase(node);

            lno++;
        }
        
    }

    int get_asm_lno(uint64_t node_id) {
        if (_lnos.count(node_id))
            return _lnos[node_id];
        return -1;
    }

private:
    // Generate asm markers for Halide loops
    int _loop_id;
    int gen_loop_id() {
        return ++_loop_id;
    }

    std::regex gen_loop_asm_marker(int id, string loop_var) {
        std::regex dollar("\\$");
        string marker = "%\"" + std::to_string(id) + "_for " + loop_var;
        marker = std::regex_replace(marker, dollar, "\\$");
        return std::regex(marker);
    }

    // Generate asm markers for Halide producer/consumer ndoes
    int _prodcons_id;
    int gen_prodcons_id() {
        return ++_prodcons_id;
    }

    std::regex gen_prodcons_asm_marker(int id, string var, bool is_producer) {
        std::regex dollar("\\$");
        string marker = "%\"" + std::to_string(id) + (is_producer ? "_produce " : "_consume ") + var;
        marker = std::regex_replace(marker, dollar, "\\$");
        return std::regex(marker);
    }

    // Mapping of IR nodes to their asm markers
    std::map<uint64_t, std::regex> _markers;

    // Mapping of IR nodes to their asm line numbers
    std::map<uint64_t, int> _lnos;

private:
    using IRVisitor::visit;

    void visit(const ProducerConsumer *op) override {
        // Generate asm marker
        _markers[(uint64_t)op] = gen_prodcons_asm_marker(gen_prodcons_id(), op->name, op->is_producer);
        // Continue traversal
        IRVisitor::visit(op);
    }

    
    void visit(const For *op) override {
        // Generate asm marker
        _markers[(uint64_t)op] = gen_loop_asm_marker(gen_loop_id(), op->name);
        // Continue traversal
        IRVisitor::visit(op);
    }
};

/******************* HTMLCodePrinter Class *******************/
// Prints IR code in HTML. Very similar to generating a stmt
// file, except that the generated html is more interactive.
template<typename T>
class HTMLCodePrinter : public IRVisitor {
public:
    HTMLCodePrinter(T &os)
        : stream(os), _id(0), context_stack(1, 0) {
    }

    void init_cost_info(IRCostModel cost_model) {
        _cost_model = cost_model;
    }

    void print(const Module &m) {
        // Generate a unique ID for this module
        int id = gen_unique_id();

        // Enter new scope for this module
        scope.push(m.name(), id);

        // The implementation currently does not support submodules.
        // TODO: test it out on a benchmark with submodules
        internal_assert(m.submodules().empty()) << "StmtToViz does not yet support submodules.";
        
        // Open div to hold this module
        print_opening_tag("div", "Module");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "module");
        print_text(" name=" + m.name() + ", target=" + m.target().to_string());
        print_closing_tag("span");

        print_toggle_anchor_closing_tag();
        
        // Open code block to hold module body
        print_html_element("span", "matched", " {");

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

private:
    // Handle to output file stream
    T &stream;

    // Used to generate unique ids
    int _id;

    // Used to track scope during IR traversal
    Scope<int> scope;

    /* All spans and divs will have an id of the form "x-y", where x
     * is shared among all spans/divs in the same context, and y is unique.
     * These variables are used to track the context within generated HTML */
    std::vector<int> context_stack;
    std::vector<string> context_stack_tags;

    // Holds cost information for visualized program
    IRCostModel _cost_model;

    /* Private print functions to handle various IR types */
    void print(const Buffer<> &buf) {
        // Generate a unique ID for this module
        int id = gen_unique_id();

        // Determine whether to print buffer data
        bool print_data = ends_with(buf.name(), "_gpu_source_kernels");

        // Open div to hold this buffer
        print_opening_tag("div", "Buffer");

        if (print_data) {
            // Generate the show hide icon/text buttons
            print_toggle_anchor_opening_tag(id);

            // -- print icon
            print_show_hide_icon(id);

            // -- print text
            print_html_element("span", "keyword", "buffer ");
            print_variable(buf.name());

            print_toggle_anchor_closing_tag();

            // Print data
            print_text(" = ");

            // Open code block to hold module body
            print_html_element("span", "matched", " {");

            // Open indented div to hold buffer data
            print_opening_tag("div", "indent BufferData", id);

            string str((const char *)buf.data(), buf.size_in_bytes());
            if (starts_with(buf.name(), "cuda_"))
                print_cuda_gpu_source_kernels(str);
            else
                stream << "<pre>\n"
                       << str << "</pre>\n";

            print_closing_tag("div");

            // Close code block holding buffer body
            print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), " }");
        } else {
            // Print buffer name and move on
            print_html_element("span", "keyword", "buffer ");
            print_variable(buf.name());
        }

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
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text (fn name and args)
        //    Note: We wrap the show/hide buttons in a navigation anchor
        //    that lets us sync text and visualization tabs.
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor", "func ", "lowered-func-" + std::to_string((uint64_t) &fn));
        print_text(fn.name + "(");
        print_closing_tag("span");
        print_fndecl_args(fn.args);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this function in the viz
        print_visualization_button("lowered-func-viz-" + std::to_string((uint64_t)&fn));

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

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
    void print_opening_tag(const string &tag, const string &cls, int id = -1) {
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

    void print_opening_tag(const string &tag, const string &cls, string id) {
        stream << "<" << tag << " class='" << cls << "' id='" << id << "'>";
        context_stack.push_back(gen_unique_id());
        context_stack_tags.push_back(tag);
    }

    // Prints the closing tag for the specified html element.
    void print_closing_tag(const string &tag) {
        internal_assert(!context_stack.empty() && tag == context_stack_tags.back())
            << tag << " " << context_stack.empty() << " " << context_stack_tags.back();
        context_stack.pop_back();
        context_stack_tags.pop_back();
        stream << "</" + tag + ">";
    }

    // Prints an html element: opening tag, body and closing tag
    void print_html_element(const string &tag, const string &cls, const string &body, int id = -1) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    void print_html_element(const string &tag, const string &cls, const string &body, string id) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    // Prints the opening/closing tags for an anchor that toggles code block view
    void print_toggle_anchor_opening_tag(int id) {
        stream << "<a onclick='return toggle(" << id << ");' href=_blank>";
    }

    void print_toggle_anchor_closing_tag() {
        stream << "</a>";
    }

    // Prints a variable to stream
    void print_variable(const string &x) {
        stream << variable(x);
    }

    string variable(const string &x) {
        int id;
        if (scope.contains(x)) {
            id = scope.get(x);
        } else {
            id = gen_unique_id();
            scope.push(x, id);
        }
        ostringstream s;
        s << "<b class='variable matched' id='" << id << "-" << gen_unique_id() << "'>";
        s << x;
        s << "</b>";
        return s.str();
    }

    // Prints text to stream
    void print_text(const string &x) {
        stream << x;
    }

    // Prints the button to show or hide a code scope
    void print_show_hide_icon(int id) {
        stream << "<div class='show-hide-btn-wrapper'>"
               << "  <div class='show-hide-btn' style='display:none;' id=" << id << "-show>"
               << "    <i class='bi bi-plus-square' title='Expand code block'></i>"
               << "  </div>"
               << "  <div class='show-hide-btn' id=" << id << "-hide>"
               << "    <i class='bi bi-dash-square' title='Collapse code block'></i>"
               << "  </div>"
               << "</div>";
    }

    // Prints a button to sync text with visualization
    void print_visualization_button(string id) {
        stream << "<button class='icon-btn sync-btn' onclick='scrollToViz(\"" << id << "\")'>"
               << "  <i class='bi bi-arrow-right-square' title='Jump to visualization'></i>"
               << "</button>";
    }

    // Maaz: This is a legacy function from the old html generator. I made sure it works
    // with the refactored code but I did not dig into why this function does what it does.
    void print_cuda_gpu_source_kernels(const string &str) {
        print_opening_tag("code", "ptx");

        int current_id = -1;
        bool in_braces = false;
        bool in_func_signature = false;
        
        string current_kernel;
        std::istringstream ss(str);

        for (string line; std::getline(ss, line);) {
            if (line.empty()) {
                stream << "\n";
                continue;
            }
            line = replace_all(line, "&", "&amp;");
            line = replace_all(line, "<", "&lt;");
            line = replace_all(line, ">", "&gt;");
            line = replace_all(line, "\"", "&quot;");
            line = replace_all(line, "/", "&#x2F;");
            line = replace_all(line, "'", "&#39;");

            if (starts_with(line, ".visible .entry")) {
                std::vector<string> parts = split_string(line, " ");
                if (parts.size() == 3) {
                    in_func_signature = true;
                    current_id = gen_unique_id();
                    print_toggle_anchor_opening_tag(current_id);
                    print_show_hide_icon(current_id);
                    string kernel_name = parts[2].substr(0, parts[2].length() - 1);
                    line = "<span class='keyword'>.visible</span> <span class='keyword'>.entry</span> ";
                    line += variable(kernel_name) + " <span class='matched'>(</span>";
                    current_kernel = kernel_name;
                }
            } else if (starts_with(line, ")") && in_func_signature) {
                print_toggle_anchor_closing_tag();
                in_func_signature = false;
                line = "<span class='matched'>)</span>" + line.substr(1);
            } else if (starts_with(line, "{") && !in_braces) {
                in_braces = true;
                print_toggle_anchor_closing_tag();
                print_html_element("span", "matched", "{");
                internal_assert(current_id != -1);
                print_opening_tag("div", "indent", current_id);
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
            if ((idx = line.find("&#x2F;&#x2F")) != string::npos) {
                line.insert(idx, "<span class='Comment'>");
                line += "</span>";
            }

            // Predicated instructions
            if (line.front() == '@' && indent) {
                idx = line.find(' ');
                string pred = line.substr(1, idx - 1);
                line = "<span class='Pred'>@" + variable(pred) + "</span>" + line.substr(idx);
            }

            // Labels
            if (line.front() == 'L' && !indent && (idx = line.find(':')) != string::npos) {
                string label = line.substr(0, idx);
                line = "<span class='Label'>" + variable(label) + "</span>:" + line.substr(idx + 1);
            }

            // Highlight operands
            if ((idx = line.find(" \t")) != string::npos && line.back() == ';') {
                string operands_str = line.substr(idx + 2);
                operands_str = operands_str.substr(0, operands_str.length() - 1);
                std::vector<string> operands = split_string(operands_str, ", ");
                operands_str = "";
                for (size_t opidx = 0; opidx < operands.size(); ++opidx) {
                    string op = operands[opidx];
                    internal_assert(!op.empty());
                    if (opidx != 0) {
                        operands_str += ", ";
                    }
                    if (op.back() == '}') {
                        string reg = op.substr(0, op.size() - 1);
                        operands_str += variable(reg) + '}';
                    } else if (op.front() == '%') {
                        operands_str += variable(op);
                    } else if (op.find_first_not_of("-0123456789") == string::npos) {
                        operands_str += "<span class='IntImm Imm'>";
                        operands_str += op;
                        operands_str += "</span>";
                    } else if (starts_with(op, "0f") &&
                               op.find_first_not_of("0123456789ABCDEF", 2) == string::npos) {
                        operands_str += "<span class='FloatImm Imm'>";
                        operands_str += op;
                        operands_str += "</span>";
                    } else if (op.front() == '[' && op.back() == ']') {
                        size_t idx = op.find('+');
                        if (idx == string::npos) {
                            string reg = op.substr(1, op.size() - 2);
                            operands_str += '[' + variable(reg) + ']';
                        } else {
                            string reg = op.substr(1, idx - 1);
                            string offset = op.substr(idx + 1);
                            offset = offset.substr(0, offset.size() - 1);
                            operands_str += '[' + variable(reg) + "+";
                            operands_str += "<span class='IntImm Imm'>";
                            operands_str += offset;
                            operands_str += "</span>";
                            operands_str += ']';
                        }
                    } else if (op.front() == '{') {
                        string reg = op.substr(1);
                        operands_str += '{' + variable(reg);
                    } else if (op.front() == 'L') {
                        // Labels
                        operands_str += "<span class='Label'>" + variable(op) + "</span>";
                    } else {
                        operands_str += op;
                    }
                }
                operands_str += ";";
                line = line.substr(0, idx + 2) + operands_str;
            }

            if (indent) {
                stream << "    ";
            }
            stream << line << "\n";
        }
        print_closing_tag("code");
    }

    // Prints the args in a function declaration
    void print_fndecl_args(std::vector<LoweredArgument> args) {
        bool print_delim = false;
        for (const auto arg : args) {
            if (print_delim) {
                print_html_element("span", "matched", ",");
                print_text(" ");
            }
            print_variable(arg.name);
            print_delim = true;
        }
    }

    /* Helper functions for printing IR nodes */
    void print_constant(string cls, Expr c) {
        print_opening_tag("span", cls);
        stream << c;
        print_closing_tag("span");
    }

    void print_type(Type t) {
        print_opening_tag("span", "Type");
        stream << t;
        print_closing_tag("span");
    }

    void print_binary_op(const Expr &a, const Expr &b, string op) {
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

    void print_function_call(string fn_name, const std::vector<Expr> &args, uint64_t id) {
        print_opening_tag("span", "nav-anchor", "fn-call-" + std::to_string(id));
        print_function_call(fn_name, args);
        print_closing_tag("span");
    }

    void print_function_call(string fn_name, const std::vector<Expr> &args) {
        print_opening_tag("span", "matched");
        print_html_element("span", "Symbol matched", fn_name);
        print_text("(");
        print_closing_tag("span");
        bool print_delim = false;
        for (auto arg : args) {
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
            print_toggle_anchor_opening_tag(id);

            // -- print icon
            print_show_hide_icon(id);

            // -- print text
            print_html_element("span", "keyword matched", "task");

            print_toggle_anchor_closing_tag();

            // Open code block to hold task body
            print_html_element("span", "matched", " {");

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
        print_cost_buttons(op, (uint64_t)op);
    }

    void print_cost_buttons(const IRNode *op, int id) {
        print_opening_tag("div", "node-cost");
        print_compute_cost(op, id);
        print_datamovement_cost(op, id);
        print_closing_tag("div");
    }

    // Prints the button/indicator for the compute cost of a line in the program
    void print_compute_cost(const IRNode *op, uint64_t id) {
        int max_line_cost = _cost_model.get_max_compute_cost(false);
        int line_cost = _cost_model.get_compute_cost(op, false);
        int block_cost = _cost_model.get_compute_cost(op, true);
        string _id = "cc-" + std::to_string(id);
        print_cost_btn(line_cost, block_cost, max_line_cost, _id, "Op Count: ");
    }

    // Prints the button/indicator for the data movement cost of a line in the program
    void print_datamovement_cost(const IRNode *op, uint64_t id) {
        int max_line_cost = _cost_model.get_max_datamovement_cost(false);
        int line_cost = _cost_model.get_datamovement_cost(op, false);
        int block_cost = _cost_model.get_datamovement_cost(op, true);
        string _id = "dc-" + std::to_string(id);
        print_cost_btn(line_cost, block_cost, max_line_cost, _id, "Bits Moved: ");
    }

    // Prints a cost button/indicator
     void print_cost_btn(int line_cost, int block_cost, int max_line_cost, string id, string prefix) {
        const int num_cost_buckets = 20;

        int line_cost_bin_size = (max_line_cost / num_cost_buckets) + 1;
        int block_cost_bin_size = (max_line_cost / num_cost_buckets) + 1;

        int line_costc = line_cost / line_cost_bin_size;
        int block_costc = block_cost / block_cost_bin_size;

        if (line_costc >= num_cost_buckets)
            line_costc = num_cost_buckets - 1;
        if (block_costc >= num_cost_buckets)
            block_costc = num_cost_buckets - 1;

        stream << "<div id='" << id << "' class='cost-btn CostColor" << line_costc << "'"
               << "   aria-describedby='tooltip-" << id << "'"
               << "   line-cost='" << line_cost << "' block-cost='" << block_cost << "'"
               << "   line-cost-color='" << line_costc << "' block-cost-color='" << block_costc << "'>"
               << "</div>";

        stream << "<span id='tooltip-" << id << "' class='tooltip cond-tooltop' role='tooltip-" << id << "'>"
               << prefix << line_cost
               << "</span>";
    }

    /* Misc utility methods */
    int gen_unique_id() {
        return _id++;
    }

private:
    /* All visitor functions inherited from IRVisitor*/

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
        print_binary_op(op->a, op->b, "&lt=");
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
        print_opening_tag("span", "Load nav-anchor", "load-" + std::to_string((uint64_t)op));
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
        print_opening_tag("span", "Call");
        print_function_call(op->name, op->args, (uint64_t)op);
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
        scope.push(op->name, gen_unique_id());
        print_opening_tag("div", "LetStmt");
        print_cost_buttons(op);
        print_opening_tag("p", "WrapLine");
        print_opening_tag("span", "cost-highlight", "cost-bg-" + std::to_string((uint64_t)op));
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "let ");
        print_variable(op->name);
        print_html_element("span", "Operator Assign", " = ");
        print_closing_tag("span");
        print(op->value);
        print_closing_tag("span");
        print_closing_tag("p");        
        print(op->body); 
        print_closing_tag("div");
        scope.pop(op->name);
    }

    void visit(const AssertStmt *op) override {
        print_opening_tag("div", "AssertStmt WrapLine");
        print_cost_buttons(op);
        print_function_call("assert", {op->condition, op->message});
        print_closing_tag("div");
    }

    void visit(const ProducerConsumer *op) override {
        // Give this Producer/Consumer a unique id
        int id = gen_unique_id();

        // Push a new scope
        scope.push(op->name, id);

        // Start a dive to hold code for this Producer/Consumer
        print_opening_tag("div", op->is_producer ? "Produce" : "Consumer");

        // Print cost buttons
        print_cost_buttons(op, id);

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor", op->is_producer ? "produce " : "consume ", 
            "prodcons-" + std::to_string((uint64_t)op));
        print_variable(op->name);
        print_closing_tag("span");

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this producer/consumer in the viz
        print_visualization_button("prodcons-viz-" + std::to_string((uint64_t)op));

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

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

        // Pop out of loop scope
        scope.pop(op->name);
    }

    void visit(const For *op) override {
        // Give this loop a unique id
        int id = gen_unique_id();

        // Push scope
        scope.push(op->name, id);
        
        // Start a dive to hold code for this allocate
        print_opening_tag("div", "For");

        // Print cost buttons
        print_cost_buttons(op, id);

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_opening_tag("span", "keyword nav-anchor", "loop-" + std::to_string((uint64_t)op));
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

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this loop in the viz
        print_visualization_button("loop-viz-" + std::to_string((uint64_t)op));

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

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

        // Pop out of loop scope
        scope.pop(op->name);
    }

    void visit(const Acquire *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this acquire
        print_opening_tag("div", "Acquire");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "acquire", "acquire-" + std::to_string((uint64_t)op));
        print_text(" (");
        print_closing_tag("span");
        print(op->semaphore);
        print_html_element("span", "matched", ", ");
        print(op->count);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this acquire in the viz
        print_visualization_button("acquire-viz-" + std::to_string((uint64_t)op));

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

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
    }

    void visit(const Store *op) override {
        // Start a dive to hold code for this acquire
        print_opening_tag("div", "Store WrapLine");

        // Print cost buttons
        print_cost_buttons(op);

        // Print store target
        print_opening_tag("span", "matched");
        print_opening_tag("span", "nav-anchor", "store-" + std::to_string((uint64_t)op));
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
    }

    void visit(const Allocate *op) override {
        // Push scope
        scope.push(op->name, gen_unique_id());

        // Start a dive to hold code for this allocate
        print_opening_tag("div", "Allocate");

        // Print cost buttons
        print_cost_buttons(op);

        //  Print allocation name, type and extents
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor", "allocate ", "allocate-" + std::to_string((uint64_t)op));
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

        // Add a button to jump to this allocation in the viz
        print_visualization_button("allocate-viz-" + std::to_string((uint64_t)op));

        // Print allocation body
        print_opening_tag("div", "AllocateBody");
        print(op->body);
        print_closing_tag("div");

        // Close dive holding the allocate
        print_closing_tag("div");

        // Pop out of allocate scope
        scope.pop(op->name);
    }
    
    void visit(const Free *op) override {
        print_opening_tag("div", "Free WrapLine");
        print_cost_buttons(op);
        print_html_element("span", "keyword", "free ");
        print_variable(op->name);
        print_closing_tag("div");
    }

    void visit(const Realize *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Push scope
        scope.push(op->name, id);

        // Start a dive to hold code for this realize
        print_opening_tag("div", "Realize");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "realize", "realize-" + std::to_string((uint64_t)op));
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

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this realize in the viz
        print_visualization_button("realize-viz-" + std::to_string((uint64_t)op));

        // Open code block to hold function body
        print_html_element("span", "matched", " {");

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
    }

    void visit(const Fork *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this realize
        print_opening_tag("div", "Fork");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_html_element("span", "keyword matched", "fork");
        
        print_toggle_anchor_closing_tag();

        // Open code block to hold fork body
        print_html_element("span", "matched", " {");

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
    }

    void visit(const IfThenElse *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this conditional
        print_opening_tag("div", "IfThenElse");

        // Print cost buttons
        print_cost_buttons(op, id);

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor IfSpan", "if", "cond-" + std::to_string((uint64_t)&op->then_case));
        print_text(" (");
        print_closing_tag("span");
        print(op->condition);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this conditional in the viz
        print_visualization_button("cond-viz-" + std::to_string((uint64_t)&op->then_case));

        // Flatten nested if's in the else case as an 
        // `if-then-else_if-else` sequence
        while (true) {
            /* Handle the `then` case */

            // Open code block to hold `then` case
            print_html_element("span", "matched", " {");

            // Open indented div to hold code for the `then` case
            print_opening_tag("div", "indent ThenBody", id);

            // Print then case body
            print(op->then_case);

            // Close indented div holding `then` case
            print_closing_tag("div");

            // Close code block holding `then` case
            print_html_element("span", "matched ClosingBrace cb-" + std::to_string(id), "}");

            // If there is no `else` case, we are done!
            if (!op->else_case.defined())
                break;

            /* Handle the `else` case */

            // If the else-case is another if-then-else, flatten it
            if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
                // Generate a new id for the `else-if` case
                id = gen_unique_id();

                // Print cost buttons
                print_cost_buttons(op, id);

                // Generate the show hide icon/text buttons
                print_toggle_anchor_opening_tag(id);

                // -- print icon
                print_show_hide_icon(id);

                // -- print text
                print_opening_tag("span", "matched");
                print_html_element("span", "keyword nav-anchor IfSpan", "else if", "cond-" + std::to_string((uint64_t)&nested_if->then_case));
                print_text(" (");
                print_closing_tag("span");
                print(nested_if->condition);
                print_html_element("span", "matched", ")");

                print_toggle_anchor_closing_tag();

                // Add a button to jump to this conditional branch in the viz
                print_visualization_button("cond-viz-" + std::to_string((uint64_t)nested_if));

                // Update op to the nested if for next loop iteration
                op = nested_if;
            }
            // Otherwise, print it and we are done!
            else {
                int else_id = gen_unique_id();

                // Print cost buttons
                print_cost_buttons(op, else_id);

                // Generate the show hide icon/text buttons
                print_toggle_anchor_opening_tag(else_id);

                // -- print icon
                print_show_hide_icon(else_id);

                // -- print text
                print_opening_tag("span", "matched");
                print_html_element("span", "keyword nav-anchor IfSpan", "else", "cond-" + std::to_string((uint64_t)&op->else_case));
                print_closing_tag("span");
                
                print_toggle_anchor_closing_tag();

                // Add a button to jump to this conditional branch in the viz
                print_visualization_button("cond-viz-" + std::to_string((uint64_t)&op->else_case));

                // Open code block to hold `else` case
                print_html_element("span", "matched", " {");

                // Open indented div to hold code for the `then` case
                print_opening_tag("div", "indent ElseBody", else_id);

                // Print `else` case body
                print(op->else_case);

                // Close indented div holding `else` case
                print_closing_tag("div");

                // Close code block holding `else` case
                print_html_element("span", "matched ClosingBrace cb-" + std::to_string(else_id), "}");

                break;
            }
        }

        // Close div holding the conditional
        print_closing_tag("div");
    }

    void visit(const Evaluate *op) override {
        print_opening_tag("div", "Block");
        // Print cost buttons
        print_cost_buttons(op);
        print(op->value);
        print_closing_tag("div");
    }

    void visit(const Shuffle *op) override {
        print_opening_tag("div", "Block");
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
        print_closing_tag("div");
    }

    void visit(const VectorReduce *op) override {
        print_opening_tag("div", "VectorReduce");
        print_type(op->type);
        print_function_call("vector_reduce", {op->op, op->value});
        print_closing_tag("div");
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
    }

    void visit(const Atomic *op) override {
        // Give this node a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this atomic
        print_opening_tag("div", "Atomic");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_html_element("span", "matched keyword", "atomic");
        if (!op->mutex_name.empty()) {
            print_html_element("span", "matched", "(");
            print_html_element("span", "Symbol", op->mutex_name);
            print_html_element("span", "matched", ")");
        }
        
        print_toggle_anchor_closing_tag();

         // Open code block to hold atomic body
        print_html_element("span", "matched", " {");

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
    }
};

/************** HTMLVisualizationPrinter Class ***************/
// Visualizes the IR in HTML. The visualization is essentially
// an abstracted version of the code, highlighting the higher
// level execution pipeline along with key properties of the
// execution performed at each stage.
class HTMLVisualizationPrinter : public IRVisitor {
public:
    HTMLVisualizationPrinter(std::ofstream &os)
        : stream(os), printer(ss), _id(0) {
    }

    void init_cost_info(IRCostModel cost_model) {
        _cost_model = cost_model;
    }

    void print(const Module &m, AssemblyInfo asm_info) {
        _assembly_info = asm_info;
        for (const auto &fn : m.functions()) {
            print(fn);
        }
    }

private:
    // Handle to output file stream
    std::ofstream &stream;

    // Used to track the context within generated HTML
    std::vector<string> context_stack_tags;

    // Used to translate IR to code in HTML
    ostringstream ss;
    HTMLCodePrinter<ostringstream> printer;

    // Assembly line number info
    AssemblyInfo _assembly_info;

    // Holds cost information for visualized program
    IRCostModel _cost_model;

    // Generate unique ids
    int _id;
    int gen_unique_id() {
        return _id++;
    }

    /* Private print functions to handle various IR types */
    void print(const LoweredFunc &fn) {
        int id = gen_unique_id();

        // Start a div to hold the function viz
        print_opening_tag("div", "center fn-wrapper");

        // Create the header bar
        print_opening_tag("div", "fn-header");
        print_collapse_expand_btn(id);
        print_code_button("lowered-func-" + std::to_string((uint64_t)&fn));
        print_html_element("span", "fn-title", "Func: " + fn.name, "lowered-func-viz-" + std::to_string((uint64_t)&fn));
        print_closing_tag("div");

        // Print function body
        print_opening_tag("div", "fn-body", "viz-" + std::to_string(id));
        fn.body.accept(this);
        print_closing_tag("div");

        // Close function div
        print_closing_tag("div");
    }

    /* Methods used to emit common HTML patterns */

    // Prints the opening tag for the specified html element.
    void print_opening_tag(const string &tag, const string &cls) {
        stream << "<" << tag << " class='" << cls << "'>";
        context_stack_tags.push_back(tag);
    }

    void print_opening_tag(const string &tag, const string &cls, string id) {
        stream << "<" << tag << " class='" << cls << "' id='" << id << "'>";
        context_stack_tags.push_back(tag);
    }

    // Prints the closing tag for the specified html element.
    void print_closing_tag(const string &tag) {
        internal_assert(tag == context_stack_tags.back());
        context_stack_tags.pop_back();
        stream << "</" + tag + ">";
    }

    // Prints an html element: opening tag, body and closing tag
    void print_html_element(const string &tag, const string &cls, const string &body) {
        print_opening_tag(tag, cls);
        stream << body;
        print_closing_tag(tag);
    }

    void print_html_element(const string &tag, const string &cls, const string &body, string id) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    // Prints text to stream
    void print_text(const string &x) {
        stream << x;
    }

    // Prints a button to sync visualization with code
    void print_code_button(string id) {
        stream << "<button class='icon-btn sync-btn' onclick='scrollToCode(\"" << id << "\")'>"
               << "  <i class='bi bi-arrow-left-square' title='Jump to code'></i>"
               << "</button>";
    }

    // Prints a button to sync visualization with assembly
    void print_asm_button(string id) {
        stream << "<button class='icon-btn sync-btn' onclick='scrollToAsm(\"" << id << "\")'>"
               << "  <i class='bi bi-arrow-right-square' title='Jump to assembly'></i>"
               << "</button>";
    }

    // Prints a function-call box
    void print_fn_button(string name, uint64_t id) {
        print_opening_tag("div", "fn-call");
        print_code_button("fn-call-" + std::to_string(id));
        print_text(get_as_var(name) + "(...)");
        print_closing_tag("div");
    }

    // Prints a button to collapse or expand a visualization box
    void print_collapse_expand_btn(int id) {
        stream << "<button class='icon-btn' id='viz-" << id << "-hide' onclick='return toggleViz(\"viz-" << id << "\");'>"
               << "  <i class='bi bi-dash-square' title='Collapse block'></i>"
               << "</button>"
               << "<button class='icon-btn' id='viz-" << id << "-show' style = 'display:none;' onclick='return toggleViz(\"viz-" << id << "\");'>"
               << "  <i class='bi bi-plus-square' title='Expand block'></i>"
               << "</button>";
    }

    // Prints the box title within the div.box-header
    void print_box_title(string title, string anchor) {
        print_opening_tag("div", "box-title");
        print_html_element("span", "", title, anchor);
        print_closing_tag("div");
    }

    // Prints the cost indicator buttons within div.box-header
    void print_cost_buttons(int id, const IRNode *op) {
        print_opening_tag("div", "viz-cost-btns");
        
        // Print compute cost indicator
        int max_line_ccost = _cost_model.get_max_compute_cost(false);
        int line_ccost = _cost_model.get_compute_cost(op, false);
        int block_ccost = _cost_model.get_compute_cost(op, true);
        print_cost_button(line_ccost, block_ccost, max_line_ccost, "vcc-" + std::to_string(id), "Op Count: ");

        // Print data movement cost indicator
        int max_line_dcost = _cost_model.get_max_datamovement_cost(false);
        int line_dcost = _cost_model.get_datamovement_cost(op, false);
        int block_dcost = _cost_model.get_datamovement_cost(op, true);
        // Special handling for Store nodes; since unlike the code view
        // the viz view prints stores and loads seperately, therefore using 
        // inclusive cost is confusing.
        if (op->node_type == IRNodeType::Store) {
            const Store *st = static_cast<const Store *>(op);
            line_dcost = st->value.type().bits() * st->value.type().lanes();
            block_dcost = line_dcost;
        }
        print_cost_button(line_dcost, block_dcost, max_line_dcost, "vdc-" + std::to_string(id), "Bits Moved: ");
        
        print_closing_tag("div");
    }

    void print_cost_button(int line_cost, int block_cost, int max_line_cost, string id, string prefix) {
        const int num_cost_buckets = 20;

        int line_cost_bin_size = (max_line_cost / num_cost_buckets) + 1;
        int block_cost_bin_size = (max_line_cost / num_cost_buckets) + 1;

        int line_costc = line_cost / line_cost_bin_size;
        int block_costc = block_cost / block_cost_bin_size;

        if (line_costc >= num_cost_buckets)
            line_costc = num_cost_buckets - 1;
        if (block_costc >= num_cost_buckets)
            block_costc = num_cost_buckets - 1;

        stream << "<div id='" << id << "' class='cost-btn CostColor" << line_costc << "'"
               << "   aria-describedby='tooltip-" << id << "'"
               << "   line-cost='" << line_cost << "' block-cost='" << block_cost << "'"
               << "   line-cost-color='" << line_costc << "' block-cost-color='" << block_costc << "'>"
               << "</div>";

        stream << "<span id='tooltip-" << id << "' class='tooltip cond-tooltop' role='tooltip-" << id << "'>"
               << prefix << line_cost
               << "</span>";
    }

    // Prints the box .box-header within div.box
    void print_box_header(int id, const IRNode* op, string anchor, string code_anchor, string title) {
        print_opening_tag("div", "box-header");
        print_collapse_expand_btn(id);
        print_code_button(code_anchor);
        print_box_title(title, anchor);
        print_cost_buttons(id, op);
        print_closing_tag("div");
    }

    // Prints the box .box-header within div.box, contains the asm info button
    void print_box_header_asm(int id, const IRNode *op, string anchor, string code_anchor, string asm_anchor, string title) {
        print_opening_tag("div", "box-header");
        print_collapse_expand_btn(id);
        print_code_button(code_anchor);
        print_asm_button(asm_anchor);
        print_box_title(title, anchor);
        print_cost_buttons(id, op);
        print_closing_tag("div");
    }

    // Converts an expr to a string without printing to stream
    string get_as_str(Expr e) {
        return get_as_str(e, "");
    }

    string get_as_str(Expr e, string prefix) {
        if (prefix == "Else")
            return "Else";

        ss.str("");
        ss.clear();
        e.accept(&printer);
        string html_e = ss.str();

        if (large_expr(e)) {
            return prefix + truncate_html(html_e);
        } else {
            return prefix + html_e;
        }
    }

    // Return variable name wrapped with html that enables matching
    string get_as_var(string name){
        return "<b class='variable matched'>" + name + "</b>";
    }

    // Sometimes the expressions are too large to show within the viz. In
    // such cases we use tooltips.
    bool large_expr(Expr e) {
        ostringstream ss;
        ss << e;
        return ss.str().size() > 50;
    }

    string truncate_html(string cond) {
        int id = gen_unique_id();

        ostringstream ss;

        // Show condition expression button
        ss << "<button title='Click to see path condition' id='cond-" << id << "' aria-describedby='cond-tooltip-" << id << "' class='trunc-cond' role='button'>"
           << "..."
           << "</button>";

        // Tooltip that shows condition expression
        ss << "<span id='cond-tooltip-" << id << "' class='tooltip cond-tooltop' role='cond-tooltip-" << id << "'>"
           << cond
           << "</span>";

        return ss.str();
    }

    // Prints a single node in an `if-elseif-...-else` chain
    void print_if_tree_node(const Stmt &node, Expr cond, string prefix) {
        // Assign unique id to this node
        int id = gen_unique_id();

        // Start tree node
        print_opening_tag("li", "");
        print_opening_tag("span", "tf-nc if-node");

        // Start a box to hold viz
        print_opening_tag("div", "box center IfBox");

        // Create viz content
        string aid = std::to_string((uint64_t)&node);
        print_box_header(id, node.get(), "cond-viz-" + aid, "cond-" + aid, get_as_str(cond, prefix));
        
        // Print contents of node
        print_opening_tag("div", "box-body", "viz-" + std::to_string(id));
        node.accept(this);
        print_closing_tag("div");

        // Close box holding viz
        print_closing_tag("div");

        // Close tree node
        print_closing_tag("span");
        print_closing_tag("li");
    }

private:
    using IRVisitor::visit;

    /* Override key visit functions */

    void visit(const Allocate *op) override {
        // Assign unique id to this node
        int id = gen_unique_id();

        // Start a box to hold viz
        print_opening_tag("div", "box center AllocateBox");

        // Print box header
        string aid = std::to_string((uint64_t)op);
        print_box_header(id, op, "allocate-viz-" + aid, "allocate-" + aid, "Allocate: " + op->name);

        // Start a box to hold viz
        print_opening_tag("div", "box-body", "viz-" + std::to_string(id));

        // Generate a table with allocation details
        print_opening_tag("table", "allocate-table");

        // - Memory type
        stream << "<tr><th scope='col'>Memory Type</th><td>" << op->memory_type << "</td></tr>";

        // - Allocation condition
        if (!is_const_one(op->condition)) {
            stream << "<tr><th scope='col'>Condition</th><td>" << op->condition << "</td></tr>";
        }

        // - Data type
        stream << "<tr><th scope='col'>Data Type</th><td>" << op->type << "</td></tr>";

        // - Dimensions
        for (size_t i = 0; i < op->extents.size(); i++) {
            stream << "<tr><th scope='col'>Dim-" << i << "</th><td>" << get_as_str(op->extents[i]) << "</td></tr>";
        }

        print_closing_tag("table");

        op->body.accept(this);

        print_closing_tag("div");

        print_closing_tag("div");
    }

    void visit(const For *op) override {
        // Assign unique id to this node
        int id = gen_unique_id();

        // Start a box to hold viz
        print_opening_tag("div", "box center ForBox");

        // Print box header
        string aid = std::to_string((uint64_t)op);
        int asm_lno = _assembly_info.get_asm_lno((uint64_t)op);
        if (asm_lno == -1)
            print_box_header(id, op, "loop-viz-" + aid, "loop-" + aid, "For: " + get_as_var(op->name));
        else
            print_box_header_asm(id, op, "loop-viz-" + aid, "loop-" + aid, std::to_string(asm_lno), "For: " + get_as_var(op->name));

        // Start a box to hold viz
        print_opening_tag("div", "box-body", "viz-" + std::to_string(id));

        // Generate a table with loop details
        print_opening_tag("table", "allocate-table");

        // - Loop type
        if (op->for_type != ForType::Serial)
            stream << "<tr><th scope='col'>Loop Type</th><td>" << op->for_type << "</td></tr>";
        // - Device API
        if (op->device_api != DeviceAPI::None)
            stream << "<tr><th scope='col'>Device API</th><td>" << op->device_api << "</td></tr>";
        // - Min
        stream << "<tr><th scope='col'>Min</th><td>" << get_as_str(op->min) << "</td></tr>";
        // - Extent
        stream << "<tr><th scope='col'>Extent</th><td>" << get_as_str(op->extent) << "</td></tr>";

        print_closing_tag("table");

        op->body.accept(this);

        print_closing_tag("div");

        print_closing_tag("div");
    }

    void visit(const IfThenElse *op) override {
        // Open If tree
        print_opening_tag("div", "tf-tree tf-gap-sm tf-custom-ir-viz");

        // Create root 'cond' node
        if (op->else_case.defined()) {
            print_opening_tag("ul", "");
            print_opening_tag("li", "");
            print_html_element("span", "tf-nc if-node if-root-node", "Control Flow Branching");
        }

        // Create children nodes ('then', 'else if' and 'else' cases)
        print_opening_tag("ul", "");

        // `then` case
        print_if_tree_node(op->then_case, op->condition, "If: ");

        // `else if` cases
        Stmt else_case = op->else_case;
        const IfThenElse *nested_if;
        while (else_case.defined() && (nested_if = else_case.as<IfThenElse>())) {
            print_if_tree_node(nested_if->then_case, op->condition, "Else If: ");
            else_case = nested_if->else_case;
        }

        // `else` case
        if (else_case.defined()) {
            print_if_tree_node(else_case, UIntImm::make(UInt(1), 1), "Else");
        }

        print_closing_tag("ul");

        // Close If tree
        if (op->else_case.defined()) {
            print_closing_tag("li");
            print_closing_tag("ul");
        }
        print_closing_tag("div");
    }

    void visit(const ProducerConsumer *op) override {
        // Assign unique id to this node
        int id = gen_unique_id();

        // Start a box to hold viz
        string box_name = op->is_producer ? "ProducerBox" : "ConsumerBox";
        print_opening_tag("div", "box center " + box_name);

        // Print box header
        string aid = std::to_string((uint64_t)op);
        string prefix = op->is_producer ? "Produce: " : "Consume: ";
        int asm_lno = _assembly_info.get_asm_lno((uint64_t)op);
        if (asm_lno == -1)
            print_box_header(id, op, "prodcons-viz-" + aid, "prodcons-" + aid, prefix + get_as_var(op->name));
        else
            print_box_header_asm(id, op, "prodcons-viz-" + aid, "prodcons-" + aid, std::to_string(asm_lno), prefix + get_as_var(op->name));

        // Print the body
        print_opening_tag("div", "box-body", "viz-" + std::to_string(id));
        op->body.accept(this);
        print_closing_tag("div");

        // Close div holding the producer/consumer
        print_closing_tag("div");
    }

    void visit(const Store *op) override {
        // Visit the value first. We want to show any loads 
        // that happen before the store operation
        op->value.accept(this);

        // Assign unique id to this node
        int id = gen_unique_id();

        // Start a box to hold viz
        print_opening_tag("div", "box center StoreBox");

        // Print box header
        string aid = std::to_string((uint64_t)op);
        print_box_header(id, op, "store-viz-" + aid, "store-" + aid, "Store: " + get_as_var(op->name));

        // Start a box to hold viz
        print_opening_tag("div", "box-body", "viz-" + std::to_string(id));

        // Generate a table with store details
        print_opening_tag("table", "allocate-table");

        // - Store predicate
        if (!is_const_one(op->predicate))
            stream << "<tr><th scope='col'>Predicate</th><td>" << get_as_str(op->predicate) << "</td></tr>";

        // - Alignment
        const bool show_alignment = op->value.type().is_vector() && op->alignment.modulus > 1;
        if (show_alignment) 
            stream << "<tr><th scope='col'>Alignment</th><td>"
                   << "aligned(" << op->alignment.modulus << ", " << op->alignment.remainder << ")"
                   << "</td></tr>";

        // - Qualifiers
        if (op->value.type().is_vector()) {
            const Ramp *idx = op->index.as<Ramp>();
            if (idx && is_const_one(idx->stride))
                stream << "<tr><th scope='col'>Type</th><td>Dense Vector</td></tr>";
            else
                stream << "<tr><th scope='col'>Type</th><td>Strided Vector</td></tr>";
            stream << "<tr><th scope='col'>Output Tile</th><td>" << op->value.type() << "</td></tr>";
        } else {
            stream << "<tr><th scope='col'>Type</th><td>Scalar</td></tr>";
            stream << "<tr><th scope='col'>Output</th><td>" << op->value.type() << "</td></tr>";
        }
        
        print_closing_tag("table");

        print_closing_tag("div");

        print_closing_tag("div");
    }
    
    void visit(const Load *op) override {
        // Assign unique id to this node
        int id = gen_unique_id();

        // Start a box to hold viz
        print_opening_tag("div", "box center LoadBox");

        // Print box header
        string aid = std::to_string((uint64_t)op);
        print_box_header(id, op, "load-viz-" + aid, "load-" + aid, "Load: " + get_as_var(op->name));

        // Start a box to hold viz
        print_opening_tag("div", "box-body", "viz-" + std::to_string(id));

        // Generate a table with load details
        print_opening_tag("table", "allocate-table");

        // - Load predicate
        if (!is_const_one(op->predicate))
            stream << "<tr><th scope='col'>Predicate</th><td>" << get_as_str(op->predicate) << "</td></tr>";

        // - Alignment
        const bool show_alignment = op->type.is_vector() && op->alignment.modulus > 1;
        if (show_alignment)
            stream << "<tr><th scope='col'>Alignment</th><td>"
                   << "aligned(" << op->alignment.modulus << ", " << op->alignment.remainder << ")"
                   << "</td></tr>";

        // - Qualifiers
        if (op->type.is_vector()) {
            const Ramp *idx = op->index.as<Ramp>();
            if (idx && is_const_one(idx->stride))
                stream << "<tr><th scope='col'>Type</th><td>Dense Vector</td></tr>";
            else
                stream << "<tr><th scope='col'>Type</th><td>Strided Vector</td></tr>";
            stream << "<tr><th scope='col'>Output Tile</th><td>" << op->type << "</td></tr>";
        } else {
            stream << "<tr><th scope='col'>Type</th><td>Scalar</td></tr>";
            stream << "<tr><th scope='col'>Output</th><td>" << op->type << "</td></tr>";
        }

        print_closing_tag("table");

        print_closing_tag("div");

        print_closing_tag("div");
    }

    void visit(const Call *op) override {
        // Add viz support for key functions/intrinsics
        if (op->name == "halide_do_par_for") {
            print_fn_button(op->name, (uint64_t)op);
        } 
        else if (op->name == "halide_do_par_task") {
            print_fn_button(op->name, (uint64_t)op);
        } 
        else if (op->name == "_halide_buffer_init") {
            print_fn_button(op->name, (uint64_t)op);
        } 
        else if (op->name.rfind("_halide", 0) != 0) {
            // Assumption: We want to ignore intrinsics starting with _halide
            // but for everything else, generate a warning
            debug(2) << "Function call ignored by IRVisualizer: " << op->name << "\n";
        }
    }
    
};

/************** IRVisualizer Class **************/
// Generates the output html page. Currently the html page has
// three key tabs: IR code, Visualized pipeline and the generated
// assembly.
class IRVisualizer {
public:
    // Construct the visualizer and point it to the output file
    IRVisualizer(const string &filename)
        : _popup_id(0), html_code_printer(stream), html_viz_printer(stream) {
        // Open output file
        stream.open(filename.c_str());

        // Load assembly code
        string asm_file = filename;
        asm_file.replace(asm_file.find(".stmt.viz.html"), 15, ".s");
        load_asm_code(asm_file);
    }

    // Generate the html visualization of the input module
    void generate_html(const Module &m) {
        // Before we generate any html, we annotate IR nodes with
        // line numbers containing corresponding assembly code. This
        // code is based on darya-ver's original implementation. We
        // use comments in the generated assembly to infer association 
        // between Halide IR and assembly -- unclear how reliable this is.
        _asm_info.generate(_asm.str(), m);

        // Run the cost model over this module to pre-compute all 
        // node costs
        _cost_model.comput_all_costs(m);
        html_code_printer.init_cost_info(_cost_model);
        html_viz_printer.init_cost_info(_cost_model);

        // Generate html page
        stream << "<html>\n";
        generate_head(m);
        generate_body(m);
        stream << "</html>";
    }

private:
    // Handle to output file stream
    std::ofstream stream;

    // Handle to assembly file stream
    std::ifstream assembly;

    // Holds cost information for visualized program
    IRCostModel _cost_model;

    // Used to generate unique popup ids
    int _popup_id;

    // Used to translate IR to code in HTML
    HTMLCodePrinter<std::ofstream> html_code_printer;

    // Used to translate IR to visualization in HTML
    HTMLVisualizationPrinter html_viz_printer;

    // These variables hold static html/js/css used to
    // generate the html page
    static const std::string dependencies;
    static const std::string css;
    static const std::string javascript;

    /* Methods for generating the <head> section of the html file */
    void generate_head(const Module &m) {
        stream << "<head>\n";
        stream << "<title>Visualizing Module: " << m.name() << "</title>\n";
        generate_dependency_links();
        generate_stylesheet();
        stream << "</head>\n";
    }

    // Loads the html code responsible for linking with various js/css libraries from
    // `ir_visualizer/dependencies.html`
    void generate_dependency_links() {
        stream << dependencies;
    }

    // Loads the stylesheet code from `ir_visualizer/stylesheet.html`
    void generate_stylesheet() {
        stream << css;
    }

    /* Methods for generating the <body> section of the html file */
    void generate_body(const Module &m) {
        stream << "<body>\n";
        stream << "  <div id='page-container'>\n";
        generate_visualization_tabs(m);
        stream << "  </div>\n";
        stream << "</body>";
        generate_javascript();
    }

    // Generate the three visualization tabs
    void generate_visualization_tabs(const Module &m) {
        stream << "<div id='visualization-tabs'>\n";
        generate_ir_tab(m);
        generate_resize_bar_1();
        generate_visualization_tab(m);
        generate_resize_bar_2();
        generate_assembly_tab(m);
        stream << "</div>\n";
    }

    // Generate tab 1/3: Lowered IR code with syntax highlighting in HTML
    void generate_ir_tab(const Module &m) {
        stream << "<div id='ir-code-tab'>\n";
        html_code_printer.print(m);
        stream << "</div>\n";
    }

    // Generate tab 2/3: Lowered IR code with syntax highlighting in HTML
    void generate_visualization_tab(const Module &m) {
        stream << "<div id='ir-visualization-tab'>\n";
        html_viz_printer.print(m, _asm_info);
        stream << "</div>\n";
    }

    // Generate tab 3/3: Generated assembly code
    void generate_assembly_tab(const Module &m) {
        stream << "<div id='assembly-tab'>\n";
        stream << "<div id='assemblyContent' style='display: none;'>\n";
        stream << "<pre>\n";
        stream << _asm.str();
        stream << "</pre>\n";
        stream << "</div>\n";
        stream << "</div>\n";
    }

    // Generate a resizing bar to control the width of code and visualization tabs
    void generate_resize_bar_1() {
        stream << R"(<div class='resize-bar' id='resize-bar-1'>
                       <div class='collapse-btns'>
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapse_code_tab()'>
                             <i class='bi bi-arrow-bar-left' title='Collapse code tab'></i>
                           </button>
                         </div>                         
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapseR_visualization_tab()'>
                             <i class='bi bi-arrow-bar-right' title='Collapse visualization tab'></i>
                           </button>
                         </div>
                       </div>
                     </div>)";
    }

    // Generate a resizing bar to control the width of visualization and assembly tabs
    void generate_resize_bar_2() {
        stream << R"(<div class='resize-bar' id='resize-bar-2'>
                       <div class='collapse-btns'>
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapseL_visualization_tab()'>
                             <i class='bi bi-arrow-bar-left' title='Collapse visualization tab'></i>
                           </button>
                         </div>
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapse_assembly_tab()'>
                             <i class='bi bi-arrow-bar-right' title='Collapse assembly tab'></i>
                           </button>
                         </div>
                       </div>
                     </div>)";
    }

    // Loads and initializes the javascript template from `ir_visualizer / javascript_template.html`
    void generate_javascript() {
        stream << javascript;
    }

    /* Misc helper methods */

    // Returns a new unique popup id
    int gen_popup_id() {
        return _popup_id++;
    }

private:
    ostringstream _asm;
    AssemblyInfo _asm_info;

    // Load assembly code from file
    void load_asm_code(string asm_file) {
        // Open assembly file
        assembly.open(asm_file.c_str());

        // Slurp the code into _asm
        string line;
        while (getline(assembly, line)) {
            _asm << line << "\n";
        }
    }
    
};

/************** The external interface to this module **************/

void print_to_viz(const string &filename, const Stmt &s) {
    internal_assert(false) << "\n\n"
                           << "Exiting early: print_to_viz cannot be called from a Stmt node - it must be "
                              "called from a Module node.\n"
                           << "\n\n\n";
}

void print_to_viz(const string &filename, const Module &m) {

    IRVisualizer visualizer(filename);
    visualizer.generate_html(m);
    debug(1) << "Done generating HTML IR Visualization - printed to: " << filename << "\n";
}

/*********************** Static HTML/JS/CSS ***********************/

const std::string IRVisualizer::dependencies = R"(
<!-- Bootstrap links -->
<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.2.0/dist/css/bootstrap.min.css' rel='stylesheet' integrity='sha384-gH2yIJqKdNHPEq0n4Mqa/HGKIhSkIHeL5AyhkYV8i59U5AR6csBvApHHNl/vI1Bx' crossorigin='anonymous'>

<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.2.0/dist/js/bootstrap.bundle.min.js' integrity='sha384-A3rJD856KowSb7dwlZdYEkO39Gagi7vIsF0jrRAoQmDKKtQBHUuLZ9AsSv4jD4Xa' crossorigin='anonymous'></script>

<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bootstrap@5.0.2/dist/css/bootstrap.min.css'>      
<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bootstrap-icons@1.5.0/font/bootstrap-icons.css'>
        
<!-- Tooltip links -->
<script src='https://cdn.jsdelivr.net/npm/@floating-ui/core@1.0.1'></script>
<script src='https://cdn.jsdelivr.net/npm/@floating-ui/dom@1.0.1'></script>
        
<!-- Hierarchy links -->
<link rel='stylesheet' href='https://unpkg.com/treeflex/dist/css/treeflex.css'>
        
<!-- Expand Button links -->
<link href='http://maxcdn.bootstrapcdn.com/font-awesome/4.1.0/css/font-awesome.min.css' rel='stylesheet'>

<script src='http://code.jquery.com/jquery-1.10.2.js'></script>
        
<!-- Assembly Code links -->
<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/codemirror/5.52.2/codemirror.min.css'></link>
<script src='https://cdnjs.cloudflare.com/ajax/libs/codemirror/5.52.2/codemirror.min.js'></script>
<script src='https://cdnjs.cloudflare.com/ajax/libs/codemirror/6.65.7/mode/gas/gas.min.js'></script>
<script src='https://cdnjs.cloudflare.com/ajax/libs/codemirror/6.65.7/addon/selection/mark-selection.min.js'></script>
<script src='https://cdnjs.cloudflare.com/ajax/libs/codemirror/6.65.7/addon/search/searchcursor.min.js'></script>
<script src='https://cdnjs.cloudflare.com/ajax/libs/codemirror/6.65.7/addon/search/search.min.js'></script>
)";

const std::string IRVisualizer::javascript = R"(
<script>
    /* Highlighting 'matched' elements in IR code */
    $('#ir-code-tab .matched').each(function () {
        this.onmouseover = function () {
            $('#ir-code-tab .matched[id^=' + this.id.split('-')[0] + '-]').addClass('Highlight');
        }
        this.onmouseout = function () {
            $('#ir-code-tab .matched[id^=' + this.id.split('-')[0] + '-]').removeClass('Highlight');
        }
    });

    /* Highlighting 'matched' elements in Viz code */
    $('#ir-visualization-tab .matched').each(function () {
        this.onmouseover = function () {
            $('#ir-visualization-tab .matched[id^=' + this.id.split('-')[0] + '-]').addClass('Highlight');
        }
        this.onmouseout = function () {
            $('#ir-visualization-tab .matched[id^=' + this.id.split('-')[0] + '-]').removeClass('Highlight');
        }
    });

    /* Cross highlighting 'matched' variables (only) */
    $('#ir-visualization-tab .matched.variable').each(function () {
        this.onmouseover = function () {
            var name = this.outerText;
            $('.matched.variable').filter((idx, val) => {
                return val.outerText === name;
            }).addClass('Highlight');
        }
        this.onmouseout = function () {
            var name = this.outerText;
            $('.matched.variable').filter((idx, val) => {
                return val.outerText === name;
            }).removeClass('Highlight');
        }
    });

    /* Expand/Collapse buttons in IR code */
    function toggle(id) {
        e = document.getElementById(id);
        e_cb = document.getElementsByClassName("cb-" + id)[0];
        show = document.getElementById(id + '-show');
        hide = document.getElementById(id + '-hide');
        ccost_btn = document.getElementById("cc-" + id);
        dcost_btn = document.getElementById("dc-" + id);
        ccost_tt = document.getElementById("tooltip-cc-" + id);
        dcost_tt = document.getElementById("tooltip-dc-" + id);
        if (e.classList.contains("collapsed-block")) {
            e.classList.remove("collapsed-block");
            e_cb.classList.add("ClosingBrace");
            show.style.display = 'none';
            hide.style.display = 'block';
            if (ccost_btn && dcost_tt) {
                // Update cost indicators
                ccost_color = ccost_btn.getAttribute('line-cost-color');
                dcost_color = dcost_btn.getAttribute('line-cost-color');
                ccost_btn.className = ccost_btn.className.replace(/CostColor\d+/, 'CostColor' + ccost_color);
                dcost_btn.className = dcost_btn.className.replace(/CostColor\d+/, 'CostColor' + dcost_color);
                // Update cost tooltips
                ccost = ccost_btn.getAttribute('line-cost');
                dcost = dcost_btn.getAttribute('line-cost');
                ccost_tt.innerText = 'Op Count: ' + ccost;
                dcost_tt.innerText = 'Bits Moved: ' + dcost;
            }
        } else {
            e.classList.add("collapsed-block");
            e_cb.classList.remove("ClosingBrace");
            show.style.display = 'block';
            hide.style.display = 'none';
            if (ccost_btn && dcost_tt) {
                // Update cost indicators
                collapsed_ccost_color = ccost_btn.getAttribute('block-cost-color');
                collapsed_dcost_color = dcost_btn.getAttribute('block-cost-color');
                ccost_btn.className = ccost_btn.className.replace(/CostColor\d+/, 'CostColor' + collapsed_ccost_color);
                dcost_btn.className = dcost_btn.className.replace(/CostColor\d+/, 'CostColor' + collapsed_dcost_color);
                // Update cost tooltips
                collapsed_ccost = ccost_btn.getAttribute('block-cost');
                collapsed_dcost = dcost_btn.getAttribute('block-cost');
                ccost_tt.innerText = 'Op Count: ' + collapsed_ccost;
                dcost_tt.innerText = 'Bits Moved: ' + collapsed_dcost;
            }
        }
        return false;
    }

    /* Expand/Collapse buttons in Viz */
    function toggleViz(id) {
        var buttonShow = document.getElementById(id + '-show');
        var buttonHide = document.getElementById(id + '-hide');
        var body = document.getElementById(id);
        var re = /(?:\-([^-]+))?$/;
        var ccost_btn = document.getElementById("vcc-" + re.exec(id)[1]);
        var dcost_btn = document.getElementById("vdc-" + re.exec(id)[1]);
        var ccost_tt = document.getElementById("tooltip-vcc-" + re.exec(id)[1]);
        var dcost_tt = document.getElementById("tooltip-vdc-" + re.exec(id)[1]);
        if (body.classList.contains("collapsed-viz")) {
            body.classList.remove("collapsed-viz");
            buttonShow.style.display = 'none';
            buttonHide.style.display = 'block';
            if (ccost_btn && dcost_tt) {
                // Update cost indicators
                ccost_color = ccost_btn.getAttribute('line-cost-color');
                dcost_color = dcost_btn.getAttribute('line-cost-color');
                ccost_btn.className = ccost_btn.className.replace(/CostColor\d+/, 'CostColor' + ccost_color);
                dcost_btn.className = dcost_btn.className.replace(/CostColor\d+/, 'CostColor' + dcost_color);
                // Update cost tooltips
                ccost = ccost_btn.getAttribute('line-cost');
                dcost = dcost_btn.getAttribute('line-cost');
                ccost_tt.innerText = 'Op Count: ' + ccost;
                dcost_tt.innerText = 'Bits Moved: ' + dcost;
            }
        } else {
            body.classList.add("collapsed-viz");
            buttonShow.style.display = 'block';
            buttonHide.style.display = 'none';
            if (ccost_btn && dcost_tt) {
                // Update cost indicators
                collapsed_ccost_color = ccost_btn.getAttribute('block-cost-color');
                collapsed_dcost_color = dcost_btn.getAttribute('block-cost-color');
                ccost_btn.className = ccost_btn.className.replace(/CostColor\d+/, 'CostColor' + collapsed_ccost_color);
                dcost_btn.className = dcost_btn.className.replace(/CostColor\d+/, 'CostColor' + collapsed_dcost_color);
                // Update cost tooltips
                collapsed_ccost = ccost_btn.getAttribute('block-cost');
                collapsed_dcost = dcost_btn.getAttribute('block-cost');
                ccost_tt.innerText = 'Op Count: ' + collapsed_ccost;
                dcost_tt.innerText = 'Bits Moved: ' + collapsed_dcost;
            }
        }
    };

    /* Scroll to visualization from IR code */
    function scrollToViz(id) {
        var container = document.getElementById('ir-visualization-tab');
        var scrollToObject = document.getElementById(id);
        makeVizVisible(scrollToObject);
        container.scrollTo({
            top: getOffsetTop(scrollToObject) - 8,
            left: getOffsetLeft(scrollToObject),
            behavior: 'smooth'
        });
        scrollToObject.style.backgroundColor = 'white';
        setTimeout(function () {
            scrollToObject.style.backgroundColor = 'transparent';
        }, 1000);
    }

    function getOffsetTop(element) {
        if (!element) return 0;
        if (element.id == 'ir-visualization-tab') return 0;
        return getOffsetTop(element.offsetParent) + element.offsetTop;
    }

    function getOffsetLeft(element) {
        if (!element) return 0;
        if (element.id == 'ir-visualization-tab') return 0;
        return getOffsetLeft(element.offsetParent) + element.offsetLeft;
    }

    // In case the code we are scrolling to viz block that sits within 
    // a collapsed parent block, uncollapse it
    function makeVizVisible(element) {
        if (!element) return;
        if (element == document) return;
        if (element.classList.contains("collapsed-viz")) {
            toggleViz(element.id);
        }
        makeVizVisible(element.parentNode);
    }

    /* Scroll to code from visualization */
    function scrollToCode(id) {
        var container = document.getElementById('ir-code-tab');
        var scrollToObject = document.getElementById(id);
        makeCodeVisible(scrollToObject);
        container.scrollTo({
            top: scrollToObject.offsetTop,
            behavior: 'smooth'
        });
        scrollToObject.style.backgroundColor = 'lightgray';
        setTimeout(function () {
            scrollToObject.style.backgroundColor = 'transparent';
        }, 1000);
    }

    // In case the code we are scrolling to code that sits within 
    // a collapsed code block, uncollapse it
    function makeCodeVisible(element) {
        if (!element) return;
        if (element == document) return;
        if (element.classList.contains("collapsed-block")) {
            toggle(element.id);
        }
        makeCodeVisible(element.parentNode);
    }

    /* Resizing visualization tabs */
    var codeDiv = document.getElementById('ir-code-tab');
    var resizeBar = document.getElementById('resize-bar-1');
    var irVizDiv = document.getElementById('ir-visualization-tab');
    var resizeBarAssembly = document.getElementById('resize-bar-2');
    var assemblyCodeDiv = document.getElementById('assembly-tab');

    codeDiv.style.flexGrow = '0';
    resizeBar.style.flexGrow = '0';
    irVizDiv.style.flexGrow = '0';
    resizeBarAssembly.style.flexGrow = '0';
    assemblyCodeDiv.style.flexGrow = '0';

    codeDiv.style.flexBasis = 'calc(50% - 6px)';
    resizeBar.style.flexBasis = '6px';
    irVizDiv.style.flexBasis = 'calc(50% - 3px)';
    resizeBarAssembly.style.flexBasis = '6px';

    resizeBar.addEventListener('mousedown', (event) => {
        document.addEventListener('mousemove', resize, false);
        document.addEventListener('mouseup', () => {
            document.removeEventListener('mousemove', resize, false);
        }, false);
    });

    resizeBarAssembly.addEventListener('mousedown', (event) => {
        document.addEventListener('mousemove', resizeAssembly, false);
        document.addEventListener('mouseup', () => {
            document.removeEventListener('mousemove', resizeAssembly, false);
        }, false);
    });

    function resize(e) {
        if (e.x < 25) {
            collapse_code_tab();
            return;
        }

        const size = `${e.x}px`;
        var rect = resizeBarAssembly.getBoundingClientRect();

        if (e.x > rect.left) {
            collapseR_visualization_tab();
            return;
        }

        codeDiv.style.display = 'block';
        irVizDiv.style.display = 'block';
        codeDiv.style.flexBasis = size;
        irVizDiv.style.flexBasis = `calc(${rect.left}px - ${size})`;
    }

    function resizeAssembly(e) {
        if (e.x > screen.width - 25) {
            collapse_assembly_tab();
            return;
        }

        var rect = resizeBar.getBoundingClientRect();

        if (e.x < rect.right) {
            collapseL_visualization_tab();
            return;
        }

        const size = `${e.x}px`;
        irVizDiv.style.display = 'block';
        assemblyCodeDiv.style.display = 'block';
        irVizDiv.style.flexBasis = `calc(${size} - ${rect.right}px)`;
        assemblyCodeDiv.style.flexBasis = `calc(100% - ${size})`;

    }

    function collapse_code_tab() {
        irVizDiv.style.display = 'block';
        var rect = resizeBarAssembly.getBoundingClientRect();
        irVizDiv.style.flexBasis = `${rect.left}px`;
        codeDiv.style.display = 'none';
    }

    function collapseR_visualization_tab() {
        codeDiv.style.display = 'block';
        var rect = resizeBarAssembly.getBoundingClientRect();
        codeDiv.style.flexBasis = `${rect.left}px`;
        irVizDiv.style.display = 'none';
    }

    function collapseL_visualization_tab() {
        assemblyCodeDiv.style.display = 'block';
        var rect = resizeBar.getBoundingClientRect();
        assemblyCodeDiv.style.flexBasis = `calc(100% - ${rect.right}px)`;
        irVizDiv.style.display = 'none';
    }

    function collapse_assembly_tab() {
        irVizDiv.style.display = 'block';
        var rect = resizeBar.getBoundingClientRect();
        irVizDiv.style.flexBasis = `calc(100% - ${rect.right}px)`;
        assemblyCodeDiv.style.display = 'none';
    }

    // Tooltips
    function update(buttonElement, tooltipElement) {
        window.FloatingUIDOM.computePosition(buttonElement, tooltipElement, {
            placement: 'top',
            middleware: [
                window.FloatingUIDOM.offset(6),
                window.FloatingUIDOM.flip(),
                window.FloatingUIDOM.shift({
                    padding: 5
                }),
            ],
        }).then(({
            x,
            y,
            placement,
            middlewareData
        }) => {
            Object.assign(tooltipElement.style, {
                left: `${x}px`,
                top: `${y}px`,
            });
            // Accessing the data 
            const staticSide = {
                top: 'bottom',
                right: 'left',
                bottom: 'top',
                left: 'right',
            }[placement.split('-')[0]];
        });
    }

    function showTooltip(buttonElement, tooltipElement) {
        tooltipElement.style.display = 'block';
        tooltipElement.style.opacity = '1';
        update(buttonElement, tooltipElement);
    }

    function hideTooltip(tooltipElement) {
        tooltipElement.style.display = '';
        tooltipElement.style.opacity = '0';
    }

    function init_tooltips(btns, prefix) {
        var re = /(?:\-([^-]+))?$/;
        for (var i = 0; i < btns.size(); i++) {
            const button = btns[i];
            const tooltip = $(prefix + re.exec(button.id)[1])[0];
            button.is_clicked = false;
            button.addEventListener('mouseenter', () => {
                if (!button.is_clicked)
                    showTooltip(button, tooltip);
            });
            button.addEventListener('mouseleave', () => {
                if (!button.is_clicked)
                    hideTooltip(tooltip);
            });
            button.addEventListener('click', () => {
                if (button.is_clicked) {
                    hideTooltip(tooltip);
                    button.is_clicked = false;
                } else {
                    showTooltip(button, tooltip);
                    button.is_clicked = true;
                }
            });
        }
    }

    init_tooltips($('button[id^="cond-"]'), "span#cond-tooltip-");
    init_tooltips($('div[id^="cc-"]'), "span#tooltip-cc-");
    init_tooltips($('div[id^="dc-"]'), "span#tooltip-dc-");
    init_tooltips($('div[id^="vcc-"]'), "span#tooltip-vcc-");
    init_tooltips($('div[id^="vdc-"]'), "span#tooltip-vdc-");
    
    function depth(elem) {
        if ($(elem).is("div#ir-visualization-tab"))
            return 0;
        else if ($(elem).is("div.box"))
            return 1 + depth(elem.parentNode);
        else
            return depth(elem.parentNode);
    }

    // Collapse viz boxes beyond at depth > 1
    $('div[id^="viz-"]').filter((idx, val) => {
        return depth(val) > 1;
    }).each((idx, val) => {
        toggleViz(val.id);
    });

    // CodeMirror Js

    // Populate assembly code
    assemblyCodeDiv.style.display = 'block';
    var codeHTML = document.getElementById('assemblyContent');
    var code = codeHTML.textContent;
    code = code.trimLeft();
    document.getElementById('assembly-tab').innerHTML = '';
    var assemblyCM = CodeMirror(document.getElementById('assembly-tab'), {
        value: code,
        lineNumbers: true,
        lineWrapping: true,
        mode: {
            name: 'gas',
            architecture: 'ARMv6'
        },
        readOnly: true,
    });

    function scrollToAsm(lno) {
        assemblyCM.scrollIntoView({
            line: assemblyCM.lineCount()-1,
            ch: 0
        });
        assemblyCM.scrollIntoView({
            line: lno - 1,
            ch: 0
        });
        assemblyCM.focus();
        assemblyCM.setCursor({
            line: lno,
            ch: 0
        });
    }

    scrollToAsm(1);
    collapse_assembly_tab();

    // Cost model js
    var re = /(?:\-([^-]+))?$/;
    var cost_btns = $('div[id^="cc-"], div[id^="dc-"]');
    for (var i = 0; i < cost_btns.size(); i++) {
        const button = cost_btns[i];
        const highlight_span = $("span#cost-bg-" + re.exec(button.id)[1])[0];
        $(button).mouseover(() => {
            $(highlight_span).css("background", "#e5e3e3");
        });
        $(button).mouseout(() => {
            $(highlight_span).css("background", "none");
        });
    }
</script>
)";

const std::string IRVisualizer::css = R"(
<style type='text/css'>   
	/* General CSS Rules*/
	body {
		font-family: Consolas, 'Liberation Mono', Menlo, Courier, monospace;
		font-size: 12px;
		background: #f8f8f8;
		margin-left: 15px;
	}

    div#page-container {
        height: 100vh;
        display: flex;
        flex-direction: column;
    }

    a,
    a:hover,
    a:visited,
    a:active {
        color: inherit;
        text-decoration: none;
    }

    b {
        font-weight: normal;
    }

    table {
        font-size: 12px;
    }

	/* Visualization tabs */
    div#visualization-tabs {
        display: flex;
        flex-grow: 1;
        width: 100%;
        overflow: hidden;
        border-top: 1px solid rgb(200, 200, 200)
    }

    div#ir-code-tab {
        counter-reset: line;
        padding-left: 50px;
        padding-top: 20px;
        overflow-y: scroll;
        position: relative;
    }

    div#ir-visualization-tab {
        overflow-y: scroll;
        padding-top: 20px;
        padding-left: 20px;
        position: relative;
    }

    /* Resize bars */
    div#visualization-tabs div.resize-bar {
        background: rgb(201, 231, 190);
        cursor: col-resize;
        border-left: 1px solid rgb(0, 0, 0);
        border-right: 1px solid rgb(0, 0, 0);
    }

    div#visualization-tabs div.resize-bar div.collapse-btns {
        position: relative;
        top: 50%;
    }

    div#visualization-tabs div.resize-bar div.collapse-btns {
        margin: 0px;
    }

    button.resize-btn {
        margin: 10px 3px;
        font-size: 18px;
    }

	/* IR Code Section CSS */
    b.Highlight {
        font-weight: bold;
        background-color: #DDD;
    }

    span.Highlight {
        font-weight: bold;
        background-color: #FF0;
    }

    span.OpF32 {
        color: hsl(106deg 100% 40%);
        font-weight: bold;
    }

    span.OpF64 {
        color: hsl(106deg 100% 30%);
        font-weight: bold;
    }

    span.OpB8 {
        color: hsl(208deg 100% 80%);
        font-weight: bold;
    }

    span.OpB16 {
        color: hsl(208deg 100% 70%);
        font-weight: bold;
    }

    span.OpB32 {
        color: hsl(208deg 100% 60%);
        font-weight: bold;
    }

    span.OpB64 {
        color: hsl(208deg 100% 50%);
        font-weight: bold;
    }

    span.OpI8 {
        color: hsl(46deg 100% 45%);
        font-weight: bold;
    }

    span.OpI16 {
        color: hsl(46deg 100% 40%);
        font-weight: bold;
    }

    span.OpI32 {
        color: hsl(46deg 100% 34%);
        font-weight: bold;
    }

    span.OpI64 {
        color: hsl(46deg 100% 27%);
        font-weight: bold;
    }

    span.OpVec2 {
        background-color: hsl(100deg 100% 90%);
        font-weight: bold;
    }

    span.OpVec4 {
        background-color: hsl(100deg 100% 80%);
        font-weight: bold;
    }

    span.Memory {
        color: #d22;
        font-weight: bold;
    }

    span.Pred {
        background-color: #ffe8bd;
        font-weight: bold;
    }

    span.Label {
        background-color: #bde4ff;
        font-weight: bold;
    }

    div.show-hide-btn-wrapper {
        position: relative;
        width: 0;
        height: 0;
    }

    div.show-hide-btn {
        position: absolute;
        left: -14px;
        top: 4px;
        font-size: 8pt;
    }

    div.show-hide-btn:hover {
        color: #c30000;
    }

    button.icon-btn {
        border: 0px;
        background: transparent;
        color: black;
        font-size: 11pt;
        display: inline-block;
        vertical-align: middle;
        margin-right: 5px;
        margin-left: 5px;
        padding: 0px;
    }

    div#ir-visualization-tab button.icon-btn {
        margin-left: 1px;
    }

    button.icon-btn:hover {
        color: #c30000;
    }

    code.ptx {
        tab-size: 26;
        white-space: pre;
    }

    div.indent {
        padding-left: 15px;
    }

    span.comment {
        color: #998;
        font-style: italic;
    }

    span.keyword {
        color: #333;
        font-weight: bold;
    }

	span.IntImm {
        color: #099;
    }

    span.UIntImm {
        color: #099;
    }

    span.FloatImm {
        color: #099;
    }

    span.StringImm {
        color: #d14;
    }

    span.Type {
        color: #445588;
        font-weight: bold;
    }

    span.Symbol {
        color: #990073;
    }

    span.Assign {
        color: #d14;
        font-weight: bold;
    }

    p.WrapLine {
        margin: 0px;
        margin-left: 30px;
        text-indent: -30px;
    }

    div.WrapLine {
        margin-left: 30px;
        text-indent: -30px;
    }

    p.WrapLine,
    span.IfSpan,
    span.ClosingBrace,
    div.Module,
    div.WrapLine,
    div.Consumer,
    div.Produce,
    div.For,
    div.Evaluate,
    div.Allocate,
    div.Function {
        counter-increment: line;
    }

    p.WrapLine:before,
    span.IfSpan:before,
    span.ClosingBrace:before,
    div.WrapLine:before,
    div.Consumer:before,
    div.Produce:before,
    div.For:before,
    div.Evaluate:before,
    div.Allocate:before,
    div.Module:before,
    div.Function:before {
        content: counter(line) '. ';
        display: inline-block;
        position: absolute;
        left: 0px;
        color: rgb(175, 175, 175);
        user-select: none;
        -webkit-user-select: none;
    }

    p.WrapLine:before,
    div.WrapLine:before {
        left: 30px;
    }

    .collapsed-block {
        position: absolute;
        left: -9999px;
        max-height: 0px;
        overflow: hidden;
        opacity: 0;
    }

    /* IR Viz Section CSS */
    .collapsed-viz {
        display: none;
    }

    div.fn-wrapper {
        background-color: #f0f0f0;
        border: 1px dashed grey;
        margin-bottom: 15px;
        width: max-content;
    }

    div.fn-header {
        display: flex;
        font-weight: bold;
        padding: 5px 7px;
        background-color: #dfdede;
    }

    span.fn-title {
        margin-right: 10px;
        margin-left: 3px;
    }

    div.fn-body {
        padding: 7px;
    }

    div.fn-call {
        padding: 3px 5px;
        background: #f1daeb;
        border: 1px dashed gray;
    }

    div.box {
        border: 1px dashed grey;
        width: max-content;
        min-width: -webkit-fill-available;
        display: flex;
        flex-direction: column;
    }

    div.box:not(:last-child) {
        margin-bottom: 15px;
    }

    div.box-header {
        padding: 5px 7px;
        display: flex;
    }

    div.box-title {
        font-weight: bold;
        margin-top: auto;
        margin-bottom: auto;
        margin-left: 3px;
    }

    div.viz-cost-btns {
        margin: auto 0px auto auto;
        display: flex;
        padding-left: 10px;
    }

    div.viz-cost-btns div {
        width: 10px;
        height: 17.02px;
        border: 1px solid gray;
    }

    div.viz-cost-btns div:hover {
        border: 1px solid black;
    }

    div.box-body {
        padding: 7px;
    }

    div.ProducerBox {
        background-color: #e9fbe4;
    }

    div.ProducerBox > div.box-header {
        background-color: #c6edbb;
    }

    div.ConsumerBox {
        background-color: #ffe6e8;
    }

    div.ConsumerBox > div.box-header {
        background-color: #f1c8cc;
    }

    div.ForBox {
        background-color: #f0e9f9;
    }

    div.ForBox > div.box-header {
        background-color: #ddc6fb;
    } 

    div.IfBox {
        background-color: #e6eeff;
    }

    div.IfBox > div.box-header {
        background-color: #c0d4ff;
    }

    div.AllocateBox {
        background-color: #f4f8bf;
    }

    div.AllocateBox > div.box-header{
        background-color: #f5e790;
    }

    div.LoadBox {
        background-color: #fff4e6;
    }

    div.LoadBox > div.box-header {
        background-color: #fbe3c6;
    }

    div.StoreBox {
        background-color: #ddf3e9;
    }

    div.StoreBox > div.box-header {
        background-color: #bcedd6;
    }

    .if-root-node {
        width: 200px !important;
        border: 1px dashed gray !important;
        text-align: center;
        padding: 3px !important;
        background: #c0d4ff;
    }

    .allocate-table {
        background-color: rgba(197, 195, 195, 0.2);
        min-width: -webkit-fill-available;
    }

    .allocate-table:not(:last-child) {
        margin-bottom: 10px;
    }

    .allocate-table td, .allocate-table th {
        padding: 5px;
        border: 1px dashed gray;
    }

    button.trunc-cond {
        font-size: 12px;
        vertical-align: middle;
        padding: 0px;
        line-height: 1.5;
        background: none;
        border: none;
    }

    span.tooltip {
        display: none;
        position: absolute;
        top: 0;
        left: 0;
        width: max-content;
        padding: 8px;
        background: #fff7e0;
        font-size: 12px;
        border-radius: 5px;
        border: 1px solid #aaa;
        z-index: 9999;
        box-shadow: rgba(100, 100, 100, 0.8) 0 2px 5px 0;
        text-indent: 0px;
    }

    .conditionTooltip {
        width: 300px;
        padding: 5px;
        font-family: Consolas, 'Liberation Mono', Menlo, Courier, monospace;
    }

	/* TreeFlex CSS */
    .tf-tree {
        overflow: unset;
    }

    .tf-tree ul {
        width: -webkit-fill-available;
    }

    .tf-custom-ir-viz li {
        padding: 0px !important;
        flex-grow: 1;
    }

    .tf-tree.tf-gap-sm li > .tf-nc:after, .tf-tree.tf-gap-sm li > .tf-node-content:after {
        /*height: 1em;*/
    }

    .tf-custom-ir-viz {
        font-size: 12px;
    }

    .tf-custom-ir-viz:not(:last-child) {
        margin-bottom: 15px;
    }

    .tf-custom-ir-viz .tf-nc {
        border: none;
        margin: 0px;
        padding: 0px;
        width: -webkit-fill-available;
    }

    .tf-custom-ir-viz .tf-nc:before,
    .tf-custom-ir-viz .tf-nc:after {
        border-left-width: 1px;
    }

    .tf-custom-ir-viz li:not(:first-child) > .if-node {
        padding-left: 8px !important;
    }

    .tf-custom-ir-viz li li:before {
        border-top-width: 1px;
    }

    .tf-custom-ir-viz .end-node {
        border-style: dashed;
    }

    /* CodeMirror */
    .CodeMirror {
        height: 100%;
        width: 100%;
    }

    /* Cost model */

    div.node-cost {
        position: absolute;
        left: 35px;
        display: flex;
    }

    div.cost-btn {
        width: 10px;
        height: 14px;
        margin-top: 2px;
    }

    div.cost-btn:not(:last-child) {
        margin-right: 3px;
        margin-right: 3px;
    }

    div.cost-btn:hover {
        cursor: pointer;
        border: 1px solid lightgray;
    }

    .CostColor19 {
        background-color: rgb(130, 31, 27);
    }

    .CostColor18 {
        background-color: rgb(145, 33, 30);
    }

    .CostColor17 {
        background-color: rgb(160, 33, 32);
    }

    .CostColor16 {
        background-color: rgb(176, 34, 34);
    }

    .CostColor15 {
        background-color: rgb(185, 47, 32);
    }

    .CostColor14 {
        background-color: rgb(193, 59, 30);
    }

    .CostColor13 {
        background-color: rgb(202, 71, 27);
    }

    .CostColor12 {
        background-color: rgb(210, 82, 22);
    }

    .CostColor11 {
        background-color: rgb(218, 93, 16);
    }

    .CostColor10 {
        background-color: rgb(226, 104, 6);
    }

    .CostColor9 {
        background-color: rgb(229, 118, 9);
    }

    .CostColor8 {
        background-color: rgb(230, 132, 15);
    }

    .CostColor7 {
        background-color: rgb(231, 146, 20);
    }

    .CostColor6 {
        background-color: rgb(232, 159, 25);
    }

    .CostColor5 {
        background-color: rgb(233, 172, 30);
    }

    .CostColor4 {
        background-color: rgb(233, 185, 35);
    }

    .CostColor3 {
        background-color: rgb(233, 198, 40);
    }

    .CostColor2 {
        background-color: rgb(232, 211, 45);
    }

    .CostColor1 {
        background-color: rgb(231, 223, 50);
    }

    .CostColor0 {
        background-color: rgb(236,233,89);
    }
</style>
)";

}  // namespace Internal
}  // namespace Halide