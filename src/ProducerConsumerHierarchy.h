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

    void generate_producer_consumer_html(const Module &m);

    void generate_producer_consumer_html(const Stmt &stmt);

    void print_hiararchy();

private:
    std::stringstream html;

    string get_hierarchy_html(const Expr &startNode);
    string get_producer_consumer_html(const Stmt &startNode);

    void traverse(const Module &m);

    void start_html();
    void end_html();

    void open_table();
    void close_table();

    void table_header(const string &header);
    void open_table_row();
    void close_table_row();

    Stmt visit(const ProducerConsumer *op) override;
};

#endif
