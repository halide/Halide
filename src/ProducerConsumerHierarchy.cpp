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
        // TODO: make sure this is what i want
        return StmtSize();
    }
    return it->second;
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

string StmtSizes::get_simplified_string(string a, string b, string op) {
    if (op == "+") {
        return a + " + " + b;
    }

    else if (op == "*") {
        // check if b contains "+"
        if (b.find("+") != string::npos) {
            return a + "*(" + b + ")";
        } else {
            return a + "*" + b;
        }
    }

    else {
        internal_error << "\n"
                       << "Unsupported operator: " << op << "\n";
        return "";
    }
}

string StmtSizes::print_sizes() const {
    stringstream ss;
    for (const auto &it : stmt_sizes) {
        ss << it.first << ":" << endl;
        for (const auto &it2 : it.second.produces) {
            ss << "    produces " << it2.first << ": " << it2.second << endl;
        }
        for (const auto &it2 : it.second.consumes) {
            ss << "    consumes " << it2.first << ": " << it2.second << endl;
        }
    }
    return ss.str();
}
string StmtSizes::print_produce_sizes(StmtSize &stmtSize) const {
    stringstream ss;
    for (const auto &it : stmtSize.produces) {
        ss << "produces:" << it.first << ": " << it.second << "<br>";
    }
    return ss.str();
}
string StmtSizes::print_consume_sizes(StmtSize &stmtSize) const {
    stringstream ss;
    for (const auto &it : stmtSize.consumes) {
        ss << "consumes:" << it.first << ": " << it.second << "<br>";
    }
    return ss.str();
}

void StmtSizes::set_produce_size(const IRNode *node, string produce_var, string produce_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize();
    }
    stmt_sizes[node].produces[produce_var] = produce_size;
}
void StmtSizes::set_consume_size(const IRNode *node, string consume_var, string consume_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize();
    }
    stmt_sizes[node].consumes[consume_var] = consume_size;
}

string StmtSizes::string_span(string varName) const {
    return "<span class=\\'stringType\\'>" + varName + "</span>";
}
string StmtSizes::int_span(int64_t intVal) const {
    return "<span class=\\'intType\\'>" + to_string(intVal) + "</span>";
}

Stmt StmtSizes::visit(const LetStmt *op) {
    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());

    // set all produces and consumes of the body
    for (const auto &produce_var : bodySize.produces) {
        set_produce_size(op, produce_var.first, produce_var.second);
    }
    for (const auto &consume_var : bodySize.consumes) {
        set_consume_size(op, consume_var.first, consume_var.second);
    }

    return op;
}
Stmt StmtSizes::visit(const ProducerConsumer *op) {

    bool previous_in_producer = in_producer;
    bool previous_in_consumer = in_consumer;

    if (op->is_producer) {
        in_producer = true;
    } else {
        in_consumer = true;
    }

    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());

    // set all produces and consumes of the body
    for (const auto &produce_var : bodySize.produces) {
        set_produce_size(op, produce_var.first, produce_var.second);
    }
    for (const auto &consume_var : bodySize.consumes) {
        set_consume_size(op, consume_var.first, consume_var.second);
    }

    in_producer = previous_in_producer;
    in_consumer = previous_in_consumer;
    return op;
}
Stmt StmtSizes::visit(const For *op) {
    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());

    // don't do anything if body is empty
    if (bodySize.empty()) {
        return op;
    }

    Expr min = op->min;
    Expr extent = op->extent;

    string loopIterator;

    // check if min and extend are of type IntImm
    if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::IntImm) {
        int64_t minValue = min.as<IntImm>()->value;
        int64_t extentValue = extent.as<IntImm>()->value;
        uint16_t range = uint16_t(extentValue - minValue);
        loopIterator = int_span(range);
    }

    else if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::Variable) {
        int64_t minValue = min.as<IntImm>()->value;
        string minName = int_span(minValue);
        string extentName = string_span(extent.as<Variable>()->name);

        // TODO: inline variable for extentName

        if (minValue == 0) {
            loopIterator = extentName;
        } else {
            loopIterator = "(" + extentName + " - " + minName + ")";
        }
    }

    else {
        internal_error
            << "\n"
            << print_node(op->min.get()) << "\n"
            << "StmtSizes::visit(const For *op): min and extent are not of type (IntImm) "
               "or (IntImm & Variable) - "
               "can't generate ProdCons hierarchy yet. \n\n";
    }

    for (const auto &produce_var : bodySize.produces) {
        string bodyProduceSize = produce_var.second;
        string opProduceSize = get_simplified_string(loopIterator, bodyProduceSize, "*");
        set_produce_size(op, produce_var.first, opProduceSize);
    }
    for (const auto &consume_var : bodySize.consumes) {
        string bodyConsumeSize = consume_var.second;
        string opConsumeSize = get_simplified_string(loopIterator, bodyConsumeSize, "*");
        set_consume_size(op, consume_var.first, opConsumeSize);
    }

    return op;
}
Stmt StmtSizes::visit(const Store *op) {

    uint16_t lanes = op->value.type().lanes();

    if (in_producer) {
        set_produce_size(op, op->name, int_span(lanes));
    }

    if (in_consumer) {
        mutate(op->value);
        set_consume_size(op, curr_consumer, int_span(lanes));
    }
    return op;
}
Expr StmtSizes::visit(const Load *op) {

    // only set curr_consumer if in consumer
    if (in_consumer) {
        curr_consumer = op->name;
    }

    return op;
}
Stmt StmtSizes::visit(const Block *op) {
    mutate(op->first);
    mutate(op->rest);
    StmtSize firstSize = get_size(op->first.get());
    StmtSize restSize = get_size(op->rest.get());

    // copies over all produces and consumes from the first statement
    for (const auto &produce_var : firstSize.produces) {
        string firstProduceSize = produce_var.second;
        set_produce_size(op, produce_var.first, firstProduceSize);
    }
    for (const auto &consume_var : firstSize.consumes) {
        string firstConsumeSize = consume_var.second;
        set_consume_size(op, consume_var.first, firstConsumeSize);
    }

    // copies over all produces and consumes from the rest statement - takes into
    // account that the first might already have set some produces and consumes
    for (const auto &produce_var : restSize.produces) {
        string restProduceSize = produce_var.second;

        // check if produce_var.first is in firstSize.consumes
        auto it = firstSize.produces.find(produce_var.first);
        if (it != firstSize.produces.end()) {
            string firstProducesSize = it->second;
            string opProduceSize = get_simplified_string(firstProducesSize, restProduceSize, "+");

            set_produce_size(op, produce_var.first, opProduceSize);
        } else {
            set_produce_size(op, produce_var.first, restProduceSize);
        }
    }
    for (const auto &consume_var : restSize.consumes) {
        string restConsumeSize = consume_var.second;

        // check if consume_var.first is in firstSize.produces
        auto it = firstSize.consumes.find(consume_var.first);
        if (it != firstSize.consumes.end()) {
            string firstConsumeSize = it->second;
            string opConsumeSize = get_simplified_string(firstConsumeSize, restConsumeSize, "+");

            set_consume_size(op, consume_var.first, opConsumeSize);
        } else {
            set_consume_size(op, consume_var.first, restConsumeSize);
        }
    }

    return op;
}
Stmt StmtSizes::visit(const Allocate *op) {
    mutate(op->body);
    StmtSize bodySize = get_size(op->body.get());

    // set all produces and consumes of the body
    for (const auto &produce_var : bodySize.produces) {
        set_produce_size(op, produce_var.first, produce_var.second);
    }
    for (const auto &consume_var : bodySize.consumes) {
        set_consume_size(op, consume_var.first, consume_var.second);
    }

    return op;
}
Stmt StmtSizes::visit(const IfThenElse *op) {
    mutate(op->then_case);
    mutate(op->else_case);

    StmtSize thenSize = get_size(op->then_case.get());
    StmtSize elseSize = get_size(op->else_case.get());

    // copies over all produces and consumes from the then statement
    for (const auto &produce_var : thenSize.produces) {
        string thenProduceSize = produce_var.second;
        set_produce_size(op, produce_var.first, thenProduceSize);
    }
    for (const auto &consume_var : thenSize.consumes) {
        string thenConsumeSize = consume_var.second;
        set_consume_size(op, consume_var.first, thenConsumeSize);
    }

    // copies over all produces and consumes from the else statement - takes into
    // account that the then might already have set some produces and consumes
    for (const auto &produce_var : elseSize.produces) {
        string elseProduceSize = produce_var.second;

        // check if produce_var.first is in thenSize.consumes
        auto it = thenSize.produces.find(produce_var.first);
        if (it != thenSize.produces.end()) {
            string thenProducesSize = it->second;
            string opProduceSize = get_simplified_string(thenProducesSize, elseProduceSize, "+");

            set_produce_size(op, produce_var.first, opProduceSize);
        } else {
            set_produce_size(op, produce_var.first, elseProduceSize);
        }
    }
    for (const auto &consume_var : elseSize.consumes) {
        string elseConsumeSize = consume_var.second;

        // check if consume_var.first is in thenSize.produces
        auto it = thenSize.consumes.find(consume_var.first);
        if (it != thenSize.consumes.end()) {
            string thenConsumeSize = it->second;
            string opConsumeSize = get_simplified_string(thenConsumeSize, elseConsumeSize, "+");

            set_consume_size(op, consume_var.first, opConsumeSize);
        } else {
            set_consume_size(op, consume_var.first, elseConsumeSize);
        }
    }

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

    html << "span.intType { color: #099; }";
    html << "span.stringType { color: #990073; }";

    html << ".costTableHeader {";
    html << "border-bottom: 1px solid black;";
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

    html << "<th colspan=\\'2\\' class=\\'costTableHeader\\'>";
    html << "Prod";
    html << "</th>";

    html << "<th colspan=\\'2\\' class=\\'costTableHeader\\'>";
    html << "Cons";
    html << "</th>";

    html << "</tr>";

    // TODO: add something if both are empty so that there are 2 empty cols

    vector<string> rows;
    for (const auto &produce_var : size.produces) {
        stringstream ss;
        ss << "<td class=\\'costTableData\\'>";
        ss << produce_var.first;
        ss << "</td>";

        ss << "<td class=\\'costTableData\\'>";
        ss << produce_var.second;
        ss << "</td>";

        rows.push_back(ss.str());
    }
    unsigned long rowNum = 0;
    for (const auto &consume_var : size.consumes) {
        stringstream ss;
        ss << "<td class=\\'costTableData\\'>";
        ss << consume_var.first;
        ss << "</td>";

        ss << "<td class=\\'costTableData\\'>";
        ss << consume_var.second;
        ss << "</td>";

        if (rowNum < rows.size()) {
            rows[rowNum] += ss.str();
        } else {
            stringstream sEmpty;
            sEmpty << "<td class=\\'costTableData\\'>";
            sEmpty << "</td>";
            sEmpty << "<td class=\\'costTableData\\'>";
            sEmpty << "</td>";

            rows.push_back(sEmpty.str() + ss.str());
        }
        rowNum++;
    }
    rowNum = size.consumes.size();
    while (rowNum < size.produces.size()) {
        stringstream sEmpty;
        sEmpty << "<td class=\\'costTableData\\'>";
        sEmpty << "</td>";
        sEmpty << "<td class=\\'costTableData\\'>";
        sEmpty << "</td>";

        rows[rowNum] += sEmpty.str();
        rowNum++;
    }

    for (const auto &row : rows) {
        html << "<tr>";
        html << row;
        html << "</tr>";
    }

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
    header << (op->is_producer ? "Produce" : "Consume");
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
    StmtSize size;

    size = pre_processor.get_size(op);

    stringstream header;
    header << "For (" << op->name << ")";

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
    // TODO: inline condition
    ifHeader << "if (" << op->condition << ")";
    table_header(ifHeader.str(), thenSize);

    stringstream elseHeader;
    elseHeader << "else";
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

string StmtSizes::print_node(const IRNode *node) const {
    stringstream s;
    s << "Crashing node has type: ";
    IRNodeType type = node->node_type;
    if (type == IRNodeType::IntImm) {
        s << "IntImm type" << endl;
        auto node1 = dynamic_cast<const IntImm *>(node);
        s << "value: " << node1->value << endl;
    } else if (type == IRNodeType::UIntImm) {
        s << "UIntImm type" << endl;
    } else if (type == IRNodeType::FloatImm) {
        s << "FloatImm type" << endl;
    } else if (type == IRNodeType::StringImm) {
        s << "StringImm type" << endl;
    } else if (type == IRNodeType::Broadcast) {
        s << "Broadcast type" << endl;
    } else if (type == IRNodeType::Cast) {
        s << "Cast type" << endl;
    } else if (type == IRNodeType::Variable) {
        s << "Variable type" << endl;
    } else if (type == IRNodeType::Add) {
        s << "Add type" << endl;
    } else if (type == IRNodeType::Sub) {
        s << "Sub type" << endl;
    } else if (type == IRNodeType::Mod) {
        s << "Mod type" << endl;
    } else if (type == IRNodeType::Mul) {
        s << "Mul type" << endl;
    } else if (type == IRNodeType::Div) {
        s << "Div type" << endl;
    } else if (type == IRNodeType::Min) {
        s << "Min type" << endl;
    } else if (type == IRNodeType::Max) {
        s << "Max type" << endl;
    } else if (type == IRNodeType::EQ) {
        s << "EQ type" << endl;
    } else if (type == IRNodeType::NE) {
        s << "NE type" << endl;
    } else if (type == IRNodeType::LT) {
        s << "LT type" << endl;
    } else if (type == IRNodeType::LE) {
        s << "LE type" << endl;
    } else if (type == IRNodeType::GT) {
        s << "GT type" << endl;
    } else if (type == IRNodeType::GE) {
        s << "GE type" << endl;
    } else if (type == IRNodeType::And) {
        s << "And type" << endl;
    } else if (type == IRNodeType::Or) {
        s << "Or type" << endl;
    } else if (type == IRNodeType::Not) {
        s << "Not type" << endl;
    } else if (type == IRNodeType::Select) {
        s << "Select type" << endl;
    } else if (type == IRNodeType::Load) {
        s << "Load type" << endl;
    } else if (type == IRNodeType::Ramp) {
        s << "Ramp type" << endl;
    } else if (type == IRNodeType::Call) {
        s << "Call type" << endl;
    } else if (type == IRNodeType::Let) {
        s << "Let type" << endl;
    } else if (type == IRNodeType::Shuffle) {
        s << "Shuffle type" << endl;
    } else if (type == IRNodeType::VectorReduce) {
        s << "VectorReduce type" << endl;
    } else if (type == IRNodeType::LetStmt) {
        s << "LetStmt type" << endl;
    } else if (type == IRNodeType::AssertStmt) {
        s << "AssertStmt type" << endl;
    } else if (type == IRNodeType::ProducerConsumer) {
        s << "ProducerConsumer type" << endl;
    } else if (type == IRNodeType::For) {
        s << "For type" << endl;
    } else if (type == IRNodeType::Acquire) {
        s << "Acquire type" << endl;
    } else if (type == IRNodeType::Store) {
        s << "Store type" << endl;
    } else if (type == IRNodeType::Provide) {
        s << "Provide type" << endl;
    } else if (type == IRNodeType::Allocate) {
        s << "Allocate type" << endl;
    } else if (type == IRNodeType::Free) {
        s << "Free type" << endl;
    } else if (type == IRNodeType::Realize) {
        s << "Realize type" << endl;
    } else if (type == IRNodeType::Block) {
        s << "Block type" << endl;
    } else if (type == IRNodeType::Fork) {
        s << "Fork type" << endl;
    } else if (type == IRNodeType::IfThenElse) {
        s << "IfThenElse type" << endl;
    } else if (type == IRNodeType::Evaluate) {
        s << "Evaluate type" << endl;
    } else if (type == IRNodeType::Prefetch) {
        s << "Prefetch type" << endl;
    } else if (type == IRNodeType::Atomic) {
        s << "Atomic type" << endl;
    } else {
        s << "Unknown type" << endl;
    }

    return s.str();
}
