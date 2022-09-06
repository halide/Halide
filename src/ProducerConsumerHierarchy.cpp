#include "ProducerConsumerHierarchy.h"
#include "IROperator.h"
#include "Module.h"

using namespace std;
using namespace Halide;
using namespace Internal;

template<typename T>
string to_string(T value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

/*
 * StmtSizes class
 */

void StmtSizes::generate_sizes(const Module &m) {
    traverse(m);
}
void StmtSizes::generate_sizes(const Stmt &stmt) {
    stmt.accept(this);
}

StmtSize StmtSizes::get_size(const IRNode *node) const {
    auto it = stmt_sizes.find(node);

    // return empty size if not found (this just means it wasn't set)
    if (it == stmt_sizes.end()) {
        return StmtSize();
    }

    return it->second;
}

string StmtSizes::string_span(string varName) const {
    return "<span class='stringType'>" + varName + "</span>";
}
string StmtSizes::int_span(int64_t intVal) const {
    return "<span class='intType'>" + to_string(intVal) + "</span>";
}

void StmtSizes::traverse(const Module &m) {

    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }

    // traverse all functions
    for (const auto &f : m.functions()) {
        function_names.push_back(f.name);
        f.body.accept(this);
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
void StmtSizes::visit(const Store *op) {

    // TODO: is this correct? should i be getting it from `index`?
    uint16_t lanes = op->index.type().lanes();

    set_produce_size(op, op->name, int_span(lanes));

    // empty curr_load_values
    curr_load_values.clear();
    op->value.accept(this);

    // set consume (for now, read values)
    for (const auto &load_var : curr_load_values) {
        set_consume_size(op, load_var.first, int_span(load_var.second));
    }
}
void StmtSizes::add_load_value(const string &name, const int lanes) {
    auto it = curr_load_values.find(name);
    if (it == curr_load_values.end()) {
        curr_load_values[name] = lanes;
    } else {
        curr_load_values[name] += lanes;
    }
}
void StmtSizes::visit(const Load *op) {

    // TODO: make sure this logic is right
    int lanes = int(op->type.lanes());

    add_load_value(op->name, lanes);
}

/*
 * ProducerConsumerHierarchy class
 */
string ProducerConsumerHierarchy::generate_producer_consumer_html(const Module &m) {
    pre_processor.generate_sizes(m);

    html = "";
    startModuleTraversal(m);

    return html.c_str();
}
string ProducerConsumerHierarchy::generate_producer_consumer_html(const Stmt &stmt) {
    pre_processor.generate_sizes(stmt);

    html = "";
    stmt.accept(this);

    return html.c_str();
}

void ProducerConsumerHierarchy::startModuleTraversal(const Module &m) {

    // print main function first
    for (const auto &f : m.functions()) {
        if (f.name == m.name()) {
            visit_function(f);
        }
    }

    // print the rest of the functions
    for (const auto &f : m.functions()) {
        if (f.name != m.name()) {
            visit_function(f);
        }
    }
}

void ProducerConsumerHierarchy::open_box_div(string className, const IRNode *op) {
    html += "<div class='box center " + className + "'";
    html += ">";

    if (op != nullptr) {
        generate_computation_cost_div(op);
        generate_memory_cost_div(op);
    }

    open_content_div();
}
void ProducerConsumerHierarchy::close_box_div() {
    close_div();  // content div
    close_div();  // main box div
}
void ProducerConsumerHierarchy::open_header_div() {
    html += "<div class='boxHeader'>";
}
void ProducerConsumerHierarchy::open_box_header_title_div() {
    html += "<div class='boxHeaderTitle'>";
}
void ProducerConsumerHierarchy::open_box_header_table_div() {
    html += "<div class='boxHeaderTable'>";
}
void ProducerConsumerHierarchy::open_store_div() {
    html += "<div class='store'>";
}
void ProducerConsumerHierarchy::close_div() {
    html += "</div>";
}

void ProducerConsumerHierarchy::open_header(const string &header, string anchorName) {
    open_header_div();

    open_box_header_title_div();

    html += "<span id='" + anchorName + "_viz'>";
    html += header;
    html += "</span>";

    close_div();

    // spacing purposes
    html += "<div class='spacing'></div>";

    open_box_header_table_div();
}
void ProducerConsumerHierarchy::close_header(string anchorName) {

    close_div();  // header table div
    see_code_button_div(anchorName);
    close_div();  // header div
}
void ProducerConsumerHierarchy::div_header(const string &header, StmtSize *size,
                                           string anchorName) {

    open_header(header, anchorName);

    // add producer consumer size if size is provided
    if (size != nullptr) {
        prod_cons_table(*size);
    }

    close_header(anchorName);
}
vector<string> ProducerConsumerHierarchy::get_allocation_sizes(const Allocate *op) const {
    vector<string> sizes;

    string type;
    type += "<span class='stringType'>" + to_string(op->type) + "</span>";
    sizes.push_back(type);

    for (const auto &extent : op->extents) {
        string ss;
        if (extent.as<IntImm>()) {
            ss += "<span class='intType'>" + to_string(extent) + "</span>";
        } else {
            ss += "<span class='stringType'>" + to_string(extent) + "</span>";
        }

        sizes.push_back(ss);
    }

    internal_assert(sizes.size() == op->extents.size() + 1);

    return sizes;
}
void ProducerConsumerHierarchy::allocate_div_header(const Allocate *op, const string &header,
                                                    string anchorName) {
    open_header(header, anchorName);

    vector<string> allocationSizes = get_allocation_sizes(op);
    allocate_table(allocationSizes);

    close_header(anchorName);
}
void ProducerConsumerHierarchy::for_loop_div_header(const For *op, const string &header,
                                                    string anchorName) {
    open_header(header, anchorName);

    string loopSize = get_loop_iterator(op);
    for_loop_table(loopSize);

    close_header(anchorName);
}

void ProducerConsumerHierarchy::if_tree(const IRNode *op, const string &header, string anchorName) {
    html += "<li>";
    html += "<span class='tf-nc if-node'>";

    open_box_div("IfBox", op);
    div_header(header, nullptr, anchorName);
}
void ProducerConsumerHierarchy::close_if_tree() {
    close_box_div();
    html += "</span>";
    html += "</li>";
}

void ProducerConsumerHierarchy::prod_cons_table(StmtSize &size) {
    // open table
    html += "<table class='costTable'>";

    // Prod | Cons
    html += "<tr>";

    html += "<th colspan='2' class='costTableHeader middleCol'>";
    html += "Written";
    html += "</th>";

    html += "<th colspan='2' class='costTableHeader'>";
    html += "Read";
    html += "</th>";

    html += "</tr>";

    // produces and consumes are empty
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
            string ss;
            ss += "<td class='costTableData'>";
            ss += produce_var.first + ": ";
            ss += "</td>";

            ss += "<td class='costTableData middleCol'>";
            ss += produce_var.second;
            ss += "</td>";

            rows.push_back(ss);
        }

        // fill in consumer variables
        unsigned long rowNum = 0;
        for (const auto &consume_var : size.consumes) {
            string ss;
            ss += "<td class='costTableData'>";
            ss += consume_var.first + ": ";
            ss += "</td>";

            ss += "<td class='costTableData'>";
            ss += consume_var.second;
            ss += "</td>";

            if (rowNum < rows.size()) {
                rows[rowNum] += ss;
            } else {
                // pad row with empty cells for produce
                string sEmpty;
                sEmpty += "<td colspan='2' class='costTableData middleCol'>";
                sEmpty += "</td>";

                rows.push_back(sEmpty + ss);
            }
            rowNum++;
        }

        // pad row with empty calls for consume
        rowNum = size.consumes.size();
        while (rowNum < size.produces.size()) {
            string sEmpty;
            sEmpty += "<td class='costTableData'>";
            sEmpty += "</td>";
            sEmpty += "<td class='costTableData'>";
            sEmpty += "</td>";

            rows[rowNum] += sEmpty;
            rowNum++;
        }

        // add rows to html
        for (const auto &row : rows) {
            html += "<tr>";
            html += row;
            html += "</tr>";
        }
    }

    // close table
    html += "</table>";
}
void ProducerConsumerHierarchy::allocate_table(vector<string> &allocationSizes) {
    // open table
    html += "<table class='costTable'>";

    // open header and data rows
    string header = "<tr>";
    string data = "<tr>";

    // iterate through all allocation sizes and add them to the header and data rows
    for (unsigned long i = 0; i < allocationSizes.size(); i++) {
        if (i == 0) {
            header += "<th class='costTableHeader middleCol'>";
            header += "Type";
            header += "</th>";

            data += "<td class='costTableHeader middleCol'>";
            data += allocationSizes[0];
            data += "</td>";
        } else {
            if (i < allocationSizes.size() - 1) {
                header += "<th class='costTableHeader middleCol'>";
                data += "<td class='costTableHeader middleCol'>";
            } else {
                header += "<th class='costTableHeader'>";
                data += "<td class='costTableHeader'>";
            }
            header += "Dim-" + std::to_string(i);
            header += "</th>";

            data += allocationSizes[i];
            data += "</td>";
        }
    }

    // close header and data rows
    header += "</tr>";
    data += "</tr>";

    // add header and data rows to html
    html += header;
    html += data;

    // close table
    html += "</table>";
}
void ProducerConsumerHierarchy::for_loop_table(string loop_size) {
    // open table
    html += "<table class='costTable'>";

    // Loop Size
    html += "<tr>";

    html += "<th class='costTableHeader'>";
    html += "Loop Span";
    html += "</th>";

    html += "</tr>";

    html += "<tr>";

    // loop size
    html += "<td class='costTableData'>";
    html += loop_size;
    html += "</td>";

    html += "</tr>";

    // close table
    html += "</table>";
}

void ProducerConsumerHierarchy::see_code_button_div(string anchorName) {
    html += "<div>";
    html += "<button class='icon-button'";
    html += "onclick='scrollToFunctionVizToCode(\"" + anchorName + "\")'>";
    html += "<i class='bi bi-code-square'></i>";
    html += "</button>";
    html += "</div>";
}

string ProducerConsumerHierarchy::info_tooltip(string toolTipText, string className = "") {

    string ss;

    // info-button
    prodConsTooltipCount++;
    ss += "<button id='prodConsButton" + std::to_string(prodConsTooltipCount) + "' ";
    ss += "aria-describedby='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "' ";
    ss += "class='info-button' role='button' ";
    ss += ">";
    ss += "<i class='bi bi-info'></i>";
    ss += "</button>";

    // tooltip span
    ss += "<span id='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "' ";
    ss += "class='tooltip";
    if (className != "") {
        ss += " " + className;
    }
    ss += "'";
    ss += "role='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "'>";
    ss += toolTipText;
    ss += "</span>";

    return ss;
}

void ProducerConsumerHierarchy::generate_computation_cost_div(const IRNode *op) {
    // skip if it's a store
    if (op->node_type == IRNodeType::Store) return;

    prodConsTooltipCount++;

    string tooltipText = findStmtCost.generate_computation_cost_tooltip(op, true, "");

    // tooltip span
    html += "<span id='prodConsTooltip" + std::to_string(prodConsTooltipCount) +
            "' class='tooltip CostTooltip' ";
    html += "role='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "'>";
    html += tooltipText;
    html += "</span>";

    int computation_range = findStmtCost.get_computation_color_range(op, true);
    string className = "computation-cost-div CostColor" + to_string(computation_range);
    html += "<div id='prodConsButton" + std::to_string(prodConsTooltipCount) + "' ";
    html += "aria-describedby='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "' ";
    html += "class='" + className + "'>";

    close_div();
}
void ProducerConsumerHierarchy::generate_memory_cost_div(const IRNode *op) {
    // skip if it's a store
    if (op->node_type == IRNodeType::Store) return;

    prodConsTooltipCount++;

    string tooltipText = findStmtCost.generate_data_movement_cost_tooltip(op, true, "");

    // tooltip span
    html += "<span id='prodConsTooltip" + std::to_string(prodConsTooltipCount) +
            "' class='tooltip CostTooltip' ";
    html += "role='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "'>";
    html += tooltipText;
    html += "</span>";

    int data_movement_range = findStmtCost.get_data_movement_color_range(op, true);
    string className = "memory-cost-div CostColor" + to_string(data_movement_range);
    html += "<div id='prodConsButton" + std::to_string(prodConsTooltipCount) + "' ";
    html += "aria-describedby='prodConsTooltip" + std::to_string(prodConsTooltipCount) + "' ";
    html += "class='" + className + "'>";

    close_div();
}
void ProducerConsumerHierarchy::open_content_div() {
    html += "<div class='content'>";
}

string ProducerConsumerHierarchy::color_button(int colorRange) {
    stringstream s;

    prodConsTooltipCount++;
    s << "<button id='prodConsButton" << prodConsTooltipCount << "' ";
    s << "aria-describedby='prodConsTooltip" << prodConsTooltipCount << "' ";
    s << "class='prodConsColorButton CostColor" + to_string(colorRange) + "' role='button' ";
    s << ">";
    s << "</button>";

    return s.str();
}

string ProducerConsumerHierarchy::computation_div(const IRNode *op) {
    // want exclusive cost (so that the colors match up with exclusive costs)
    int computation_range = findStmtCost.get_computation_color_range(op, false);

    stringstream s;
    s << color_button(computation_range);

    string tooltipText = findStmtCost.generate_computation_cost_tooltip(op, false, "");

    // tooltip span
    s << "<span id='prodConsTooltip" << prodConsTooltipCount << "' class='tooltip CostTooltip' ";
    s << "role='prodConsTooltip" << prodConsTooltipCount << "'>";
    s << tooltipText;
    s << "</span>";

    return s.str();
}
string ProducerConsumerHierarchy::data_movement_div(const IRNode *op) {
    // want exclusive cost (so that the colors match up with exclusive costs)
    int data_movement_range = findStmtCost.get_data_movement_color_range(op, false);

    stringstream s;
    s << color_button(data_movement_range);

    string tooltipText = findStmtCost.generate_data_movement_cost_tooltip(op, false, "");

    // tooltip span
    s << "<span id='prodConsTooltip" << prodConsTooltipCount << "' class='tooltip CostTooltip' ";
    s << "role='prodConsTooltip" << prodConsTooltipCount << "'>";
    s << tooltipText;
    s << "</span>";

    return s.str();
}
string ProducerConsumerHierarchy::tooltip_table(map<string, string> &table) {
    stringstream s;
    s << "<table class='tooltipTable'>";
    for (auto &row : table) {
        s << "<tr>";
        s << "<td class = 'left-table'>" << row.first << "</td>";
        s << "<td class = 'right-table'> " << row.second << "</td>";
        s << "</tr>";
    }
    s << "</table>";
    return s.str();
}
void ProducerConsumerHierarchy::cost_colors(const IRNode *op) {
    html += computation_div(op);
    html += data_movement_div(op);
}

void ProducerConsumerHierarchy::visit_function(const LoweredFunc &func) {
    open_box_div("FunctionBox", nullptr);

    functionCount++;
    string anchorName = "loweredFunc" + to_string(functionCount);

    string header = "<span id='" + func.name + "'><h4>" + "Func: " + func.name + "</h4></span>";

    div_header(header, nullptr, anchorName);

    html += "<div class='functionViz'>";
    func.body.accept(this);
    html += "</div>";

    close_box_div();
}
void ProducerConsumerHierarchy::visit(const Variable *op) {
    // if op->name starts with "::", remove "::"
    string varName = op->name;
    if (varName[0] == ':' && varName[1] == ':') {
        varName = varName.substr(2);
    }

    // see if varName is in pre_processor.function_names
    if (std::count(pre_processor.function_names.begin(), pre_processor.function_names.end(),
                   varName)) {

        html += "<div class='box center FunctionCallBox'>";

        html += "Function Call";
        html += "<button class='function-scroll-button' role='button' ";
        html += "onclick='scrollToFunctionCodeToViz(\"" + varName + "\")'>";

        html += varName;
        html += "</button>";

        html += "</div>";
    }
}
void ProducerConsumerHierarchy::visit(const ProducerConsumer *op) {
    open_box_div("ProducerConsumerBox", op);

    producerConsumerCount++;
    string anchorName = "producerConsumer" + std::to_string(producerConsumerCount);

    string header = (op->is_producer ? "Produce" : "Consume");
    header += " " + op->name;

    div_header(header, nullptr, anchorName);

    op->body.accept(this);

    close_box_div();
}
string ProducerConsumerHierarchy::get_loop_iterator(const For *op) const {
    Expr min = op->min;
    Expr extent = op->extent;

    string loopIterator;

    // check if min and extend are of type IntImm
    if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::IntImm) {
        int64_t minValue = min.as<IntImm>()->value;
        int64_t extentValue = extent.as<IntImm>()->value;
        uint16_t range = uint16_t(extentValue - minValue);
        loopIterator = pre_processor.int_span(range);
    }

    else if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::Variable) {
        int64_t minValue = min.as<IntImm>()->value;
        string minName = pre_processor.int_span(minValue);
        string extentName = pre_processor.string_span(extent.as<Variable>()->name);

        // TODO: inline variable for extentName
        if (minValue == 0) {
            loopIterator = extentName;
        } else {
            loopIterator = "(" + extentName + " - " + minName + ")";
        }
    }

    else if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::Add) {
        int64_t minValue = min.as<IntImm>()->value;
        string minName = pre_processor.int_span(minValue);
        string extentName = "(";

        // deal with a
        if (extent.as<Add>()->a.node_type() == IRNodeType::IntImm) {
            int64_t extentValue = extent.as<Add>()->a.as<IntImm>()->value;
            extentName += pre_processor.int_span(extentValue);
        } else if (extent.as<Add>()->a.node_type() == IRNodeType::Variable) {
            extentName += pre_processor.string_span(extent.as<Add>()->a.as<Variable>()->name);
        } else {
            internal_error << "\n"
                           << "In for loop: " << op->name << "\n"
                           << pre_processor.print_node(extent.as<Add>()->a.get()) << "\n"
                           << "StmtSizes::visit(const For *op): add->a isn't IntImm or Variable - "
                              "can't generate ProdCons hierarchy yet. \n\n";
        }

        extentName += "+";

        // deal with b
        if (extent.as<Add>()->b.node_type() == IRNodeType::IntImm) {
            int64_t extentValue = extent.as<Add>()->b.as<IntImm>()->value;
            extentName += pre_processor.int_span(extentValue);
        } else if (extent.as<Add>()->b.node_type() == IRNodeType::Variable) {
            extentName += pre_processor.string_span(extent.as<Add>()->b.as<Variable>()->name);
        } else {
            internal_error << "\n"
                           << "In for loop: " << op->name << "\n"
                           << pre_processor.print_node(extent.as<Add>()->b.get()) << "\n"
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
            << pre_processor.print_node(op->min.get()) << "\n"
            << pre_processor.print_node(op->extent.get()) << "\n"
            << "StmtSizes::visit(const For *op): min and extent are not of type (IntImm) "
               "or (IntImm & Variable) or (IntImm & Add) - "
               "can't generate ProdCons hierarchy yet. \n\n";
    }

    return loopIterator;
}

void ProducerConsumerHierarchy::visit(const For *op) {
    open_box_div("ForBox", op);

    forCount++;
    string anchorName = "for" + std::to_string(forCount);

    string header = "For (" + op->name + ")";

    for_loop_div_header(op, header, anchorName);

    op->body.accept(this);

    close_box_div();
}

void ProducerConsumerHierarchy::visit(const IfThenElse *op) {

    // open main if tree
    html += "<div class='tf-tree tf-gap-sm tf-custom-prodCons'>";
    html += "<ul>";
    html += "<li><span class='tf-nc if-node'>";
    html += "If";
    html += "</span>";
    html += "<ul>";

    string ifHeader;
    ifHeader += "if ";

    // anchor name
    ifCount++;
    string anchorName = "if" + std::to_string(ifCount);

    while (true) {

        // TODO: inline condition
        string condition;
        condition += to_string(op->condition);

        // make condition smaller if it's too big
        if (condition.size() > MAX_CONDITION_LENGTH) {
            condition = "";
            condition += "... ";
            condition += info_tooltip(to_string(op->condition), "conditionTooltip");
        }

        ifHeader += condition;

        if_tree(op->then_case.get(), ifHeader, anchorName);

        // then body
        op->then_case.accept(this);

        close_if_tree();

        // if there is no else case, we are done
        if (!op->else_case.defined()) {
            break;
        }

        // if else case is another ifthenelse, recurse and reset op to else case
        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            op = nested_if;
            ifHeader = "";
            ifHeader += "else if ";

            // anchor name
            ifCount++;
            anchorName = "if" + std::to_string(ifCount);

        }

        // if else case is not another ifthenelse
        else {

            string elseHeader;
            elseHeader += "else ";

            // anchor name
            ifCount++;
            anchorName = "if" + std::to_string(ifCount);

            if_tree(op->else_case.get(), elseHeader, anchorName);

            op->else_case.accept(this);

            close_if_tree();
            break;
        }
    }

    // close main if tree
    html += "</ul>";
    html += "</li>";
    html += "</ul>";
    html += "</div>";
}

void ProducerConsumerHierarchy::visit(const Store *op) {
    StmtSize size = pre_processor.get_size(op);

    storeCount++;
    string anchorName = "store" + std::to_string(storeCount);

    string header = "Store " + op->name;

    map<string, string> tableRows;

    tableRows["Vector Size"] = to_string(op->index.type().lanes());
    tableRows["Bit Size"] = to_string(op->index.type().bits());

    header += info_tooltip(tooltip_table(tableRows));

    open_box_div("StoreBox", op);

    div_header(header, &size, anchorName);

    op->value.accept(this);

    close_box_div();
}
void ProducerConsumerHierarchy::visit(const Load *op) {
    string header;

    if (op->type.is_scalar()) {
        header = "Scalar ";
    } else if (op->type.is_vector()) {
        header = "Vector ";
    } else {
        internal_error << "\n\nUnsupported type for Load: " << op->type << "\n\n";
    }

    header += "Load " + op->name;

    map<string, string> tableRows;

    if (findStmtCost.is_local_variable(op->name)) {
        tableRows["Variable Type"] = "local var";
    } else {
        tableRows["Variable Type"] = "global var";
    }

    tableRows["Bit Size"] = to_string(op->index.type().bits());
    tableRows["Vector Size"] = to_string(op->index.type().lanes());

    if (op->param.defined()) {
        tableRows["Parameter"] = op->param.name();
    }

    header += info_tooltip(tooltip_table(tableRows));

    open_store_div();
    cost_colors(op);
    html += header;
    close_div();
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
void ProducerConsumerHierarchy::visit(const Allocate *op) {
    open_box_div("AllocateBox", op);

    allocateCount++;
    string anchorName = "allocate" + std::to_string(allocateCount);

    string header = "Allocate " + op->name;

    map<string, string> tableRows;
    tableRows["Memory Type"] = get_memory_type(op->memory_type);

    if (!is_const_one(op->condition)) {
        tableRows["Condition"] = to_string(op->condition);
    }
    if (op->new_expr.defined()) {
        internal_error << "\n"
                       << "ProducerConsumerHierarchy: Allocate " << op->name
                       << " `op->new_expr.defined()` is not supported.\n\n";

        tableRows["New Expr"] = to_string(op->new_expr);
    }
    if (!op->free_function.empty()) {
        internal_error << "\n"
                       << "ProducerConsumerHierarchy: Allocate " << op->name
                       << " `!op->free_function.empty()` is not supported.\n\n";

        tableRows["Free Function"] = to_string(op->free_function);
    }

    tableRows["Bit Size"] = to_string(op->type.bits());
    tableRows["Vector Size"] = to_string(op->type.lanes());

    header += info_tooltip(tooltip_table(tableRows));

    allocate_div_header(op, header, anchorName);

    op->body.accept(this);

    close_box_div();
}

string ProducerConsumerHierarchy::generate_prodCons_js() {
    string prodConsJS;

    prodConsJS += "\n// prodCons JS\n";
    prodConsJS += "for (let i = 1; i <= " + std::to_string(prodConsTooltipCount) + "; i++) { \n";
    prodConsJS += "    const button = document.querySelector('#prodConsButton' + i); \n";
    prodConsJS += "    const tooltip = document.querySelector('#prodConsTooltip' + i); \n";
    prodConsJS += "    button.addEventListener('mouseenter', () => { \n";
    prodConsJS += "        showTooltip(button, tooltip); \n";
    prodConsJS += "    }); \n";
    prodConsJS += "    button.addEventListener('mouseleave', () => { \n";
    prodConsJS += "        hideTooltip(tooltip); \n";
    prodConsJS += "    } \n";
    prodConsJS += "    ); \n";
    prodConsJS += "    tooltip.addEventListener('focus', () => { \n";
    prodConsJS += "        showTooltip(button, tooltip); \n";
    prodConsJS += "    } \n";
    prodConsJS += "    ); \n";
    prodConsJS += "    tooltip.addEventListener('blur', () => { \n";
    prodConsJS += "        hideTooltip(tooltip); \n";
    prodConsJS += "    } \n";
    prodConsJS += "    ); \n";
    prodConsJS += "} \n";

    return prodConsJS;
}

/*
 * PRINT NODE
 */
string StmtSizes::print_node(const IRNode *node) const {
    stringstream s;
    s << "Node in question has type: ";
    IRNodeType type = node->node_type;
    if (type == IRNodeType::IntImm) {
        s << "IntImm type";
        auto node1 = dynamic_cast<const IntImm *>(node);
        s << "value: " << node1->value;
    } else if (type == IRNodeType::UIntImm) {
        s << "UIntImm type";
    } else if (type == IRNodeType::FloatImm) {
        s << "FloatImm type";
    } else if (type == IRNodeType::StringImm) {
        s << "StringImm type";
    } else if (type == IRNodeType::Broadcast) {
        s << "Broadcast type";
    } else if (type == IRNodeType::Cast) {
        s << "Cast type";
    } else if (type == IRNodeType::Variable) {
        s << "Variable type";
    } else if (type == IRNodeType::Add) {
        s << "Add type";
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
        s << "Min type";
    } else if (type == IRNodeType::Max) {
        s << "Max type";
    } else if (type == IRNodeType::EQ) {
        s << "EQ type";
    } else if (type == IRNodeType::NE) {
        s << "NE type";
    } else if (type == IRNodeType::LT) {
        s << "LT type";
    } else if (type == IRNodeType::LE) {
        s << "LE type";
    } else if (type == IRNodeType::GT) {
        s << "GT type";
    } else if (type == IRNodeType::GE) {
        s << "GE type";
    } else if (type == IRNodeType::And) {
        s << "And type";
    } else if (type == IRNodeType::Or) {
        s << "Or type";
    } else if (type == IRNodeType::Not) {
        s << "Not type";
    } else if (type == IRNodeType::Select) {
        s << "Select type";
    } else if (type == IRNodeType::Load) {
        s << "Load type";
    } else if (type == IRNodeType::Ramp) {
        s << "Ramp type";
    } else if (type == IRNodeType::Call) {
        s << "Call type";
    } else if (type == IRNodeType::Let) {
        s << "Let type";
    } else if (type == IRNodeType::Shuffle) {
        s << "Shuffle type";
    } else if (type == IRNodeType::VectorReduce) {
        s << "VectorReduce type";
    } else if (type == IRNodeType::LetStmt) {
        s << "LetStmt type";
    } else if (type == IRNodeType::AssertStmt) {
        s << "AssertStmt type";
    } else if (type == IRNodeType::ProducerConsumer) {
        s << "ProducerConsumer type";
    } else if (type == IRNodeType::For) {
        s << "For type";
    } else if (type == IRNodeType::Acquire) {
        s << "Acquire type";
    } else if (type == IRNodeType::Store) {
        s << "Store type";
    } else if (type == IRNodeType::Provide) {
        s << "Provide type";
    } else if (type == IRNodeType::Allocate) {
        s << "Allocate type";
    } else if (type == IRNodeType::Free) {
        s << "Free type";
    } else if (type == IRNodeType::Realize) {
        s << "Realize type";
    } else if (type == IRNodeType::Block) {
        s << "Block type";
    } else if (type == IRNodeType::Fork) {
        s << "Fork type";
    } else if (type == IRNodeType::IfThenElse) {
        s << "IfThenElse type";
    } else if (type == IRNodeType::Evaluate) {
        s << "Evaluate type";
    } else if (type == IRNodeType::Prefetch) {
        s << "Prefetch type";
    } else if (type == IRNodeType::Atomic) {
        s << "Atomic type";
    } else {
        s << "Unknown type";
    }

    return s.str();
}

const string ProducerConsumerHierarchy::scrollToFunctionJSVizToCode = "\n \
// scroll to function - viz to code\n \
function makeVisible(element) { \n \
    if (!element) return; \n \
    if (element.className == 'mainContent') return; \n \
    if (element.style.visibility == 'hidden') { \n \
        element.style = ''; \n \
        show = document.getElementById(element.id + '-show'); \n \
        hide = document.getElementById(element.id + '-hide'); \n \
        show.style.display = 'none'; \n \
        hide.style.display = 'block'; \n \
        return; \n \
    } \n \
    makeVisible(element.parentNode); \n \
} \n \
 \n \
function scrollToFunctionVizToCode(id) { \n \
    var container = document.getElementById('IRCode-code'); \n \
    var scrollToObject = document.getElementById(id); \n \
    makeVisible(scrollToObject); \n \
    container.scrollTo({ \n \
        top: scrollToObject.offsetTop - 10, \n \
        behavior: 'smooth' \n \
    }); \n \
    scrollToObject.style.backgroundColor = 'yellow'; \n \
    scrollToObject.style.fontSize = '20px'; \n \
 \n \
    // change content for 1 second   \n \
    setTimeout(function () { \n \
        scrollToObject.style.backgroundColor = 'transparent'; \n \
        scrollToObject.style.fontSize = '12px'; \n \
    }, 1000); \n \
} \n \
";

const string ProducerConsumerHierarchy::prodConsCSS = "\n \
/* ProdCons CSS */\n \
.tf-custom-prodCons .tf-nc { border-radius: 5px; border: 1px solid; }\n \
.tf-custom-prodCons .tf-nc:before, .tf-custom-prodCons .tf-nc:after { border-left-width: 1px; }\n \
.tf-custom-prodCons li li:before { border-top-width: 1px; }\n \
.tf-custom-prodCons .end-node { border-style: dashed; }\n \
.tf-custom-prodCons .tf-nc { background-color: #e6eeff; }\n \
.tf-custom-prodCons { font-size: 12px; } \n \
div.box { \n \
    border: 1px dashed grey; \n \
    border-radius: 5px; \n \
    margin: 5px; \n \
    padding: 5px; \n \
    display: flex; \n \
} \n \
div.boxHeader { \n \
    padding: 5px; \n \
    display: flex; \n \
} \n \
div.memory-cost-div, \n \
div.computation-cost-div { \n \
    border: 1px solid rgba(0, 0, 0, 0); \n \
     width: 7px; \n \
} \n \
div.FunctionCallBox { \n \
    background-color: #fabebe; \n \
} \n \
div.FunctionBox { \n \
    background-color: #f0f0f0; \n \
} \n \
div.ProducerConsumerBox { \n \
    background-color: #99bbff; \n \
} \n \
div.ForBox { \n \
    background-color: #b3ccff; \n \
} \n \
div.StoreBox { \n \
    background-color: #f4f8bf; \n \
} \n \
div.AllocateBox { \n \
    background-color: #f4f8bf; \n \
} \n \
div.IfBox { \n \
    background-color: #e6eeff; \n \
} \n \
div.memory-cost-div:hover, \n \
div.computation-cost-div:hover { \n \
    border: 1px solid grey; \n \
} \n \
div.spacing { \n \
    flex-grow: 1; \n \
} \n \
table { \n \
    border-radius: 5px; \n \
    font-size: 12px; \n \
    border: 1px dashed grey; \n \
    border-collapse: separate; \n \
    border-spacing: 0; \n \
} \n \
.ifElseTable { \n \
    border: 0px; \n \
}  \n \
.costTable { \n \
    float: right; \n \
    text-align: center; \n \
    border: 0px; \n \
    background-color: rgba(150, 150, 150, 0.5); \n \
} \n \
.costTable td { \n \
    border-top: 1px dashed grey; \n \
} \n \
.costTableHeader, \n \
.costTableData { \n \
    border-collapse: collapse; \n \
    padding-top: 1px; \n \
    padding-bottom: 1px; \n \
    padding-left: 5px; \n \
    padding-right: 5px; \n \
} \n \
span.intType { color: #099; } \n \
span.stringType { color: #990073; } \n \
.middleCol { \n \
    border-right: 1px dashed grey; \n \
} \n \
div.content { \n \
    flex-grow: 1; \n \
} \n \
.prodConsColorButton { \n \
    height: 15px; \n \
    width: 10px; \n \
    margin-right: 1px; \n \
    margin-left: 1px; \n \
    border: 1px solid rgba(0, 0, 0, 0); \n \
    vertical-align: middle; \n \
} \n \
.prodConsColorButton:hover { \n \
    border: 1px solid grey; \n \
} \n \
div.boxHeaderTitle { \n \
    font-weight: bold; \n \
} \n \
";
