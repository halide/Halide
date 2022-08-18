#include "ProducerConsumerHierarchy.h"
#include "IROperator.h"
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
    return "<span class='stringType'>" + varName + "</span>";
}
string StmtSizes::int_span(int64_t intVal) const {
    return "<span class='intType'>" + to_string(intVal) + "</span>";
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
    curr_loads.clear();
    mutate(op->value);

    // set consume (for now, read values)
    for (const auto &load_var : curr_load_values) {
        set_consume_size(op, load_var.first, int_span(load_var.second));
    }

    // iterate through curr_loads to get unique loads
    for (const auto &load_var : curr_loads) {
        string vectorName = load_var.first;
        vector<set<int>> loadValues = load_var.second;
        set<int> finalLoadValuesUnique;

        for (const set<int> &loadValueSet : loadValues) {
            finalLoadValuesUnique.insert(loadValueSet.begin(), loadValueSet.end());
        }

        if (SHOW_UNIQUE_LOADS) {
            set_consume_size(op, vectorName + "_unique", int_span(finalLoadValuesUnique.size()));
        }
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
void StmtSizes::add_load_value_unique_loads(const string &name, set<int> &load_values) {
    curr_loads[name].push_back(load_values);
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

            // calculate unique loads if all args are concrete
            if (op->index.as<Ramp>()->base.as<IntImm>() &&
                op->index.as<Ramp>()->stride.as<IntImm>()) {
                int64_t baseValue = op->index.as<Ramp>()->base.as<IntImm>()->value;
                int64_t strideValue = op->index.as<Ramp>()->stride.as<IntImm>()->value;

                set<int> load_values;
                for (int i = baseValue; i < baseValue + (lanes * strideValue); i += strideValue) {
                    load_values.insert(i);
                }

                if (SHOW_UNIQUE_LOADS) {
                    add_load_value_unique_loads(op->name, load_values);
                }
            }
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
    type << "<span class='stringType'>" << op->type << "</span>";
    set_allocation_size(op, type.str());

    for (const auto &extent : op->extents) {
        // TODO: inline these as well if they are variables
        stringstream ss;
        if (extent.as<IntImm>()) {
            ss << "<span class='intType'>" << extent << "</span>";
        } else {
            ss << "<span class='stringType'>" << extent << "</span>";
        }

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

    html.str(string());
    traverse(m);

    return html.str();
}
string ProducerConsumerHierarchy::generate_producer_consumer_html(const Stmt &stmt) {
    pre_processor.generate_sizes(stmt);

    html.str(string());
    mutate(stmt);

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

void ProducerConsumerHierarchy::open_table(string backgroundColor) {
    html << "<br>";
    html << "<table style='";
    html << "background-color: " << backgroundColor << "; ";
    html << "' ";
    html << "class='center'";
    html << ">";
}
void ProducerConsumerHierarchy::close_table() {
    html << "</table>";
    html << "<br>";
}

void ProducerConsumerHierarchy::table_header(const IRNode *op, const string &header, StmtSize &size,
                                             string anchorName = "") {
    html << "<th>";

    // add cost color squares if op exists and if it's not a store node
    if (op != nullptr && op->node_type != IRNodeType::Store) {
        cost_colors(op);
    }

    // add anchor button if anchorName is provided
    if (anchorName != "") {
        see_code_button(anchorName);
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
    html << "<table class='costTable' style='background-color: rgba(150, 150, 150, 0.5)'>";

    // Prod | Cons
    html << "<tr>";

    html << "<th colspan='2' class='costTableHeader middleCol'>";
    html << "Written";
    html << "</th>";

    html << "<th colspan='2' class='costTableHeader'>";
    html << "Read";
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
            ss << "<td class='costTableData'>";
            ss << produce_var.first << ": ";
            ss << "</td>";

            ss << "<td class='costTableData middleCol'>";
            ss << produce_var.second;
            ss << "</td>";

            rows.push_back(ss.str());
        }

        // fill in consumer variables
        unsigned long rowNum = 0;
        for (const auto &consume_var : size.consumes) {
            stringstream ss;
            ss << "<td class='costTableData'>";
            ss << consume_var.first << ": ";
            ss << "</td>";

            ss << "<td class='costTableData'>";
            ss << consume_var.second;
            ss << "</td>";

            if (rowNum < rows.size()) {
                rows[rowNum] += ss.str();
            } else {
                // pad row with empty cells for produce
                stringstream sEmpty;
                sEmpty << "<td colspan='2' class='costTableData middleCol'>";
                sEmpty << "</td>";

                rows.push_back(sEmpty.str() + ss.str());
            }
            rowNum++;
        }

        // pad row with empty calls for consume
        rowNum = size.consumes.size();
        while (rowNum < size.produces.size()) {
            stringstream sEmpty;
            sEmpty << "<td class='costTableData'>";
            sEmpty << "</td>";
            sEmpty << "<td class='costTableData'>";
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
    see_code_button(anchorName);

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

    allocate_table(allocationSizes);

    html << "<br><br>";
    html << "</th>";

    // spacing
    html << "<th>";
    html << "&nbsp;";
    html << "</th>";
}
void ProducerConsumerHierarchy::allocate_table(vector<string> &allocationSizes) {
    // open table
    html << "<table class='costTable' style='background-color: rgba(150, 150, 150, 0.5)'>";

    stringstream header;
    stringstream data;

    // open header and data rows
    header << "<tr>";
    data << "<tr>";

    // iterate through all allocation sizes and add them to the header and data rows
    for (unsigned long i = 0; i < allocationSizes.size(); i++) {
        if (i == 0) {
            header << "<th class='costTableHeader middleCol'>";
            header << "Type";
            header << "</th>";

            data << "<td class='costTableHeader middleCol'>";
            data << allocationSizes[0];
            data << "</td>";
        } else {
            if (i < allocationSizes.size() - 1) {
                header << "<th class='costTableHeader middleCol'>";
                data << "<td class='costTableHeader middleCol'>";
            } else {
                header << "<th class='costTableHeader'>";
                data << "<td class='costTableHeader'>";
            }
            header << "Dim-" << i;
            header << "</th>";

            data << allocationSizes[i];
            data << "</td>";
        }
    }

    // close header and data rows
    header << "</tr>";
    data << "</tr>";

    // add header and data rows to html
    html << header.str();
    html << data.str();

    // close table
    html << "</table>";
}

void ProducerConsumerHierarchy::for_loop_table_header(const For *op, const string &header,
                                                      StmtSize &size, string anchorName) {
    html << "<th>";

    // add cost color squares
    cost_colors(op);

    // add anchor button
    see_code_button(anchorName);

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
    html << "<table class='costTable' style='background-color: rgba(150, 150, 150, 0.5)'>";

    // Loop Size
    html << "<tr>";

    html << "<th class='costTableHeader middleCol'>";
    html << "Loop Size";
    html << "</th>";

    html << "</tr>";

    html << "<tr>";

    // loop size
    html << "<td class='costTableData'>";
    html << loop_size;
    html << "</td>";

    html << "</tr>";

    // close table
    html << "</table>";
}

void ProducerConsumerHierarchy::if_tree(const IRNode *op, const string &header, StmtSize &size,
                                        string anchorName = "") {
    html << "<li>";
    html << "<span class='tf-nc'>";

    // open table
    html << "<br>";
    html << "<table style='";
    html << "' ";
    html << "class='center ifElseTable'";
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

void ProducerConsumerHierarchy::see_code_button(string anchorName) {
    html << "<button class='see-code-button' onclick='";
    html << "window.open(\"" << output_file_name << "#" << anchorName << "\", \"_blank\")";
    html << "'>";
    html << "<i class='bi bi-code-square'></i>";
    html << "</button>";
}

void ProducerConsumerHierarchy::open_table_row() {
    html << "<tr>";
}
void ProducerConsumerHierarchy::close_table_row() {
    html << "</tr>";
}

void ProducerConsumerHierarchy::open_table_data(string colSpan = "3") {
    html << "<td colSpan='" << colSpan << "'>";
}
void ProducerConsumerHierarchy::close_table_data() {
    html << "</td>";
}

void ProducerConsumerHierarchy::open_span(string className) {
    html << "<span class='" << className << "'>";
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
    bool opened = false;
    if (!thenSize.empty() || !elseSize.empty()) {
        // open main if tree
        html << "<div class='tf-tree tf-gap-sm tf-custom' style='font-size: 12px; "
                "display: flex; justify-content: center;'>";
        html << "<ul>";
        html << "<li><span class='tf-nc if-node'>";
        html << "If";
        html << "</span>";
        html << "<ul>";
        opened = true;
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

            // TODO: for condition, show it if it's small, and add hover text if it's large

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
    if (opened) {
        html << "</ul>";
        html << "</li>";
        html << "</ul>";
        html << "</div>";
    }
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
    header << "Load " << op->name << " (";
    if (findStmtCost.is_local_variable(op->name)) {
        header << "local var, load size: ";
    } else {
        header << "global var, load size: ";
    }

    header << lanes << ")";

    open_table_row();
    open_table_data();
    html << "&nbsp;";
    cost_colors(op);
    html << header.str();
    html << "&nbsp;";
    close_table_data();
    close_table_row();

    return op;
}

string get_memory_type(MemoryType memType) {
    if (memType == MemoryType::Auto) {
        return "Auto";
    } else if (memType == MemoryType::Heap) {
        return "Heap";
    } else if (memType == MemoryType::Stack) {
        return "Stack";
    } else if (memType == MemoryType::Register) {
        return "Register";
    } else if (memType == MemoryType::GPUShared) {
        return "GPUShared";
    } else if (memType == MemoryType::GPUTexture) {
        return "GPUTexture";
    } else if (memType == MemoryType::LockedCache) {
        return "LockedCache";
    } else if (memType == MemoryType::VTCM) {
        return "VTCM";
    } else if (memType == MemoryType::AMXTile) {
        return "AMXTile";
    } else {
        internal_error << "\n\n"
                       << "Unknown memory type"
                       << "\n";
        return "Unknown Memory Type";
    }
}
Stmt ProducerConsumerHierarchy::visit(const Allocate *op) {
    open_table(ALLOCATE_COLOR);

    allocateCount++;
    string anchorName = "allocate" + std::to_string(allocateCount);

    StmtSize size = pre_processor.get_size(op);

    stringstream header;
    header << "Allocate " << op->name;

    // TODO: add information about memory_type to header
    header << " (" << get_memory_type(op->memory_type) << ")";

    // TODO: make sure this is right
    if (!is_const_one(op->condition)) {
        // internal_error << "\n"
        //                << "ProducerConsumerHierarchy: Allocate " << op->name
        //                << " `!is_const_one(op->condition)` is not supported.\n\n";
        header << " if " << op->condition;
    }
    if (op->new_expr.defined()) {
        internal_error << "\n"
                       << "ProducerConsumerHierarchy: Allocate " << op->name
                       << " `op->new_expr.defined()` is not supported.\n\n";
    }
    if (!op->free_function.empty()) {
        internal_error << "\n"
                       << "ProducerConsumerHierarchy: Allocate " << op->name
                       << " `!op->free_function.empty()` is not supported.\n\n";
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
        auto node1 = dynamic_cast<const Add *>(node);
        s << "a: " << print_node(node1->a.get()) << endl;
        s << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Sub) {
        s << "Sub type" << endl;
        auto node1 = dynamic_cast<const Sub *>(node);
        s << "a: " << print_node(node1->a.get()) << endl;
        s << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Mod) {
        s << "Mod type" << endl;
        auto node1 = dynamic_cast<const Mod *>(node);
        s << "a: " << print_node(node1->a.get()) << endl;
        s << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Mul) {
        s << "Mul type" << endl;
        auto node1 = dynamic_cast<const Mul *>(node);
        s << "a: " << print_node(node1->a.get()) << endl;
        s << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Div) {
        s << "Div type" << endl;
        auto node1 = dynamic_cast<const Div *>(node);
        s << "a: " << print_node(node1->a.get()) << endl;
        s << "b: " << print_node(node1->b.get()) << endl;
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
