#ifndef ProducerConsumerHierarchy_H
#define ProducerConsumerHierarchy_H

#include "IRMutator.h"

#include <unordered_map>

using namespace std;
using namespace Halide;
using namespace Internal;

struct StmtSize {
    uint16_t produce_size;
    uint16_t consume_size;

    bool empty() const {
        return produce_size == 0 && consume_size == 0;
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
    bool are_bounds_set();

private:
    using IRMutator::visit;

    unordered_map<const IRNode *, StmtSize> stmt_sizes;
    bool bounds_set = false;
    bool in_producer = false;

    void traverse(const Module &m);

    void set_size(const IRNode *node, uint16_t produce_size, uint16_t consume_size);

    Stmt visit(const LetStmt *op) override;
    Stmt visit(const ProducerConsumer *op) override;
    Stmt visit(const For *op) override;
    Stmt visit(const Store *op) override;
    Stmt visit(const Allocate *op) override;
    Stmt visit(const Block *op) override;
};

/*
 * ProducerConsumerHierarchy class
 */
class ProducerConsumerHierarchy : public IRMutator {

public:
    ProducerConsumerHierarchy() = default;
    ~ProducerConsumerHierarchy() = default;

    // generates the html for the producer-consumer hierarchy
    string generate_producer_consumer_html(const Module &m);
    string generate_producer_consumer_html(const Stmt &stmt);

private:
    using IRMutator::visit;

    std::stringstream html;   // main html string
    StmtSizes pre_processor;  // generates the sizes of the nodes

    // starts the traversal of the tree and returns the generated html
    string get_producer_consumer_html(const Expr &startNode);
    string get_producer_consumer_html(const Stmt &startNode);

    // for traversal of a Module object
    void traverse(const Module &m);

    // starts and ends the html file
    void start_html();
    void end_html();

    // opens and closes a table
    void open_table();
    void close_table();

    // creates a table header row with given header string
    void table_header(const string &header, StmtSize &size);
    void prod_cons_table(StmtSize &size);
    // void double_table_header(const string &header);

    // opens and closes a row
    void open_table_row();
    void close_table_row();

    // opens and closes a data cell
    void open_table_data();
    void close_table_data();

    Stmt visit(const ProducerConsumer *op) override;
    Stmt visit(const For *op) override;
    Stmt visit(const IfThenElse *op) override;
};

#endif
