#include "ProducerConsumerHierarchy.h"
#include "Module.h"

using namespace std;
using namespace Halide;
using namespace Internal;

/*
 * StmtSizes class
 */

void StmtSizes::generate_sizes(const Module &m) {
    traverse(m);
}
void StmtSizes::generate_sizes(const Stmt &stmt) {
    mutate(stmt);
}

StmtSize StmtSizes::get_size(const IRNode *node) const {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        return StmtSize{0, 0};
        // TODO: make sure this is what i want
    }
    return it->second;
}
bool StmtSizes::are_bounds_set() {
    return bounds_set;
}

void StmtSizes::traverse(const Module &m) {
    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }
    // traverse all functions
    for (const auto &f : m.functions()) {
        mutate(f.body);
    }
}

void StmtSizes::set_size(const IRNode *node, uint16_t produce_size, uint16_t consume_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize{produce_size, consume_size};
    } else {
        m_assert(false, "StmtSizes::set_size: node already set");
        it->second.produce_size = produce_size;
        it->second.consume_size = consume_size;
    }
}

Stmt StmtSizes::visit(const LetStmt *op) {
    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());
    set_size(op, bodySize.produce_size, bodySize.consume_size);
    return op;
}
Stmt StmtSizes::visit(const ProducerConsumer *op) {

    in_producer = op->is_producer;

    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());
    set_size(op, bodySize.produce_size, bodySize.consume_size);
    return op;
}
Stmt StmtSizes::visit(const For *op) {
    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());

    Expr min = op->min;
    Expr extent = op->extent;

    // check if min and extend are of type IntImm
    if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::IntImm) {
        int64_t minValue = min.as<IntImm>()->value;
        int64_t extentValue = extent.as<IntImm>()->value;
        uint16_t range = uint16_t(extentValue - minValue);

        set_size(op, bodySize.produce_size * range, bodySize.consume_size * range);
        bounds_set = true;
    }

    else {
        set_size(op, bodySize.produce_size, bodySize.consume_size);
    }

    return op;
}
Stmt StmtSizes::visit(const Store *op) {
    uint16_t lanes = op->value.type().lanes();
    if (in_producer) {
        set_size(op, lanes, 0);
    } else {
        set_size(op, 0, lanes);
    }
    return op;
}
Stmt StmtSizes::visit(const Block *op) {
    mutate(op->first);
    mutate(op->rest);
    StmtSize firstSize = get_size(op->first.get());
    StmtSize restSize = get_size(op->rest.get());
    set_size(op, firstSize.produce_size + restSize.produce_size,
             firstSize.consume_size + restSize.consume_size);
    return op;
}
Stmt StmtSizes::visit(const IfThenElse *op) {
    cout << "IfThenElse" << endl;
    mutate(op->then_case);
    mutate(op->else_case);
    return op;
}
Stmt StmtSizes::visit(const Allocate *op) {
    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());
    set_size(op, bodySize.produce_size, bodySize.consume_size);

    return op;
}

/*
 * ProducerConsumerHierarchy class
 */
string ProducerConsumerHierarchy::generate_producer_consumer_html(const Module &m) {
    pre_processor.generate_sizes(m);

    start_html();
    traverse(m);
    end_html();

    return html.str();
}
string ProducerConsumerHierarchy::generate_producer_consumer_html(const Stmt &stmt) {
    pre_processor.generate_sizes(stmt);

    start_html();
    mutate(stmt);
    end_html();

    return html.str();
}

string ProducerConsumerHierarchy::get_producer_consumer_html(const Expr &startNode) {
    start_html();
    mutate(startNode);
    end_html();

    return html.str();
}
string ProducerConsumerHierarchy::get_producer_consumer_html(const Stmt &startNode) {
    start_html();
    mutate(startNode);
    end_html();

    return html.str();
}

void ProducerConsumerHierarchy::traverse(const Module &m) {
    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }
    // traverse all functions
    for (const auto &f : m.functions()) {
        mutate(f.body);
    }
}

void ProducerConsumerHierarchy::start_html() {
    html.str(string());
    html << "<html>";

    html << "<head>";
    html << "</head>";

    html << "<style>";

    html << "body {";
    html << "font-family: Consolas, \\'Liberation Mono\\', Menlo, Courier, monospace;";
    html << "}";

    html << "table, th, td { ";
    html << "border-left: 1px solid black;";
    html << "border-right: 1px solid black;";
    html << "padding: 10px;";
    html << "background-color: rgba(150, 150, 150, 0.15);";
    html << "} ";

    html << "table {";
    html << "border: 1px solid black;";
    html << "border-collapse: collapse;";
    html << "font-size: 12px";
    html << "}";

    html << ".costTable {";
    html << "float: right;";
    html << "text-align: center;";
    html << "}";

    html << ".costTableHeader,";
    html << ".costTableData {";
    html << "border-collapse: collapse;";
    html << "padding-top: 1px;";
    html << "padding-bottom: 1px;";
    html << "padding-left: 5px;";
    html << "padding-right: 5px;";
    html << "}";

    html << "</style>";

    html << "<body>";
}
void ProducerConsumerHierarchy::end_html() {
    html << "</body></html>";
}

void ProducerConsumerHierarchy::open_table() {
    html << "<br>";
    html << "<table>";
}
void ProducerConsumerHierarchy::close_table() {
    html << "</table>";
}

void ProducerConsumerHierarchy::table_header(const string &header, StmtSize &size) {
    html << "<th>";
    html << header << "&nbsp&nbsp&nbsp";
    prod_cons_table(size);
    html << "</th>";
}
void ProducerConsumerHierarchy::prod_cons_table(StmtSize &size) {
    // open table
    html << "<table class=\\'costTable\\'>";

    // Prod | Cons
    html << "<tr>";

    html << "<th class=\\'costTableHeader\\'>";
    html << "Prod";
    html << "</th>";

    html << "<th class=\\'costTableHeader\\'>";
    html << "Cons";
    html << "</th>";

    html << "</tr>";

    // produce_size | consume_size
    html << "<tr>";

    html << "<td class=\\'costTableData\\'>";
    html << size.produce_size;
    html << "</td>";

    html << "<td class=\\'costTableData\\'>";
    html << size.consume_size;
    html << "</td>";

    html << "</tr>";

    // close table
    html << "</table>";
}

void ProducerConsumerHierarchy::open_table_row() {
    html << "<tr>";
}
void ProducerConsumerHierarchy::close_table_row() {
    html << "</tr>";
}

void ProducerConsumerHierarchy::open_table_data() {
    html << "<td>";
}
void ProducerConsumerHierarchy::close_table_data() {
    html << "</td>";
}

Stmt ProducerConsumerHierarchy::visit(const ProducerConsumer *op) {
    open_table();

    stringstream header;
    header << (op->is_producer ? "Produce" : "Consumer");
    header << " " << op->name;
    StmtSize size = pre_processor.get_size(op);

    open_table_row();
    table_header(header.str(), size);
    close_table_row();

    open_table_row();
    open_table_data();
    mutate(op->body);
    close_table_data();
    close_table_row();

    close_table();

    return op;
}

Stmt ProducerConsumerHierarchy::visit(const For *op) {
    open_table();
    stringstream header;
    header << "For";
    StmtSize size;

    if (pre_processor.are_bounds_set()) {
        size = pre_processor.get_size(op);
    } else {
        // TODO: handle case where bounds are not set
    }

    open_table_row();
    table_header(header.str(), size);
    close_table_row();

    open_table_row();
    open_table_data();
    mutate(op->body);
    close_table_data();
    close_table_row();
    close_table();
    return op;
}

Stmt ProducerConsumerHierarchy::visit(const IfThenElse *op) {
    // TODO: change this to account for many if then elses
    //       nested in "else_case". look at stmtToViz to
    //       see how to do this
    StmtSize thenSize = pre_processor.get_size(op->then_case.get());
    StmtSize elseSize = pre_processor.get_size(op->else_case.get());

    // don't draw anything if both cases are empty
    if (thenSize.empty() && elseSize.empty()) {
        return op;
    }

    // open table and set header
    open_table();

    // THEN CASE | ELSE CASE
    open_table_row();
    stringstream ifHeader;
    ifHeader << "if (" << op->condition << ")";
    table_header(ifHeader.str(), thenSize);

    stringstream elseHeader;
    elseHeader << "if (! (" << op->condition << "))";
    table_header(elseHeader.str(), elseSize);
    close_table_row();

    // fill in the then and else cases
    open_table_row();

    open_table_data();
    mutate(op->then_case);
    close_table_data();

    open_table_data();
    mutate(op->else_case);
    close_table_data();

    close_table_row();

    // close table
    close_table();

    return op;
}
