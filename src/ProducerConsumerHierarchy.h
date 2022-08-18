#ifndef ProducerConsumerHierarchy_H
#define ProducerConsumerHierarchy_H

#include "FindStmtCost.h"
#include "IRMutator.h"

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

#define SHOW_CUMULATIVE_COST false
#define SHOW_UNIQUE_LOADS false

struct StmtSize {
    map<string, string> produces;
    map<string, string> consumes;
    map<string, string> allocates;
    string forLoopSize;
    vector<string> allocationSizes;

    bool empty() const {
        return produces.size() == 0 && consumes.size() == 0 && allocates.size() == 0;
    }
    bool emptyAllocated() const {
        return allocates.size() == 0;
    }
};

/*
 * StmtSizes class
 */
class StmtSizes : public IRMutator {
public:
    StmtSizes() = default;
    ~StmtSizes() = default;

    void generate_sizes(const Module &m);
    void generate_sizes(const Stmt &stmt);

    StmtSize get_size(const IRNode *node) const;
    string get_allocation_size(const IRNode *node, const string &name) const;

    string print_sizes() const;
    string print_produce_sizes(StmtSize &stmtSize) const;
    string print_consume_sizes(StmtSize &stmtSize) const;

private:
    using IRMutator::visit;

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

    Stmt visit(const LetStmt *op) override;
    Stmt visit(const ProducerConsumer *op) override;
    Stmt visit(const For *op) override;
    Stmt visit(const Store *op) override;
    void add_load_value(const string &name, const int lanes);
    void add_load_value_unique_loads(const string &name, set<int> &load_values);
    Expr visit(const Load *op) override;
    Stmt visit(const Allocate *op) override;
    Stmt visit(const Block *op) override;
    Stmt visit(const IfThenElse *op) override;

    string print_node(const IRNode *node) const;
};

/*
 * ProducerConsumerHierarchy class
 */
class ProducerConsumerHierarchy : public IRMutator {

public:
    ProducerConsumerHierarchy(string fileName, FindStmtCost findStmtCostPopulated)
        : output_file_name(fileName), findStmtCost(findStmtCostPopulated) {
    }
    ~ProducerConsumerHierarchy() = default;

    // generates the html for the producer-consumer hierarchy
    string generate_producer_consumer_html(const Module &m);
    string generate_producer_consumer_html(const Stmt &stmt);

private:
    using IRMutator::visit;

    std::stringstream html;     // main html string
    StmtSizes pre_processor;    // generates the sizes of the nodes
    string output_file_name;    // used for anchoring
    FindStmtCost findStmtCost;  // used to determine the color of each statement

    // used for getting anchor names
    int ifCount = 0;
    int producerConsumerCount = 0;
    int forCount = 0;
    int storeCount = 0;
    int allocateCount = 0;

    // for traversal of a Module object
    void traverse(const Module &m);

    // opens and closes a table
    void open_table(string backgroundColor);
    void close_table();

    // creates a table header row with given header string
    void table_header(const IRNode *op, const string &header, StmtSize &size, string anchorName);
    void prod_cons_table(StmtSize &size);

    void allocate_table_header(const Allocate *op, const string &header, StmtSize &size,
                               string anchorName);
    void allocate_table(vector<string> &allocationSizes);

    void for_loop_table_header(const For *op, const string &header, StmtSize &size,
                               string anchorName);
    void for_loop_table(string loop_size);

    void if_tree(const IRNode *op, const string &header, StmtSize &size, string anchorName);
    void close_if_tree();

    void see_code_button(string anchorName);

    // opens and closes a row
    void open_table_row();
    void close_table_row();

    // opens and closes a data cell
    void open_table_data(string colSpan);
    void close_table_data();

    // for cost colors
    void open_span(string className);
    void close_span();
    void cost_color_spacer();
    void cost_colors(const IRNode *op);

    Stmt visit(const ProducerConsumer *op) override;
    Stmt visit(const For *op) override;
    Stmt visit(const IfThenElse *op) override;
    Stmt visit(const Store *op) override;
    Expr visit(const Load *op) override;
    Stmt visit(const Allocate *op) override;
};

#endif
