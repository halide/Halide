#ifndef ProducerConsumerHierarchy_H
#define ProducerConsumerHierarchy_H

#include "FindStmtCost.h"
#include "IRVisitor.h"

#include <set>
#include <unordered_map>

using namespace std;
using namespace Halide;
using namespace Internal;

// background colors for the different types of elements
#define IF_COLOR "#e6eeff"
#define FOR_COLOR "#b3ccff"
#define PRODUCER_COLOR "#99bbff"
#define CONSUMER_COLOR PRODUCER_COLOR
#define STORE_COLOR "#f4f8bf"
#define ALLOCATE_COLOR STORE_COLOR
#define FUNCTION_CALL_COLOR "#fabebe"
#define FUNCTION_BOX_COLOR "#f0f0f0"

#define SHOW_CUMULATIVE_COST false
#define SHOW_UNIQUE_LOADS false

#define MAX_CONDITION_LENGTH 30

struct StmtSize {
    map<string, string> produces;
    map<string, string> consumes;
    map<string, string> allocates;
    string forLoopSize;
    vector<string> mainFunctionCalls;
    vector<string> allocationSizes;

    bool empty() const {
        return produces.size() == 0 && consumes.size() == 0 && allocates.size() == 0 &&
               allocationSizes.size() == 0 && mainFunctionCalls.size() == 0;
    }
    bool emptyAllocated() const {
        return allocates.size() == 0;
    }
    string to_string() {
        string result = "";
        if (!empty()) {
            result += "Produces: ";
            for (auto it = produces.begin(); it != produces.end(); ++it) {
                result += it->first + ": " + it->second + "\n";
            }
            result += "\n";
            result += "Consumes: ";
            for (auto it = consumes.begin(); it != consumes.end(); ++it) {
                result += it->first + ": " + it->second + "\n";
            }
            result += "\n";
            result += "Allocates: ";
            for (auto it = allocates.begin(); it != allocates.end(); ++it) {
                result += it->first + ": " + it->second + "\n";
            }
            result += "\n";
            result += "Allocation sizes: ";
            for (auto it = allocationSizes.begin(); it != allocationSizes.end(); ++it) {
                result += *it + " ";
            }
            result += "\n";
        }
        if (!forLoopSize.empty()) {
            result += "For loop size: " + forLoopSize + "\n";
        }
        if (!mainFunctionCalls.empty()) {
            result += "Main function calls: ";
            for (auto it = mainFunctionCalls.begin(); it != mainFunctionCalls.end(); ++it) {
                result += *it + " ";
            }
            result += "\n";
        }
        return result;
    }
};

/*
 * StmtSizes class
 */
class StmtSizes : public IRVisitor {
public:
    map<string, Stmt> main_function_bodies;  // TODO: maybe move back to private if don't
                                             // use it outside of this class
                                             // TODO: remove this entirely, if possible (don't need
                                             // to store Stmt)
    string module_name;

    void generate_sizes(const Module &m);
    void generate_sizes(const Stmt &stmt);

    StmtSize get_size(const IRNode *node) const;
    string get_allocation_size(const IRNode *node, const string &name) const;

    string print_sizes() const;
    string print_produce_sizes(StmtSize &stmtSize) const;
    string print_consume_sizes(StmtSize &stmtSize) const;

private:
    using IRVisitor::visit;

    unordered_map<const IRNode *, StmtSize> stmt_sizes;
    vector<string> curr_producer_names;
    vector<string> curr_consumer_names;
    map<string, int> curr_load_values;
    vector<string> arguments;  // arguments of the main function in module
    map<string, vector<set<int>>> curr_loads;

    void traverse(const Module &m);

    string get_simplified_string(string a, string b, string op);

    void get_function_arguments(const LoweredFunc &op);

    void set_produce_size(const IRNode *node, string produce_var, string produce_size);
    void set_consume_size(const IRNode *node, string consume_var, string consume_size);
    void set_allocation_size_old(const IRNode *node, string allocate_var, string allocate_size);
    void set_for_loop_size(const IRNode *node, string for_loop_size);
    void set_allocation_size(const IRNode *node, string allocate_size);

    bool in_producer(const string &name) const;
    bool in_consumer(const string &name) const;
    void remove_producer(const string &name);
    void remove_consumer(const string &name);

    string string_span(string varName) const;
    string int_span(int64_t intVal) const;

    void bubble_up(const IRNode *from, const IRNode *to, string loopIterator);

    void visit(const Call *op) override;
    void visit(const Variable *op) override;
    void visit(const LetStmt *op) override;
    void visit(const ProducerConsumer *op) override;
    string get_loop_iterator(const For *op) const;
    void visit(const For *op) override;
    void visit(const Store *op) override;
    void add_load_value(const string &name, const int lanes);
    void add_load_value_unique_loads(const string &name, set<int> &load_values);
    void visit(const Load *op) override;
    void visit(const Allocate *op) override;
    void visit(const Block *op) override;
    void visit(const IfThenElse *op) override;

    string print_node(const IRNode *node) const;
};

/*
 * ProducerConsumerHierarchy class
 */
class ProducerConsumerHierarchy : public IRVisitor {

public:
    static const string prodConsCSS, scrollToFunctionJSVizToCode;

    // TODO: eventually get rid of output_file_name (should be able to open file within the same
    // file) - although, maybe want to print out what file is being generated to the screen
    ProducerConsumerHierarchy(string fileName, FindStmtCost findStmtCostPopulated)
        : output_file_name(fileName), findStmtCost(findStmtCostPopulated) {
    }

    // generates the html for the producer-consumer hierarchy
    string generate_producer_consumer_html(const Module &m);
    string generate_producer_consumer_html(const Stmt &stmt);

    string generate_prodCons_js();

private:
    using IRVisitor::visit;

    string html;                // main html string
    StmtSizes pre_processor;    // generates the sizes of the nodes
    string output_file_name;    // used for anchoring
    FindStmtCost findStmtCost;  // used to determine the color of each statement

    // used for getting anchor names
    int ifCount = 0;
    int producerConsumerCount = 0;
    int forCount = 0;
    int storeCount = 0;
    int allocateCount = 0;
    int functionCount = 0;

    // tooltip count
    int prodConsTooltipCount = 0;

    // for traversal of a Module object
    void startModuleTraversal(const Module &m);

    // opens and closes divs
    void open_box_div(string backgroundColor, string className, const IRNode *op);
    void close_box_div();
    void open_header_div();
    void open_box_header_title_div();
    void open_box_header_table_div();
    void open_store_div();
    void close_div();

    // header functions
    void open_header(const string &header, string anchorName);
    void close_header(string anchorName);
    void div_header(const string &header, StmtSize &size, string anchorName);
    void allocate_div_header(const Allocate *op, const string &header, StmtSize &size,
                             string anchorName);
    void for_loop_div_header(const For *op, const string &header, StmtSize &size,
                             string anchorName);

    // opens and closes an if-tree
    void if_tree(const IRNode *op, const string &header, StmtSize &size, string anchorName);
    void close_if_tree();

    // different cost tables
    void prod_cons_table(StmtSize &size);
    void allocate_table(vector<string> &allocationSizes);
    void for_loop_table(string loop_size);

    // opens relative code links
    void see_code_button_div(string anchorName);

    // tooltip
    string info_tooltip(string toolTipText, string className);

    // for cost colors - side bars
    void generate_computation_cost_div(const IRNode *op);
    void generate_memory_cost_div(const IRNode *op);
    void open_content_div();

    // for cost colors - side boxes
    string color_button(int colorRange);
    string computation_button(const IRNode *op);
    string data_movement_button(const IRNode *op);
    string tooltip_table(map<string, string> &table);
    void cost_colors(const IRNode *op);

    void visit_function(const LoweredFunc &func);
    void visit(const Variable *op) override;
    void visit(const ProducerConsumer *op) override;
    void visit(const For *op) override;
    void visit(const IfThenElse *op) override;
    void visit(const Store *op) override;
    void visit(const Load *op) override;
    void visit(const Allocate *op) override;
};

#endif
