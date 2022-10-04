#ifndef HALIDE_IR_VISUALIZATION_H
#define HALIDE_IR_VISUALIZATION_H

#include "FindStmtCost.h"
#include "IRVisitor.h"

#include <unordered_map>
#include <utility>

namespace Halide {
namespace Internal {

struct StmtSize {
    std::map<std::string, std::string> writes;
    std::map<std::string, std::string> reads;

    bool empty() const {
        return writes.empty() && reads.empty();
    }
};

/*
 * GetReadWrite class
 */
class GetReadWrite : public IRVisitor {
public:
    std::vector<std::string> function_names;  // used for figuring out whether variable is a function call

    // generates the reads/writes for the module
    void generate_sizes(const Module &m);

    // returns the reads/writes for the given node
    StmtSize get_size(const IRNode *node) const;

    // for coloring
    std::string string_span(const std::string &var_name) const;
    std::string int_span(int64_t int_val) const;

    // prints nodes in error messages
    std::string print_node(const IRNode *node) const;

private:
    using IRVisitor::visit;

    std::unordered_map<const IRNode *, StmtSize> stmt_sizes;  // stores the sizes
    std::map<std::string, int> curr_load_values;              // used when calculating store reads

    // starts traversal of the module
    void traverse(const Module &m);

    // used to simplify expressions with + and *, to not have too many parentheses
    std::string get_simplified_string(const std::string &a, const std::string &b, const std::string &op);

    // sets reads/writes for the given node
    void set_write_size(const IRNode *node, const std::string &write_var, std::string write_size);
    void set_read_size(const IRNode *node, const std::string &read_var, std::string read_size);

    void visit(const Store *op) override;
    void add_load_value(const std::string &name, int lanes);
    void visit(const Load *op) override;
};

/*
 * IRVisualization class
 */
class IRVisualization : public IRVisitor {

public:
    static const std::string ir_viz_CSS, scroll_to_function_JS_viz_to_code;

    IRVisualization(FindStmtCost find_stmt_cost_populated)
        : find_stmt_cost(std::move(find_stmt_cost_populated)), ir_viz_tooltip_count(0), if_count(0),
          producer_consumer_count(0), for_count(0), store_count(0), allocate_count(0),
          function_count(0) {
    }

    // generates the html for the IR Visualization
    std::string generate_ir_visualization_html(const Module &m);

    // returns the JS for the IR Visualization
    std::string generate_ir_visualization_js();

    // generates tooltip tables based on given node
    std::string generate_computation_cost_tooltip(const IRNode *op, const std::string &extraNote);
    std::string generate_data_movement_cost_tooltip(const IRNode *op, const std::string &extraNote);

    // returns the range of the node's cost based on the other nodes' costs
    int get_color_range(const IRNode *op, bool inclusive, bool is_computation) const;

    // returns color range when blocks are collapsed in code viz
    int get_combined_color_range(const IRNode *op, bool is_computation) const;

private:
    using IRVisitor::visit;

    std::ostringstream html;      // main html string
    GetReadWrite get_read_write;  // generates the read/write sizes
    FindStmtCost find_stmt_cost;  // used to determine the color of each statement
    int num_of_nodes;             // keeps track of the number of nodes in the visualization
    int ir_viz_tooltip_count;     // tooltip count

    // used for getting anchor names
    int if_count;
    int producer_consumer_count;
    int for_count;
    int store_count;
    int allocate_count;
    int function_count;

    // for traversal of a Module object
    void start_module_traversal(const Module &m);

    // opens and closes divs
    std::string open_box_div(const std::string &class_name, const IRNode *op);
    std::string close_box_div() const;
    std::string open_function_box_div() const;
    std::string close_function_box_div() const;
    std::string open_header_div() const;
    std::string open_box_header_title_div() const;
    std::string open_box_header_table_div() const;
    std::string open_store_div() const;
    std::string open_body_div() const;
    std::string close_div() const;

    // header functions
    std::string open_header(const std::string &header, const std::string &anchor_name,
                            std::vector<std::pair<std::string, std::string>> info_tooltip_table);
    std::string close_header() const;
    std::string div_header(const std::string &header, StmtSize *size, const std::string &anchor_name,
                           std::vector<std::pair<std::string, std::string>> info_tooltip_table);
    std::string function_div_header(const std::string &function_name, const std::string &anchor_name) const;
    std::vector<std::string> get_allocation_sizes(const Allocate *op) const;
    std::string allocate_div_header(const Allocate *op, const std::string &header, const std::string &anchor_name,
                                    std::vector<std::pair<std::string, std::string>> &info_tooltip_table);
    std::string for_loop_div_header(const For *op, const std::string &header, const std::string &anchor_name);

    // opens and closes an if-tree
    std::string if_tree(const IRNode *op, const std::string &header, const std::string &anchor_name);
    std::string close_if_tree() const;

    // different cost tables
    std::string read_write_table(StmtSize &size) const;
    std::string allocate_table(std::vector<std::string> &allocation_sizes) const;
    std::string for_loop_table(const std::string &loop_size) const;

    // generates code for button that will scroll to associated IR code line
    std::string see_code_button_div(const std::string &anchor_name, bool put_div = true) const;

    // info button with tooltip
    std::string info_button_with_tooltip(const std::string &tooltip_text, const std::string &button_class_name,
                                         const std::string &tooltip_class_name = "");

    // for cost colors - side bars of boxes
    std::string generate_computation_cost_div(const IRNode *op);
    std::string generate_memory_cost_div(const IRNode *op);
    std::string open_content_div() const;

    // gets cost percentages of a given node
    int get_cost_percentage(const IRNode *node, bool inclusive, bool is_computation) const;

    // builds the tooltip cost table based on given input table
    std::string tooltip_table(std::vector<std::pair<std::string, std::string>> &table, const std::string &extra_note = "");

    // for cost colors - side boxes of Load nodes
    std::string color_button(int color_range);
    std::string computation_div(const IRNode *op);
    std::string data_movement_div(const IRNode *op);
    std::string cost_colors(const IRNode *op);

    void visit_function(const LoweredFunc &func);
    void visit(const Variable *op) override;
    void visit(const ProducerConsumer *op) override;
    std::string get_loop_iterator_binary(const IRNodeType &type, const Expr &a, const Expr &b) const;
    std::string get_loop_iterator(const For *op) const;
    void visit(const For *op) override;
    void visit(const IfThenElse *op) override;
    void visit(const Store *op) override;
    void visit(const Load *op) override;
    std::string get_memory_type(MemoryType mem_type) const;
    void visit(const Allocate *op) override;
};

}  // namespace Internal
}  // namespace Halide

#endif
