#include "ProducerConsumerHierarchy.h"
#include "IROperator.h"
#include "Module.h"

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

    // return empty size if not found (this just means it wasn't set)
    if (it == stmt_sizes.end()) {
        return StmtSize();
    }

    return it->second;
}
string StmtSizes::get_allocation_size(const IRNode *node, const string &name) const {
    StmtSize size = get_size(node);

    auto it = size.allocates.find(name);
    if (it == size.allocates.end()) {
        internal_error << "\n"
                       << print_node(node) << "\n"
                       << "StmtSizes::get_allocation_size - " << name << " not found in allocates\n"
                       << "\n\n";
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
        get_function_arguments(f);
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

void StmtSizes::get_function_arguments(const LoweredFunc &op) {
    for (size_t i = 0; i < op.args.size(); i++) {
        arguments.push_back(op.args[i].name);
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
void StmtSizes::set_allocation_size_old(const IRNode *node, string allocate_var,
                                        string allocate_size) {
    auto it = stmt_sizes.find(node);

    // error if size found and allocate_var already exists
    if (it != stmt_sizes.end()) {
        auto it2 = it->second.allocates.find(allocate_var);
        if (it2 != it->second.allocates.end()) {
            internal_error << "\n"
                           << print_node(node) << "\n"
                           << "StmtSizes::set_allocation_size - " << allocate_var
                           << " already found in allocates\n"
                           << "\n\n";
        }
    }

    // if size not found, create new entry
    else {
        stmt_sizes[node] = StmtSize();
    }

    // set allocation size
    stmt_sizes[node].allocates[allocate_var] = allocate_size;
}
void StmtSizes::set_for_loop_size(const IRNode *node, string for_loop_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize();
    }
    stmt_sizes[node].forLoopSize = for_loop_size;
}
void StmtSizes::set_allocation_size(const IRNode *node, string allocate_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize();
    }
    stmt_sizes[node].allocationSizes.push_back(allocate_size);
}

bool StmtSizes::in_producer(const string &name) const {
    // check if name is in curr_producer_names
    for (const auto &it : curr_producer_names) {
        if (it == name) {
            return true;
        }
    }
    return false;
}
bool StmtSizes::in_consumer(const string &name) const {
    // check if name is in curr_consumer_names
    for (const auto &it : curr_consumer_names) {
        if (it == name) {
            return true;
        }
    }
    return false;
}
void StmtSizes::remove_producer(const string &name) {
    // remove name from curr_producer_names
    for (size_t i = 0; i < curr_producer_names.size(); i++) {
        if (curr_producer_names[i] == name) {
            curr_producer_names.erase(curr_producer_names.begin() + i);
            return;
        }
    }
}
void StmtSizes::remove_consumer(const string &name) {
    // remove name from curr_consumer_names
    for (size_t i = 0; i < curr_consumer_names.size(); i++) {
        if (curr_consumer_names[i] == name) {
            curr_consumer_names.erase(curr_consumer_names.begin() + i);
            return;
        }
    }
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

    if (op->is_producer) {
        curr_producer_names.push_back(op->name);
    } else {
        curr_consumer_names.push_back(op->name);
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

    // remove name from curr_producer_names or curr_consumer_names
    if (op->is_producer) {
        remove_producer(op->name);
    } else {
        remove_consumer(op->name);
    }

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

    else if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::Add) {
        int64_t minValue = min.as<IntImm>()->value;
        string minName = int_span(minValue);
        string extentName = "(";

        // deal with a
        if (extent.as<Add>()->a.node_type() == IRNodeType::IntImm) {
            int64_t extentValue = extent.as<Add>()->a.as<IntImm>()->value;
            extentName += int_span(extentValue);
        } else if (extent.as<Add>()->a.node_type() == IRNodeType::Variable) {
            extentName += string_span(extent.as<Add>()->a.as<Variable>()->name);
        } else {
            internal_error << "\n"
                           << "In for loop: " << op->name << "\n"
                           << print_node(extent.as<Add>()->a.get()) << "\n"
                           << "StmtSizes::visit(const For *op): add->a isn't IntImm or Variable - "
                              "can't generate ProdCons hierarchy yet. \n\n";
        }

        extentName += "+";

        // deal with b
        if (extent.as<Add>()->b.node_type() == IRNodeType::IntImm) {
            int64_t extentValue = extent.as<Add>()->b.as<IntImm>()->value;
            extentName += int_span(extentValue);
        } else if (extent.as<Add>()->b.node_type() == IRNodeType::Variable) {
            extentName += string_span(extent.as<Add>()->b.as<Variable>()->name);
        } else {
            internal_error << "\n"
                           << "In for loop: " << op->name << "\n"
                           << print_node(extent.as<Add>()->b.get()) << "\n"
                           << "StmtSizes::visit(const For *op): add->b isn't IntImm or Variable - "
                              "can't generate ProdCons hierarchy yet. \n\n";
        }
        extentName += ")";

        if (minValue == 0) {
            loopIterator = extentName;
        } else {
            loopIterator = "(" + extentName + " - " + minName + ")";
        }

    }

    else {
        internal_error
            << "\n"
            << "In for loop: " << op->name << "\n"
            << print_node(op->min.get()) << "\n"
            << print_node(op->extent.get()) << "\n"
            << "StmtSizes::visit(const For *op): min and extent are not of type (IntImm) "
               "or (IntImm & Variable) or (IntImm & Add) - "
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

    set_for_loop_size(op, loopIterator);

    return op;
}
Stmt StmtSizes::visit(const Store *op) {

    // TODO: is this correct? should i be getting it from `index`?
    uint16_t lanes = op->index.type().lanes();
    // uint16_t lanes = op->type.lanes();

    if (in_producer(op->name)) {
        set_produce_size(op, op->name, int_span(lanes));
    }

    // empty curr_load_values
    curr_load_values.clear();
    mutate(op->value);

    for (const auto &load_var : curr_load_values) {
        set_consume_size(op, load_var.first, int_span(load_var.second));
    }

    return op;
}
void StmtSizes::add_load_value(const string &name, const int lanes) {
    auto it = curr_load_values.find(name);
    if (it == curr_load_values.end()) {
        curr_load_values[name] = lanes;
    } else {
        curr_load_values[name] += lanes;
    }
}
Expr StmtSizes::visit(const Load *op) {

    // see if the variable is in the arguments variable
    if (std::count(arguments.begin(), arguments.end(), op->name)) {
        curr_consumer_names.push_back(op->name);
    }

    if (in_consumer(op->name)) {
        // TODO: make sure this logic is right
        int lanes;

        if (op->index.as<Ramp>()) {
            lanes = op->index.as<Ramp>()->lanes;
        } else {
            lanes = int(op->type.lanes());
        }

        add_load_value(op->name, lanes);
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

    // set allocate stuff
    stringstream type;
    type << "<span class=\\'stringtype\\'>" << op->type << "</span>";
    set_allocation_size(op, type.str());

    for (const auto &extent : op->extents) {
        stringstream ss;
        ss << "<span class=\\'intType\\'>" << extent << "</span>";
        set_allocation_size(op, ss.str());
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
    html << "<link rel=\\'stylesheet\\' "
            "href=\\'https://unpkg.com/treeflex/dist/css/treeflex.css\\'>";
    html << "</head>";

    html << "<style>";

    // if-stmt hierarchy tree style
    html << ".tf-custom .tf-nc { ";
    html << "border-radius: 5px; ";
    html << "border: 1px solid; ";
    html << "font-size: 12px; ";
    html << "background-color: " << IF_COLOR << ";";
    html << "}";
    html << ".tf-custom .end-node { border-style: dashed; font-size: 12px; } ";
    html << ".tf-custom .tf-nc:before, .tf-custom .tf-nc:after { border-left-width: 1px; } ";
    html << ".tf-custom li li:before { border-top-width: 1px; }";
    html << ".tf-custom .tf-nc .if-node { background-color: " << IF_COLOR << "; }";

    // cost colors
    html << "span.CostComputation19 { width: 13px; display: inline-block; background: "
            "rgb(130,31,27); color: transparent; }";
    html << "span.CostComputation18 { width: 13px; display: inline-block; background: "
            "rgb(145,33,30); color: transparent; }";
    html << "span.CostComputation17 { width: 13px; display: inline-block; background: "
            "rgb(160,33,32); color: transparent; }";
    html << "span.CostComputation16 { width: 13px; display: inline-block; background: "
            "rgb(176,34,34); color: transparent; }";
    html << "span.CostComputation15 { width: 13px; display: inline-block; background: "
            "rgb(185,47,32); color: transparent; }";
    html << "span.CostComputation14 { width: 13px; display: inline-block; background: "
            "rgb(193,59,30); color: transparent; }";
    html << "span.CostComputation13 { width: 13px; display: inline-block; background: "
            "rgb(202,71,27); color: transparent; }";
    html << "span.CostComputation12 { width: 13px; display: inline-block; background: "
            "rgb(210,82,22); color: transparent; }";
    html << "span.CostComputation11 { width: 13px; display: inline-block; background: "
            "rgb(218,93,16); color: transparent; }";
    html << "span.CostComputation10 { width: 13px; display: inline-block; background: "
            "rgb(226,104,6); color: transparent; }";
    html << "span.CostComputation9 { width: 13px; display: inline-block; background: "
            "rgb(229,118,9); color: transparent; }";
    html << "span.CostComputation8 { width: 13px; display: inline-block; background: "
            "rgb(230,132,15); color: transparent; }";
    html << "span.CostComputation7 { width: 13px; display: inline-block; background: "
            "rgb(231,146,20); color: transparent; }";
    html << "span.CostComputation6 { width: 13px; display: inline-block; background: "
            "rgb(232,159,25); color: transparent; }";
    html << "span.CostComputation5 { width: 13px; display: inline-block; background: "
            "rgb(233,172,30); color: transparent; }";
    html << "span.CostComputation4 { width: 13px; display: inline-block; background: "
            "rgb(233,185,35); color: transparent; }";
    html << "span.CostComputation3 { width: 13px; display: inline-block; background: "
            "rgb(233,198,40); color: transparent; }";
    html << "span.CostComputation2 { width: 13px; display: inline-block; background: "
            "rgb(232,211,45); color: transparent; }";
    html << "span.CostComputation1 { width: 13px; display: inline-block; background: "
            "rgb(231,223,50); color: transparent; }";
    html << "span.CostComputation0 { width: 13px; display: inline-block; background: "
            "rgb(236,233,89); color: transparent;  } ";
    html << "span.CostMovement19 { width: 13px; display: inline-block; background: rgb(130,31,27); "
            "color: transparent; }";
    html << "span.CostMovement18 { width: 13px; display: inline-block; background: rgb(145,33,30); "
            "color: transparent; }";
    html << "span.CostMovement17 { width: 13px; display: inline-block; background: rgb(160,33,32); "
            "color: transparent; }";
    html << "span.CostMovement16 { width: 13px; display: inline-block; background: rgb(176,34,34); "
            "color: transparent; }";
    html << "span.CostMovement15 { width: 13px; display: inline-block; background: rgb(185,47,32); "
            "color: transparent; }";
    html << "span.CostMovement14 { width: 13px; display: inline-block; background: rgb(193,59,30); "
            "color: transparent; }";
    html << "span.CostMovement13 { width: 13px; display: inline-block; background: rgb(202,71,27); "
            "color: transparent; }";
    html << "span.CostMovement12 { width: 13px; display: inline-block; background: rgb(210,82,22); "
            "color: transparent; }";
    html << "span.CostMovement11 { width: 13px; display: inline-block; background: rgb(218,93,16); "
            "color: transparent; }";
    html << "span.CostMovement10 { width: 13px; display: inline-block; background: rgb(226,104,6); "
            "color: transparent; }";
    html << "span.CostMovement9 { width: 13px; display: inline-block; background: rgb(229,118,9); "
            "color: transparent; }";
    html << "span.CostMovement8 { width: 13px; display: inline-block; background: rgb(230,132,15); "
            "color: transparent; }";
    html << "span.CostMovement7 { width: 13px; display: inline-block; background: rgb(231,146,20); "
            "color: transparent; }";
    html << "span.CostMovement6 { width: 13px; display: inline-block; background: rgb(232,159,25); "
            "color: transparent; }";
    html << "span.CostMovement5 { width: 13px; display: inline-block; background: rgb(233,172,30); "
            "color: transparent; }";
    html << "span.CostMovement4 { width: 13px; display: inline-block; background: rgb(233,185,35); "
            "color: transparent; }";
    html << "span.CostMovement3 { width: 13px; display: inline-block; background: rgb(233,198,40); "
            "color: transparent; }";
    html << "span.CostMovement2 { width: 13px; display: inline-block; background: rgb(232,211,45); "
            "color: transparent; }";
    html << "span.CostMovement1 { width: 13px; display: inline-block; background: rgb(231,223,50); "
            "color: transparent; }";
    html << "span.CostMovement0 { width: 13px; display: inline-block; background: rgb(236,233,89); "
            "color: transparent; } ";

    html << "span.CostColorSpacer { width: 2px; color: transparent; display: inline-block;}";

    // producer consumer style
    html << "body {";
    html << "font-family: Consolas, \\'Liberation Mono\\', Menlo, Courier, monospace;";
    html << "}";

    html << "table {";
    html << "border-radius: 5px;";
    html << "font-size: 12px;";
    html << "border: 1px dashed grey;";
    html << "border-collapse: separate;";
    html << "border-spacing: 0;";
    html << "}";

    html << ".center {";
    html << "margin-left: auto;";
    html << "margin-right: auto;";
    html << "} ";

    html << ".ifElseTable {";
    html << "border: 0px;";
    html << "} ";

    html << ".costTable {";
    html << "float: right;";
    html << "text-align: center;";
    html << "border: 0px;";
    html << "}";

    html << ".costTable td {";
    html << "border-top: 1px dashed grey;";
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

    html << ".middleCol {";
    html << "border-right: 1px dashed grey;";
    html << "}";

    // hierarchy tree
    html << ".tf-custom .tf-nc {";
    html << "border-radius: 5px;";
    html << "border: 1px solid;";
    html << "font-size: 12px;";
    html << "padding: 5px;";
    html << "}";
    html << "";
    html << ".tf-custom .end-node {";
    html << "border-style: dashed;";
    html << "font-size: 12px;";
    html << "}";
    html << "";
    html << ".tf-custom .tf-nc:before,";
    html << ".tf-custom .tf-nc:after {";
    html << "border-left-width: 1px;";
    html << "}";
    html << "";
    html << ".tf-custom li li:before {";
    html << "border-top-width: 1px;";
    html << "}";

    html << "</style>";

    html << "<body>";
}
void ProducerConsumerHierarchy::end_html() {
    html << "</body></html>";
}

void ProducerConsumerHierarchy::open_table(string backgroundColor) {
    html << "<br>";
    html << "<table style=\\'";
    html << "background-color: " << backgroundColor << "; ";
    html << "\\' ";
    html << "class=\\'center\\'";
    html << ">";
}
void ProducerConsumerHierarchy::close_table() {
    html << "</table>";
    html << "<br>";
}

void ProducerConsumerHierarchy::table_header(const IRNode *op, const string &header, StmtSize &size,
                                             string anchorName = "") {
    html << "<th>";

    // add cost color squares if op exists
    if (op != nullptr) {
        cost_colors(op);
    }

    // add anchor button if anchorName is provided
    if (anchorName != "") {
        html << "<button onclick=\\'";
        html << "window.open(&quot;" << output_file_name << "#" << anchorName
             << "&quot;, &quot;_blank&quot;)";
        html << "\\'>";
        html << "see code";
        html << "</button>";
    }

    // header
    html << "<br>";
    html << "&nbsp;";
    html << header;
    html << "&nbsp;";
    html << "<br><br>";
    html << "</th>";

    // add producer consumer size if size is provided
    if (!size.empty()) {
        html << "<th>";
        html << "<br>";
        prod_cons_table(size);
        html << "<br><br>";
        html << "</th>";

        // spacing
        html << "<th>";
        html << "&nbsp;";
        html << "</th>";
    }
}
void ProducerConsumerHierarchy::prod_cons_table(StmtSize &size) {
    // open table
    html << "<table class=\\'costTable\\' style=\\'background-color: rgba(150, 150, 150, 0.5)\\'>";

    // Prod | Cons
    html << "<tr>";

    html << "<th colspan=\\'2\\' class=\\'costTableHeader middleCol\\'>";
    html << "Prod";
    html << "</th>";

    html << "<th colspan=\\'2\\' class=\\'costTableHeader\\'>";
    html << "Cons";
    html << "</th>";

    html << "</tr>";

    // produces and consumes are empty - add row with values 0
    if (size.empty()) {
        internal_error << "\n\n"
                       << "ProducerConsumerHierarchy::prod_cons_table - size is empty"
                       << "\n";
    }

    // produces and consumes aren't empty
    else {
        vector<string> rows;

        // fill in producer variables
        for (const auto &produce_var : size.produces) {
            stringstream ss;
            ss << "<td class=\\'costTableData\\'>";
            ss << produce_var.first << ": ";
            ss << "</td>";

            ss << "<td class=\\'costTableData middleCol\\'>";
            ss << produce_var.second;
            ss << "</td>";

            rows.push_back(ss.str());
        }

        // fill in consumer variables
        unsigned long rowNum = 0;
        for (const auto &consume_var : size.consumes) {
            stringstream ss;
            ss << "<td class=\\'costTableData\\'>";
            ss << consume_var.first << ": ";
            ss << "</td>";

            ss << "<td class=\\'costTableData\\'>";
            ss << consume_var.second;
            ss << "</td>";

            if (rowNum < rows.size()) {
                rows[rowNum] += ss.str();
            } else {
                // pad row with empty cells for produce
                stringstream sEmpty;
                sEmpty << "<td colspan=\\'2\\' class=\\'costTableData middleCol\\'>";
                sEmpty << "</td>";

                rows.push_back(sEmpty.str() + ss.str());
            }
            rowNum++;
        }

        // pad row with empty calls for consume
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

        // add rows to html
        for (const auto &row : rows) {
            html << "<tr>";
            html << row;
            html << "</tr>";
        }
    }

    // close table
    html << "</table>";
}

void ProducerConsumerHierarchy::allocate_table_header(const Allocate *op, const string &header,
                                                      StmtSize &size, string anchorName) {
    html << "<th>";

    // add cost color squares
    cost_colors(op);

    // add anchor button
    html << "<button onclick=\\'";
    html << "window.open(&quot;" << output_file_name << "#" << anchorName
         << "&quot;, &quot;_blank&quot;)";
    html << "\\'>";
    html << "see code";
    html << "</button>";

    // header
    html << "<br>";
    html << "&nbsp;";
    html << header;
    html << "&nbsp;";
    html << "<br><br>";
    html << "</th>";

    html << "<th>";
    html << "<br>";

    vector<string> &allocationSizes = size.allocationSizes;

    string type = allocationSizes[0];

    if (op->extents.size() > 2) {
        internal_error
            << "\n\n"
            << "ProducerConsumerHierarchy::allocate_table_header - extents.size() != 2 !!\n"
            << "extents.size() = " << op->extents.size() << "\n"
            << "\n";
    }

    string rows;
    string cols;

    rows = allocationSizes[1];

    if (op->extents.size() == 2) {
        cols = allocationSizes[2];
    }

    allocate_table(type, rows, cols);
    html << "<br><br>";
    html << "</th>";

    // spacing
    html << "<th>";
    html << "&nbsp;";
    html << "</th>";
}
void ProducerConsumerHierarchy::allocate_table(string type, string rows, string cols) {
    // open table
    html << "<table class=\\'costTable\\' style=\\'background-color: rgba(150, 150, 150, 0.5)\\'>";

    // Type | Rows | Cols
    html << "<tr>";

    html << "<th class=\\'costTableHeader middleCol\\'>";
    html << "Type";
    html << "</th>";

    if (cols != "") {
        html << "<th class=\\'costTableHeader middleCol\\'>";
    } else {
        html << "<th class=\\'costTableHeader\\'>";
    }
    html << "Rows";
    html << "</th>";

    if (cols != "") {
        html << "<th class=\\'costTableHeader\\'>";
        html << "Cols";
        html << "</th>";
    }

    html << "</tr>";

    html << "<tr>";

    // type
    html << "<td class=\\'costTableData middleCol\\'>";
    html << type;
    html << "</td>";

    // rows
    if (cols != "") {
        html << "<td class=\\'costTableData middleCol\\'>";
    } else {
        html << "<td class=\\'costTableData\\'>";
    }
    html << rows;
    html << "</td>";

    // cols
    if (cols != "") {
        html << "<td class=\\'costTableData\\'>";
        html << cols;
        html << "</td>";
    }

    html << "</tr>";

    // close table
    html << "</table>";
}

void ProducerConsumerHierarchy::for_loop_table_header(const For *op, const string &header,
                                                      StmtSize &size, string anchorName) {
    html << "<th>";

    // add cost color squares
    cost_colors(op);

    // add anchor button
    html << "<button onclick=\\'";
    html << "window.open(&quot;" << output_file_name << "#" << anchorName
         << "&quot;, &quot;_blank&quot;)";
    html << "\\'>";
    html << "see code";
    html << "</button>";

    // header
    html << "<br>";
    html << "&nbsp;";
    html << header;
    html << "&nbsp;";
    html << "<br><br>";
    html << "</th>";

    html << "<th>";
    html << "<br>";

    string loopSize = pre_processor.get_size(op).forLoopSize;

    for_loop_table(loopSize);
    html << "<br><br>";
    html << "</th>";

    // spacing
    html << "<th>";
    html << "&nbsp;";
    html << "</th>";
}
void ProducerConsumerHierarchy::for_loop_table(string loop_size) {
    // open table
    html << "<table class=\\'costTable\\' style=\\'background-color: rgba(150, 150, 150, 0.5)\\'>";

    // Loop Size
    html << "<tr>";

    html << "<th class=\\'costTableHeader middleCol\\'>";
    html << "Loop Size";
    html << "</th>";

    html << "</tr>";

    html << "<tr>";

    // loop size
    html << "<td class=\\'costTableData\\'>";
    html << loop_size;
    html << "</td>";

    html << "</tr>";

    // close table
    html << "</table>";
}

void ProducerConsumerHierarchy::if_tree(const IRNode *op, const string &header, StmtSize &size,
                                        string anchorName = "") {
    html << "<li>";
    html << "<span class=\\'tf-nc\\'>";

    // open table
    html << "<br>";
    html << "<table style=\\'";
    html << "\\' ";
    html << "class=\\'center ifElseTable\\'";
    html << ">";

    open_table_row();
    table_header(op, header, size, anchorName);
    close_table_row();
}
void ProducerConsumerHierarchy::close_if_tree() {
    close_table();

    html << "</span>";
    html << "</li>";
}

void ProducerConsumerHierarchy::open_table_row() {
    html << "<tr>";
}
void ProducerConsumerHierarchy::close_table_row() {
    html << "</tr>";
}

void ProducerConsumerHierarchy::open_table_data(string colSpan = "3") {
    html << "<td colSpan=\\'" << colSpan << "\\'>";
}
void ProducerConsumerHierarchy::close_table_data() {
    html << "</td>";
}

void ProducerConsumerHierarchy::open_span(string className) {
    html << "<span class=\\'" << className << "\\'>";
}
void ProducerConsumerHierarchy::close_span() {
    html << "</span>";
}
void ProducerConsumerHierarchy::cost_color_spacer() {
    open_span("CostColorSpacer");
    html << ".";
    close_span();
}
void ProducerConsumerHierarchy::cost_colors(const IRNode *op) {
    cost_color_spacer();

    int computation_range = findStmtCost.get_computation_range(op);
    open_span("CostComputation" + to_string(computation_range));
    html << ".";
    close_span();

    cost_color_spacer();

    int data_movement_range = findStmtCost.get_data_movement_range(op);
    open_span("CostMovement" + to_string(data_movement_range));
    html << ".";
    close_span();

    open_span("CostColorSpacer");
    html << ".";
    close_span();
}

Stmt ProducerConsumerHierarchy::visit(const ProducerConsumer *op) {
    open_table(op->is_producer ? PRODUCER_COLOR : CONSUMER_COLOR);

    producerConsumerCount++;
    string anchorName = "producerConsumer" + std::to_string(producerConsumerCount);

    stringstream header;
    header << (op->is_producer ? "Produce" : "Consume");
    header << " " << op->name;
    StmtSize size = pre_processor.get_size(op);

    open_table_row();
    if (!SHOW_CUMULATIVE_COST) {
        size = StmtSize();
    }
    table_header(op, header.str(), size, anchorName);
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
    open_table(FOR_COLOR);

    forCount++;
    string anchorName = "for" + std::to_string(forCount);

    StmtSize size = pre_processor.get_size(op);

    stringstream header;
    header << "For (" << op->name << ")";

    open_table_row();
    if (SHOW_CUMULATIVE_COST) {
        table_header(op, header.str(), size, anchorName);
    } else {
        for_loop_table_header(op, header.str(), size, anchorName);
    }
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
    StmtSize thenSize = pre_processor.get_size(op->then_case.get());
    StmtSize elseSize = pre_processor.get_size(op->else_case.get());

    // only start the if-tree if either case is not empty
    // (aka won't print if both cases are empty)
    // (we can't just exit early though because we have to go through all if-stmts to
    //  get accurate count for the anchor names)
    if (!thenSize.empty() || !elseSize.empty()) {
        // open main if tree
        html << "<div class=\\'tf-tree tf-gap-sm tf-custom\\' style=\\'font-size: 12px; "
                "display: flex; justify-content: center;\\'>";
        html << "<ul>";
        html << "<li><span class=\\'tf-nc if-node\\'>";
        html << "If";
        html << "</span>";
        html << "<ul>";
    }

    stringstream ifHeader;
    ifHeader << "if ";

    while (true) {

        // anchor name
        ifCount++;
        string anchorName = "if" + std::to_string(ifCount);

        thenSize = pre_processor.get_size(op->then_case.get());

        if (!thenSize.empty()) {
            // TODO: inline condition
            ifHeader << "(" << op->condition << ")";
            if (!SHOW_CUMULATIVE_COST) {
                thenSize = StmtSize();
            }
            if_tree(op->then_case.get(), ifHeader.str(), thenSize, anchorName);

            // then body
            open_table_row();
            open_table_data();
            mutate(op->then_case);
            close_table_data();
            close_table_row();

            close_if_tree();
        }

        // if there is no else case, we are done
        if (!op->else_case.defined()) {
            break;
        }

        // if else case is another ifthenelse, recurse and reset op to else case
        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            op = nested_if;
            ifHeader.str("");
            ifHeader << "else if ";
        }

        // if else case is not another ifthenelse
        else {
            elseSize = pre_processor.get_size(op->else_case.get());

            if (!elseSize.empty()) {
                stringstream elseHeader;
                elseHeader << "else ";
                if (!SHOW_CUMULATIVE_COST) {
                    elseSize = StmtSize();
                }
                if_tree(op->else_case.get(), elseHeader.str(), elseSize);

                open_table_row();
                open_table_data();
                mutate(op->else_case);
                close_table_data();
                close_table_row();

                close_if_tree();
            }
            break;
        }
    }

    // close main if tree
    html << "</ul>";
    html << "</li>";
    html << "</ul>";
    html << "</div>";

    return op;
}

Stmt ProducerConsumerHierarchy::visit(const Store *op) {
    StmtSize size = pre_processor.get_size(op);

    storeCount++;
    string anchorName = "store" + std::to_string(storeCount);

    stringstream header;
    header << "Store " << op->name;

    open_table(STORE_COLOR);

    open_table_row();
    table_header(op, header.str(), size, anchorName);
    close_table_row();

    mutate(op->value);

    // spacing
    open_table_row();
    open_table_data();
    html << "<br>";
    close_table_data();
    close_table_row();

    close_table();

    return op;
}
Expr ProducerConsumerHierarchy::visit(const Load *op) {
    int lanes;

    // TODO: make sure this is right
    if (op->index.as<Ramp>()) {
        lanes = op->index.as<Ramp>()->lanes;
    } else {
        lanes = int(op->type.lanes());
    }

    stringstream header;
    header << "Load " << op->name << " (" << lanes << ")";

    open_table_row();
    open_table_data();
    html << "&nbsp;";
    html << header.str();
    html << "&nbsp;";
    close_table_data();
    close_table_row();

    return op;
}

Stmt ProducerConsumerHierarchy::visit(const Allocate *op) {
    open_table(ALLOCATE_COLOR);

    allocateCount++;
    string anchorName = "allocate" + std::to_string(allocateCount);

    StmtSize size = pre_processor.get_size(op);

    stringstream header;
    header << "Allocate " << op->name;

    // TODO: make sure this is right
    if (!is_const_one(op->condition)) {
        header << " if " << op->condition;
    }
    if (op->new_expr.defined()) {
        internal_error << "\n"
                       << "ProducerConsumerHierarchy: Allocate " << op->name
                       << " `op->new_expr.defined()` is not supported.\n\n";
    }
    if (!op->free_function.empty()) {
        header << " custom_delete {" << op->free_function << "}";
    }

    open_table_row();
    allocate_table_header(op, header.str(), size, anchorName);
    close_table_row();

    // spacing
    open_table_row();
    open_table_data();
    html << "<br>";
    close_table_data();
    close_table_row();

    close_table();

    mutate(op->body);

    return op;
}

/*
 * PRINT NODE
 */
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
