#include "IRVisualization.h"
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

StmtSize StmtSizes::get_size(const IRNode *node) const {
    auto it = stmt_sizes.find(node);

    // errors if node is not found
    if (it == stmt_sizes.end()) {
        internal_error << "\n\nStmtSizes::get_size - Node not found in StmtSizes: "
                       << print_node(node) << "\n\n";
        return StmtSize();
    }

    return it->second;
}

string StmtSizes::string_span(string var_name) const {
    return "<span class='stringType'>" + var_name + "</span>";
}
string StmtSizes::int_span(int64_t int_val) const {
    return "<span class='intType'>" + std::to_string(int_val) + "</span>";
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
                       << "StmtSizes::get_simplified_string - Unsupported operator: " << op << "\n";
        return "";
    }
}

void StmtSizes::set_write_size(const IRNode *node, string write_var, string write_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize();
    }
    stmt_sizes[node].writes[write_var] = write_size;
}
void StmtSizes::set_read_size(const IRNode *node, string read_var, string read_size) {
    auto it = stmt_sizes.find(node);
    if (it == stmt_sizes.end()) {
        stmt_sizes[node] = StmtSize();
    }
    stmt_sizes[node].reads[read_var] = read_size;
}

void StmtSizes::visit(const Store *op) {

    // TODO: is this correct?
    uint16_t lanes = op->index.type().lanes();

    set_write_size(op, op->name, int_span(lanes));

    // empty curr_load_values
    curr_load_values.clear();
    op->value.accept(this);

    // set consume (for now, read values)
    for (const auto &load_var : curr_load_values) {
        set_read_size(op, load_var.first, int_span(load_var.second));
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
 * IRVisualization class
 */
string IRVisualization::generate_ir_visualization_html(const Module &m) {
    pre_processor.generate_sizes(m);

    html.str("");
    num_of_nodes = 0;
    start_module_traversal(m);

    return html.str();
}

void IRVisualization::start_module_traversal(const Module &m) {

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

string IRVisualization::open_box_div(string class_name, const IRNode *op) {
    stringstream ss;

    ss << "<div class='box center " << class_name << "'";
    ss << ">";

    if (op != nullptr) {
        ss << generate_computation_cost_div(op);
        ss << generate_memory_cost_div(op);
    }

    ss << open_content_div();
    return ss.str();
}
string IRVisualization::close_box_div() const {
    stringstream ss;
    ss << close_div();  // body div (opened at end of close_header())
    ss << close_div();  // content div
    ss << close_div();  // main box div
    return ss.str();
}
string IRVisualization::open_function_box_div() const {
    return "<div class='center FunctionBox'> <div class='functionContent'>";
}
string IRVisualization::close_function_box_div() const {
    stringstream ss;
    ss << close_div();  // content div
    ss << close_div();  // main box div
    return ss.str();
}
string IRVisualization::open_header_div() const {
    return "<div class='boxHeader'>";
}
string IRVisualization::open_box_header_title_div() const {
    return "<div class='boxHeaderTitle'>";
}
string IRVisualization::open_box_header_table_div() const {
    return "<div class='boxHeaderTable'>";
}
string IRVisualization::open_store_div() const {
    return "<div class='store'>";
}
string IRVisualization::close_div() const {
    return "</div>";
}

string IRVisualization::open_header(const string &header, string anchor_name) {
    stringstream ss;
    ss << open_header_div();

    num_of_nodes++;

    // buttons div
    ss << "<div class='collapseExpandButtons'>";

    // expand button - hidden to start
    ss << "<button id='irViz" << num_of_nodes
       << "-show' class='iconButton irVizToggle' onclick='toggleCollapse(" << num_of_nodes
       << ")' style='display: none;'><i class='bi "
       << "bi-chevron-bar-down'></i></button>";

    // collapse button
    ss << "<button id='irViz" << num_of_nodes
       << "-hide' class='iconButton irVizToggle' onclick='toggleCollapse(" << num_of_nodes
       << ")' ><i class='bi bi-chevron-bar-up'></i></button>"
       << "</div>";

    ss << open_box_header_title_div();

    ss << "<span id='" << anchor_name << "_viz'>";
    ss << header;
    ss << "</span>";

    ss << close_div();

    // spacing purposes
    ss << "<div class='spacing'></div>";

    ss << open_box_header_table_div();

    return ss.str();
}
string IRVisualization::close_header(string anchor_name) const {
    stringstream ss;

    ss << close_div();  // header table div
    ss << see_code_button_div(anchor_name);
    ss << close_div();  // header div

    // open body div
    ss << "<div id='irViz" << num_of_nodes << "' class='boxBody'>";

    return ss.str();
}
string IRVisualization::div_header(const string &header, StmtSize *size, string anchor_name) {
    stringstream ss;

    ss << open_header(header, anchor_name);

    // add producer consumer size if size is provided
    if (size != nullptr) {
        ss << read_write_table(*size);
    }

    ss << close_header(anchor_name);

    return ss.str();
}
string IRVisualization::function_div_header(const string &function_name, string anchor_name) const {
    stringstream ss;

    ss << "<div class='functionHeader'>";

    ss << "<span id='" << function_name << "'>";
    ss << "<span id='" << anchor_name << "_viz' style='display: inline-block;'>";
    ss << "<h4 style='margin-bottom: 0px;'> Func: " << function_name << "</h4>";
    ss << "</span>";
    ss << "</span>";

    ss << see_code_button_div(anchor_name, false);

    ss << "</div>";

    return ss.str();
}
vector<string> IRVisualization::get_allocation_sizes(const Allocate *op) const {
    vector<string> sizes;

    stringstream type;
    type << "<span class='stringType'>" << op->type << "</span>";
    sizes.push_back(type.str());

    for (const auto &extent : op->extents) {
        stringstream ss;
        if (extent.as<IntImm>()) {
            ss << "<span class='intType'>" << extent << "</span>";
        } else {
            ss << "<span class='stringType'>" << extent << "</span>";
        }

        sizes.push_back(ss.str());
    }

    internal_assert(sizes.size() == op->extents.size() + 1);

    return sizes;
}
string IRVisualization::allocate_div_header(const Allocate *op, const string &header,
                                            string anchor_name) {
    stringstream ss;

    ss << open_header(header, anchor_name);

    vector<string> allocation_sizes = get_allocation_sizes(op);
    ss << allocate_table(allocation_sizes);

    ss << close_header(anchor_name);

    return ss.str();
}
string IRVisualization::for_loop_div_header(const For *op, const string &header,
                                            string anchor_name) {
    stringstream ss;

    ss << open_header(header, anchor_name);

    string loopSize = get_loop_iterator(op);
    ss << for_loop_table(loopSize);

    ss << close_header(anchor_name);

    return ss.str();
}

string IRVisualization::if_tree(const IRNode *op, const string &header, string anchor_name) {
    stringstream ss;

    ss << "<li>";
    ss << "<span class='tf-nc if-node'>";

    ss << open_box_div("IfBox", op);
    ss << div_header(header, nullptr, anchor_name);

    return ss.str();
}
string IRVisualization::close_if_tree() const {
    stringstream ss;
    ss << close_box_div();
    ss << "</span>";
    ss << "</li>";
    return ss.str();
}

string IRVisualization::read_write_table(StmtSize &size) const {
    stringstream read_write_table_ss;

    // open table
    read_write_table_ss << "<table class='costTable'>";

    // Prod | Cons
    read_write_table_ss << "<tr>";

    read_write_table_ss << "<th colspan='2' class='costTableHeader middleCol'>";
    read_write_table_ss << "Written";
    read_write_table_ss << "</th>";

    read_write_table_ss << "<th colspan='2' class='costTableHeader'>";
    read_write_table_ss << "Read";
    read_write_table_ss << "</th>";

    read_write_table_ss << "</tr>";

    // produces and consumes are empty
    if (size.empty()) {
        internal_error << "\n\n"
                       << "IRVisualization::read_write_table - size is empty"
                       << "\n";
    }

    // produces and consumes aren't empty
    else {
        vector<string> rows;

        // fill in producer variables
        for (const auto &produce_var : size.writes) {
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
        unsigned long row_num = 0;
        for (const auto &consume_var : size.reads) {
            string ss;
            ss += "<td class='costTableData'>";
            ss += consume_var.first + ": ";
            ss += "</td>";

            ss += "<td class='costTableData'>";
            ss += consume_var.second;
            ss += "</td>";

            if (row_num < rows.size()) {
                rows[row_num] += ss;
            } else {
                // pad row with empty cells for produce
                string s_empty;
                s_empty += "<td colspan='2' class='costTableData middleCol'>";
                s_empty += "</td>";

                rows.push_back(s_empty + ss);
            }
            row_num++;
        }

        // pad row with empty calls for consume
        row_num = size.reads.size();
        while (row_num < size.writes.size()) {
            string s_empty;
            s_empty += "<td class='costTableData'>";
            s_empty += "</td>";
            s_empty += "<td class='costTableData'>";
            s_empty += "</td>";

            rows[row_num] += s_empty;
            row_num++;
        }

        // add rows to read_write_table_ss
        for (const auto &row : rows) {
            read_write_table_ss << "<tr>";
            read_write_table_ss << row;
            read_write_table_ss << "</tr>";
        }
    }

    // close table
    read_write_table_ss << "</table>";

    return read_write_table_ss.str();
}
string IRVisualization::allocate_table(vector<string> &allocation_sizes) const {
    stringstream allocate_table_ss;

    // open table
    allocate_table_ss << "<table class='costTable'>";

    // open header and data rows
    stringstream header;
    stringstream data;

    header << "<tr>";
    data << "<tr>";

    // iterate through all allocation sizes and add them to the header and data rows
    for (unsigned long i = 0; i < allocation_sizes.size(); i++) {
        if (i == 0) {
            header << "<th class='costTableHeader middleCol'>";
            header << "Type";
            header << "</th>";

            data << "<td class='costTableHeader middleCol'>";
            data << allocation_sizes[0];
            data << "</td>";
        } else {
            if (i < allocation_sizes.size() - 1) {
                header << "<th class='costTableHeader middleCol'>";
                data << "<td class='costTableHeader middleCol'>";
            } else {
                header << "<th class='costTableHeader'>";
                data << "<td class='costTableHeader'>";
            }
            header << "Dim-" + std::to_string(i);
            header << "</th>";

            data << allocation_sizes[i];
            data << "</td>";
        }
    }

    // close header and data rows
    header << "</tr>";
    data << "</tr>";

    // add header and data rows to allocate_table_ss
    allocate_table_ss << header.str();
    allocate_table_ss << data.str();

    // close table
    allocate_table_ss << "</table>";

    return allocate_table_ss.str();
}
string IRVisualization::for_loop_table(string loop_size) const {
    stringstream for_loop_table_ss;

    // open table
    for_loop_table_ss << "<table class='costTable'>";

    // Loop Size
    for_loop_table_ss << "<tr>";

    for_loop_table_ss << "<th class='costTableHeader'>";
    for_loop_table_ss << "Loop Span";
    for_loop_table_ss << "</th>";

    for_loop_table_ss << "</tr>";

    for_loop_table_ss << "<tr>";

    // loop size
    for_loop_table_ss << "<td class='costTableData'>";
    for_loop_table_ss << loop_size;
    for_loop_table_ss << "</td>";

    for_loop_table_ss << "</tr>";

    // close table
    for_loop_table_ss << "</table>";

    return for_loop_table_ss.str();
}

string IRVisualization::see_code_button_div(string anchor_name, bool put_div) const {
    stringstream ss;
    if (put_div) ss << "<div>";
    ss << "<button class='iconButton'";
    ss << "onclick='scrollToFunctionVizToCode(\"" << anchor_name << "\")'>";
    ss << "<i class='bi bi-code-square'></i>";
    ss << "</button>";
    if (put_div) ss << "</div>";
    return ss.str();
}

string IRVisualization::info_tooltip(string tooltip_text, string class_name = "") {
    stringstream ss;

    // info-button
    ir_viz_tooltip_count++;
    ss << "<button id='irVizButton" << ir_viz_tooltip_count << "' ";
    ss << "aria-describedby='irVizTooltip" << ir_viz_tooltip_count << "' ";
    ss << "class='info-button' role='button' ";
    ss << ">";
    ss << "<i class='bi bi-info'></i>";
    ss << "</button>";

    // tooltip span
    ss << "<span id='irVizTooltip" << ir_viz_tooltip_count << "' ";
    ss << "class='tooltip";
    if (class_name != "") {
        ss << " " + class_name;
    }
    ss << "'";
    ss << "role='irVizTooltip" << ir_viz_tooltip_count << "'>";
    ss << tooltip_text;
    ss << "</span>";

    return ss.str();
}

string IRVisualization::generate_computation_cost_div(const IRNode *op) {
    stringstream ss;

    // skip if it's a store
    if (op->node_type == IRNodeType::Store) return "";

    ir_viz_tooltip_count++;

    string tooltip_text = find_stmt_cost.generate_computation_cost_tooltip(op, true, "");

    // tooltip span
    ss << "<span id='irVizTooltip" << ir_viz_tooltip_count << "' class='tooltip CostTooltip' ";
    ss << "role='irVizTooltip" << ir_viz_tooltip_count << "'>";
    ss << tooltip_text;
    ss << "</span>";

    int computation_range = find_stmt_cost.get_computation_color_range(op, true);
    string class_name = "computation-cost-div CostColor" + std::to_string(computation_range);
    ss << "<div id='irVizButton" << ir_viz_tooltip_count << "' ";
    ss << "aria-describedby='irVizTooltip" << ir_viz_tooltip_count << "' ";
    ss << "class='" << class_name << "'>";

    ss << close_div();

    return ss.str();
}
string IRVisualization::generate_memory_cost_div(const IRNode *op) {
    stringstream ss;

    // skip if it's a store
    if (op->node_type == IRNodeType::Store) return "";

    ir_viz_tooltip_count++;

    string tooltip_text = find_stmt_cost.generate_data_movement_cost_tooltip(op, true, "");

    // tooltip span
    ss << "<span id='irVizTooltip" << ir_viz_tooltip_count << "' class='tooltip CostTooltip' ";
    ss << "role='irVizTooltip" << ir_viz_tooltip_count << "'>";
    ss << tooltip_text;
    ss << "</span>";

    int data_movement_range = find_stmt_cost.get_data_movement_color_range(op, true);
    string class_name = "memory-cost-div CostColor" + std::to_string(data_movement_range);
    ss << "<div id='irVizButton" << ir_viz_tooltip_count << "' ";
    ss << "aria-describedby='irVizTooltip" << ir_viz_tooltip_count << "' ";
    ss << "class='" << class_name << "'>";

    ss << close_div();

    return ss.str();
}
string IRVisualization::open_content_div() const {
    return "<div class='content'>";
}

string IRVisualization::color_button(int color_range) {
    stringstream ss;

    ir_viz_tooltip_count++;
    ss << "<button id='irVizButton" << ir_viz_tooltip_count << "' ";
    ss << "aria-describedby='irVizTooltip" << ir_viz_tooltip_count << "' ";
    ss << "class='irVizColorButton CostColor" << color_range << "' role='button' ";
    ss << ">";
    ss << "</button>";

    return ss.str();
}

string IRVisualization::computation_div(const IRNode *op) {
    // want exclusive cost (so that the colors match up with exclusive costs)
    int computation_range = find_stmt_cost.get_computation_color_range(op, false);

    stringstream ss;
    ss << color_button(computation_range);

    string tooltip_text = find_stmt_cost.generate_computation_cost_tooltip(op, false, "");

    // tooltip span
    ss << "<span id='irVizTooltip" << ir_viz_tooltip_count << "' class='tooltip CostTooltip' ";
    ss << "role='irVizTooltip" << ir_viz_tooltip_count << "'>";
    ss << tooltip_text;
    ss << "</span>";

    return ss.str();
}
string IRVisualization::data_movement_div(const IRNode *op) {
    // want exclusive cost (so that the colors match up with exclusive costs)
    int data_movement_range = find_stmt_cost.get_data_movement_color_range(op, false);

    stringstream ss;
    ss << color_button(data_movement_range);

    string tooltip_text = find_stmt_cost.generate_data_movement_cost_tooltip(op, false, "");

    // tooltip span
    ss << "<span id='irVizTooltip" << ir_viz_tooltip_count << "' class='tooltip CostTooltip' ";
    ss << "role='irVizTooltip" << ir_viz_tooltip_count << "'>";
    ss << tooltip_text;
    ss << "</span>";

    return ss.str();
}
string IRVisualization::tooltip_table(vector<pair<string, string>> &table) const {
    stringstream ss;
    ss << "<table class='tooltipTable'>";
    for (auto &row : table) {
        ss << "<tr>";
        ss << "<td class = 'left-table'>" << row.first << "</td>";
        ss << "<td class = 'right-table'> " << row.second << "</td>";
        ss << "</tr>";
    }
    ss << "</table>";
    return ss.str();
}
string IRVisualization::cost_colors(const IRNode *op) {
    stringstream ss;
    ss << computation_div(op);
    ss << data_movement_div(op);
    return ss.str();
}

void IRVisualization::visit_function(const LoweredFunc &func) {
    html << open_function_box_div();

    function_count++;
    string anchor_name = "loweredFunc" + std::to_string(function_count);

    html << function_div_header(func.name, anchor_name);

    html << "<div class='functionViz'>";
    func.body.accept(this);
    html << "</div>";

    html << close_function_box_div();
}
void IRVisualization::visit(const Variable *op) {
    // if op->name starts with "::", remove "::"
    string var_name = op->name;
    if (var_name[0] == ':' && var_name[1] == ':') {
        var_name = var_name.substr(2);
    }

    // see if var_name is in pre_processor.function_names
    if (std::count(pre_processor.function_names.begin(), pre_processor.function_names.end(),
                   var_name)) {

        html << "<div class='box center FunctionCallBox'>";

        html << "Function Call";
        html << "<button class='function-scroll-button' role='button' ";
        html << "onclick='scrollToFunctionCodeToViz(\"" << var_name << "\")'>";

        html << var_name;
        html << "</button>";

        html << "</div>";
    }
}
void IRVisualization::visit(const ProducerConsumer *op) {
    html << open_box_div("ProducerConsumerBox", op);

    producer_consumer_count++;
    string anchor_name = "producerConsumer" + std::to_string(producer_consumer_count);

    string header = (op->is_producer ? "Produce" : "Consume");
    header += " " + op->name;

    html << div_header(header, nullptr, anchor_name);

    op->body.accept(this);

    html << close_box_div();
}
string IRVisualization::get_loop_iterator(const For *op) const {
    Expr min = op->min;
    Expr extent = op->extent;

    string loop_iterator;

    // check if min and extend are of type IntImm
    if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::IntImm) {
        int64_t min_vale = min.as<IntImm>()->value;
        int64_t extent_value = extent.as<IntImm>()->value;
        uint16_t range = uint16_t(extent_value - min_vale);
        loop_iterator = pre_processor.int_span(range);
    }

    else if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::Variable) {
        int64_t min_vale = min.as<IntImm>()->value;
        string min_name = pre_processor.int_span(min_vale);
        string extent_name = pre_processor.string_span(extent.as<Variable>()->name);

        if (min_vale == 0) {
            loop_iterator = extent_name;
        } else {
            loop_iterator = "(" + extent_name + " - " + min_name + ")";
        }
    }

    else if (min.node_type() == IRNodeType::IntImm && extent.node_type() == IRNodeType::Add) {
        int64_t min_vale = min.as<IntImm>()->value;
        string min_name = pre_processor.int_span(min_vale);
        string extent_name = "(";

        // deal with a
        if (extent.as<Add>()->a.node_type() == IRNodeType::IntImm) {
            int64_t extent_value = extent.as<Add>()->a.as<IntImm>()->value;
            extent_name += pre_processor.int_span(extent_value);
        } else if (extent.as<Add>()->a.node_type() == IRNodeType::Variable) {
            extent_name += pre_processor.string_span(extent.as<Add>()->a.as<Variable>()->name);
        } else {
            internal_error << "\n"
                           << "In for loop: " << op->name << "\n"
                           << pre_processor.print_node(extent.as<Add>()->a.get()) << "\n"
                           << "StmtSizes::visit(const For *op): add->a isn't IntImm or Variable - "
                              "can't generate irViz hierarchy yet. \n\n";
        }

        extent_name += "+";

        // deal with b
        if (extent.as<Add>()->b.node_type() == IRNodeType::IntImm) {
            int64_t extent_value = extent.as<Add>()->b.as<IntImm>()->value;
            extent_name += pre_processor.int_span(extent_value);
        } else if (extent.as<Add>()->b.node_type() == IRNodeType::Variable) {
            extent_name += pre_processor.string_span(extent.as<Add>()->b.as<Variable>()->name);
        } else {
            internal_error << "\n"
                           << "In for loop: " << op->name << "\n"
                           << pre_processor.print_node(extent.as<Add>()->b.get()) << "\n"
                           << "StmtSizes::visit(const For *op): add->b isn't IntImm or Variable - "
                              "can't generate irViz hierarchy yet. \n\n";
        }
        extent_name += ")";

        if (min_vale == 0) {
            loop_iterator = extent_name;
        } else {
            loop_iterator = "(" + extent_name + " - " + min_name + ")";
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
               "can't generate irViz hierarchy yet. \n\n";
    }

    return loop_iterator;
}
void IRVisualization::visit(const For *op) {
    html << open_box_div("ForBox", op);

    for_count++;
    string anchor_name = "for" + std::to_string(for_count);

    string header = "For (" + op->name + ")";

    html << for_loop_div_header(op, header, anchor_name);

    op->body.accept(this);

    html << close_box_div();
}
void IRVisualization::visit(const IfThenElse *op) {
    // open main if tree
    html << "<div class='tf-tree tf-gap-sm tf-custom-irViz'>";
    html << "<ul>";
    html << "<li><span class='tf-nc if-node'>";
    html << "If";
    html << "</span>";
    html << "<ul>";

    string if_header;
    if_header += "if ";

    // anchor name
    if_count++;
    string anchor_name = "if" + std::to_string(if_count);

    while (true) {
        stringstream condition;
        condition << op->condition;

        string condition_string = condition.str();
        // make condition smaller if it's too big
        if (condition_string.size() > MAX_CONDITION_LENGTH) {
            condition.str("");
            condition << "...";
            condition << info_tooltip(condition_string, "conditionTooltip");
        }

        if_header += condition.str();

        html << if_tree(op, if_header, anchor_name);

        // then body
        op->then_case.accept(this);

        html << close_if_tree();

        // if there is no else case, we are done
        if (!op->else_case.defined()) {
            break;
        }

        // if else case is another ifthenelse, recurse and reset op to else case
        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            op = nested_if;
            if_header = "";
            if_header += "else if ";

            // anchor name
            if_count++;
            anchor_name = "if" + std::to_string(if_count);

        }

        // if else case is not another ifthenelse
        else {

            string else_header;
            else_header += "else ";

            // anchor name
            if_count++;
            anchor_name = "if" + std::to_string(if_count);

            html << if_tree(op->else_case.get(), else_header, anchor_name);

            op->else_case.accept(this);

            html << close_if_tree();
            break;
        }
    }

    // close main if tree
    html << "</ul>";
    html << "</li>";
    html << "</ul>";
    html << "</div>";
}
void IRVisualization::visit(const Store *op) {
    StmtSize size = pre_processor.get_size(op);

    store_count++;
    string anchor_name = "store" + std::to_string(store_count);

    string header = "Store " + op->name;

    vector<pair<string, string>> table_rows;
    table_rows.push_back({"Vector Size", std::to_string(op->index.type().lanes())});
    table_rows.push_back({"Bit Size", std::to_string(op->index.type().bits())});

    header += info_tooltip(tooltip_table(table_rows));

    html << open_box_div("StoreBox", op);

    html << div_header(header, &size, anchor_name);

    op->value.accept(this);

    html << close_box_div();
}
void IRVisualization::visit(const Load *op) {
    string header;
    vector<pair<string, string>> table_rows;

    if (op->type.is_scalar()) {
        header = "Scalar ";
    }

    else if (op->type.is_vector()) {
        if (op->index.node_type() == IRNodeType::Ramp) {
            const Ramp *ramp = op->index.as<Ramp>();

            table_rows.push_back({"Ramp lanes", std::to_string(ramp->lanes)});
            stringstream ramp_stride;
            ramp_stride << ramp->stride;
            table_rows.push_back({"Ramp stride", ramp_stride.str()});

            if (ramp->stride.node_type() == IRNodeType::IntImm) {
                int64_t stride = ramp->stride.as<IntImm>()->value;
                if (stride == 1) {
                    header = "Dense vector ";
                } else {
                    header = "Strided vector ";
                }
            } else {
                header = "Dense vector ";
            }
        } else {
            header = "Dense vector ";
        }
    }

    else {
        internal_error << "\n\nUnsupported type for Load: " << op->type << "\n\n";
    }

    header += "load " + op->name;

    if (find_stmt_cost.is_local_variable(op->name)) {
        table_rows.push_back({"Variable Type", "local var"});
    } else {
        table_rows.push_back({"Variable Type", "global var"});
    }

    table_rows.push_back({"Bit Size", std::to_string(op->index.type().bits())});
    table_rows.push_back({"Vector Size", std::to_string(op->index.type().lanes())});

    if (op->param.defined()) {
        table_rows.push_back({"Parameter", op->param.name()});
    }

    header += info_tooltip(tooltip_table(table_rows));

    html << open_store_div();
    html << cost_colors(op);
    html << header;
    html << close_div();
}
string IRVisualization::get_memory_type(MemoryType mem_type) const {
    if (mem_type == MemoryType::Auto) {
        return "Auto";
    } else if (mem_type == MemoryType::Heap) {
        return "Heap";
    } else if (mem_type == MemoryType::Stack) {
        return "Stack";
    } else if (mem_type == MemoryType::Register) {
        return "Register";
    } else if (mem_type == MemoryType::GPUShared) {
        return "GPUShared";
    } else if (mem_type == MemoryType::GPUTexture) {
        return "GPUTexture";
    } else if (mem_type == MemoryType::LockedCache) {
        return "LockedCache";
    } else if (mem_type == MemoryType::VTCM) {
        return "VTCM";
    } else if (mem_type == MemoryType::AMXTile) {
        return "AMXTile";
    } else {
        internal_error << "\n\n"
                       << "Unknown memory type"
                       << "\n";
        return "Unknown Memory Type";
    }
}
void IRVisualization::visit(const Allocate *op) {
    html << open_box_div("AllocateBox", op);

    allocate_count++;
    string anchor_name = "allocate" + std::to_string(allocate_count);

    string header = "Allocate " + op->name;

    vector<pair<string, string>> table_rows;
    table_rows.push_back({"Memory Type", get_memory_type(op->memory_type)});

    if (!is_const_one(op->condition)) {
        stringstream condition_string;
        condition_string << op->condition;
        table_rows.push_back({"Condition", condition_string.str()});
    }
    if (op->new_expr.defined()) {
        internal_error << "\n"
                       << "IRVisualization: Allocate " << op->name
                       << " `op->new_expr.defined()` is not supported.\n\n";

        stringstream new_expr_string;
        new_expr_string << op->new_expr;
        table_rows.push_back({"New Expr", new_expr_string.str()});
    }
    if (!op->free_function.empty()) {
        internal_error << "\n"
                       << "IRVisualization: Allocate " << op->name
                       << " `!op->free_function.empty()` is not supported.\n\n";

        stringstream free_func_string;
        free_func_string << op->free_function;
        table_rows.push_back({"Free Function", free_func_string.str()});
    }

    table_rows.push_back({"Bit Size", std::to_string(op->type.bits())});
    table_rows.push_back({"Vector Size", std::to_string(op->type.lanes())});

    header += info_tooltip(tooltip_table(table_rows));

    html << allocate_div_header(op, header, anchor_name);

    op->body.accept(this);

    html << close_box_div();
}

string IRVisualization::generate_irViz_js() {
    stringstream irVizJS;

    irVizJS << "\n// irViz JS\n"
            << "for (let i = 1; i <= " << ir_viz_tooltip_count << "; i++) { \n"
            << "    const button = document.getElementById('irVizButton' + i); \n"
            << "    const tooltip = document.getElementById('irVizTooltip' + i); \n"
            << "    button.addEventListener('mouseenter', () => { \n"
            << "        showTooltip(button, tooltip); \n"
            << "    }); \n"
            << "    button.addEventListener('mouseleave', () => { \n"
            << "        hideTooltip(tooltip); \n"
            << "    } \n"
            << "    ); \n"
            << "    tooltip.addEventListener('focus', () => { \n"
            << "        showTooltip(button, tooltip); \n"
            << "    } \n"
            << "    ); \n"
            << "    tooltip.addEventListener('blur', () => { \n"
            << "        hideTooltip(tooltip); \n"
            << "    } \n"
            << "    ); \n"
            << "} \n"
            << "function toggleCollapse(id) {\n "
            << "    var buttonShow = document.getElementById('irViz' + id + '-show');\n"
            << "    var buttonHide = document.getElementById('irViz' + id + '-hide');\n"
            << "    var body = document.getElementById('irViz' + id);\n"
            << "    if (body.style.visibility != 'hidden') {\n"
            << "        body.style.visibility = 'hidden';\n"
            << "        body.style.height = '0px';\n"
            << "        body.style.width = '0px';\n"
            << "        buttonShow.style.display = 'block';\n"
            << "        buttonHide.style.display = 'none';\n"
            << "    } else {\n"
            << "        body.style = '';\n"
            << "        buttonShow.style.display = 'none';\n"
            << "        buttonHide.style.display = 'block';\n"
            << "    }\n"
            << "}\n ";

    return irVizJS.str();
}

/*
 * PRINT NODE
 */
string StmtSizes::print_node(const IRNode *node) const {
    stringstream ss;
    ss << "Node in question has type: ";
    IRNodeType type = node->node_type;
    if (type == IRNodeType::IntImm) {
        ss << "IntImm type";
        auto node1 = dynamic_cast<const IntImm *>(node);
        ss << "value: " << node1->value;
    } else if (type == IRNodeType::UIntImm) {
        ss << "UIntImm type";
    } else if (type == IRNodeType::FloatImm) {
        ss << "FloatImm type";
    } else if (type == IRNodeType::StringImm) {
        ss << "StringImm type";
    } else if (type == IRNodeType::Broadcast) {
        ss << "Broadcast type";
    } else if (type == IRNodeType::Cast) {
        ss << "Cast type";
    } else if (type == IRNodeType::Variable) {
        ss << "Variable type";
    } else if (type == IRNodeType::Add) {
        ss << "Add type";
        auto node1 = dynamic_cast<const Add *>(node);
        ss << "a: " << print_node(node1->a.get()) << endl;
        ss << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Sub) {
        ss << "Sub type" << endl;
        auto node1 = dynamic_cast<const Sub *>(node);
        ss << "a: " << print_node(node1->a.get()) << endl;
        ss << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Mod) {
        ss << "Mod type" << endl;
        auto node1 = dynamic_cast<const Mod *>(node);
        ss << "a: " << print_node(node1->a.get()) << endl;
        ss << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Mul) {
        ss << "Mul type" << endl;
        auto node1 = dynamic_cast<const Mul *>(node);
        ss << "a: " << print_node(node1->a.get()) << endl;
        ss << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Div) {
        ss << "Div type" << endl;
        auto node1 = dynamic_cast<const Div *>(node);
        ss << "a: " << print_node(node1->a.get()) << endl;
        ss << "b: " << print_node(node1->b.get()) << endl;
    } else if (type == IRNodeType::Min) {
        ss << "Min type";
    } else if (type == IRNodeType::Max) {
        ss << "Max type";
    } else if (type == IRNodeType::EQ) {
        ss << "EQ type";
    } else if (type == IRNodeType::NE) {
        ss << "NE type";
    } else if (type == IRNodeType::LT) {
        ss << "LT type";
    } else if (type == IRNodeType::LE) {
        ss << "LE type";
    } else if (type == IRNodeType::GT) {
        ss << "GT type";
    } else if (type == IRNodeType::GE) {
        ss << "GE type";
    } else if (type == IRNodeType::And) {
        ss << "And type";
    } else if (type == IRNodeType::Or) {
        ss << "Or type";
    } else if (type == IRNodeType::Not) {
        ss << "Not type";
    } else if (type == IRNodeType::Select) {
        ss << "Select type";
    } else if (type == IRNodeType::Load) {
        ss << "Load type";
    } else if (type == IRNodeType::Ramp) {
        ss << "Ramp type";
    } else if (type == IRNodeType::Call) {
        ss << "Call type";
    } else if (type == IRNodeType::Let) {
        ss << "Let type";
    } else if (type == IRNodeType::Shuffle) {
        ss << "Shuffle type";
    } else if (type == IRNodeType::VectorReduce) {
        ss << "VectorReduce type";
    } else if (type == IRNodeType::LetStmt) {
        ss << "LetStmt type";
    } else if (type == IRNodeType::AssertStmt) {
        ss << "AssertStmt type";
    } else if (type == IRNodeType::ProducerConsumer) {
        ss << "ProducerConsumer type";
    } else if (type == IRNodeType::For) {
        ss << "For type";
    } else if (type == IRNodeType::Acquire) {
        ss << "Acquire type";
    } else if (type == IRNodeType::Store) {
        ss << "Store type";
    } else if (type == IRNodeType::Provide) {
        ss << "Provide type";
    } else if (type == IRNodeType::Allocate) {
        ss << "Allocate type";
    } else if (type == IRNodeType::Free) {
        ss << "Free type";
    } else if (type == IRNodeType::Realize) {
        ss << "Realize type";
    } else if (type == IRNodeType::Block) {
        ss << "Block type";
    } else if (type == IRNodeType::Fork) {
        ss << "Fork type";
    } else if (type == IRNodeType::IfThenElse) {
        ss << "IfThenElse type";
    } else if (type == IRNodeType::Evaluate) {
        ss << "Evaluate type";
    } else if (type == IRNodeType::Prefetch) {
        ss << "Prefetch type";
    } else if (type == IRNodeType::Atomic) {
        ss << "Atomic type";
    } else {
        ss << "Unknown type";
    }

    return ss.str();
}

const string IRVisualization::scroll_to_function_JS_viz_to_code = "\n \
// scroll to function - viz to code\n \
function makeVisible(element) { \n \
    if (!element) return; \n \
    if (element.class_name == 'mainContent') return; \n \
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

const string IRVisualization::ir_viz_CSS = "\n \
/* irViz CSS */\n \
.tf-custom-irViz .tf-nc { border-radius: 5px; border: 1px solid; }\n \
.tf-custom-irViz .tf-nc:before, .tf-custom-irViz .tf-nc:after { border-left-width: 1px; }\n \
.tf-custom-irViz li li:before { border-top-width: 1px; }\n \
.tf-custom-irViz .end-node { border-style: dashed; }\n \
.tf-custom-irViz .tf-nc { background-color: #e6eeff; }\n \
.tf-custom-irViz { font-size: 12px; } \n \
div.box { \n \
    border: 1px dashed grey; \n \
    border-radius: 5px; \n \
    margin: 5px; \n \
    padding: 5px; \n \
    display: flex; \n \
    width: max-content; \n \
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
    border: 1px dashed grey; \n \
    border-radius: 5px; \n \
    margin-bottom: 15px; \n \
    padding: 5px; \n \
    width: max-content; \n \
} \n \
div.functionHeader { \n \
    display: flex; \n \
    margin-bottom: 10px; \n \
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
.irVizColorButton { \n \
    height: 15px; \n \
    width: 10px; \n \
    margin-right: 2px; \n \
    border: 1px solid rgba(0, 0, 0, 0); \n \
    vertical-align: middle; \n \
    border-radius: 2px; \n \
} \n \
.irVizColorButton:hover { \n \
    border: 1px solid grey; \n \
} \n \
div.boxHeaderTitle { \n \
    font-weight: bold; \n \
} \n \
.irVizToggle { \n \
    margin-right: 5px; \n \
} \n \
";
