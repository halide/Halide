#ifndef ProducerConsumerHierarchy_H
#define ProducerConsumerHierarchy_H

#include "IRMutator.h"

using namespace std;
using namespace Halide;
using namespace Internal;

class ProducerConsumerHierarchy : public IRMutator {

public:
    ProducerConsumerHierarchy() = default;
    ~ProducerConsumerHierarchy() = default;

    // generates the html for the producer-consumer hierarchy
    // (TODO: eventually, this will return something to StmtToViz so
    // that it can get put into the html)
    void generate_producer_consumer_html(const Module &m);
    void generate_producer_consumer_html(const Stmt &stmt);

    // prints the hierarchy to stdout
    void print_hierarchy();

private:
    std::stringstream html;  // main html string

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
    void table_header(const string &header);

    // opens and closes a normal row
    void open_table_row();
    void close_table_row();

    Stmt visit(const ProducerConsumer *op) override;
};

#endif
