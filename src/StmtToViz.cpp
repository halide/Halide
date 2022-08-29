#include "StmtToViz.h"
#include "DependencyGraph.h"
#include "Error.h"
#include "FindStmtCost.h"
#include "GetStmtHierarchy.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Module.h"
#include "ProducerConsumerHierarchy.h"
#include "Scope.h"
#include "Substitute.h"
#include "Util.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#define PRINT_HIERARCHY false
#define PRINT_DEPENDENCIES false
#define PRINT_PROD_CONS false
#define NAVIGATION_STYLE true  // change in ProduceConsumerHierarchy.cpp as well

namespace Halide {
namespace Internal {

namespace {
template<typename T>
string to_string(T value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

class StmtToViz : public IRVisitor {

    static const string css, js;
    static const string vizCss;
    static const string costColorsCSS;
    static const string navigationHTML;
    static const string scrollToCSS, navigationCSS;
    static const string lineNumbersCSS;
    static const string tooltipCSS;
    static const string expandCodeJS;

    FindStmtCost findStmtCost;                            // used for finding the cost of statements
    GetStmtHierarchy getStmtHierarchy;                    // used for getting the hierarchy of
                                                          // statements
    ProducerConsumerHierarchy producerConsumerHierarchy;  // used for getting the hierarchy of
                                                          // producer/consumer
    DependencyGraph dependencyGraph;                      // used for getting the dependency graph

    // This allows easier access to individual elements.
    int id_count;

private:
    std::ofstream stream;

    // used for deciding which variables are in context vs not
    vector<string> curr_context;
    bool in_context;
    int curr_line_num;  // for accessing div of that line
    string content_rule_script_stream;
    bool in_loop;

    // used for getting anchor names
    int ifCount = 0;
    int producerConsumerCount = 0;
    int forCount = 0;
    int storeCount = 0;
    int allocateCount = 0;

    // used for tooltip stuff
    int tooltipCount = 0;

    // used for getStmtHierarchy popup stuff
    int popupCount = 0;
    string popups;

    string get_file_name(string fileName) {
        // remove leading directories from filename
        string f = fileName;
        size_t pos = f.find_last_of("/");
        if (pos != string::npos) {
            f = f.substr(pos + 1);
        }
        cout << "Printing to " << f << endl;
        return f;
    }

    void reset_context() {
        curr_context.clear();
    }

    bool is_in_context(const string name) const {
        for (auto &context : curr_context) {
            if (context == name) {
                return true;
            }
        }
        return false;
    }

    // only removes it if it's there
    void remove_context(const string name) {
        for (auto it = curr_context.begin(); it != curr_context.end(); ++it) {
            if (*it == name) {
                curr_context.erase(it);
                return;
            }
        }
    }

    int unique_id() {
        return ++id_count;
    }

    void add_context_rule(const int line_num) {
        string id = "ContextSpan" + to_string(line_num);
        content_rule_script_stream +=
            "document.getElementById('" + id + "').style.backgroundColor = 'blue';" + "\n";
    }

    // All spans and divs will have an id of the form "x-y", where x
    // is shared among all spans/divs in the same context, and y is unique.
    std::vector<int> context_stack;
    string open_tag(const string &tag, const string &cls, int id = -1) {
        stringstream s;
        s << "<" << tag << " class='" << cls << "' id='";
        if (id == -1) {
            s << context_stack.back() << "-";
            s << unique_id();
        } else {
            s << id;
        }
        s << "'>";
        context_stack.push_back(unique_id());
        return s.str();
    }
    string tag(const string &tag, const string &cls, const string &body, int id = -1) {
        stringstream s;
        s << open_tag(tag, cls, id);
        s << body;
        s << close_tag(tag);
        return s.str();
    }
    string close_tag(const string &tag) {
        context_stack.pop_back();
        return "</" + tag + ">";
    }

    string tooltip(const string &hierarchyHTML, const string &tooltipText) {
        stringstream s;
        tooltipCount++;

        s << "<button id='button" << tooltipCount << "' ";
        s << "aria-describedby='tooltip" << tooltipCount << "' ";
        s << "class='info-button' role='button' ";
        s << "data-bs-toggle='modal' data-bs-target='#stmtHierarchyModal" << popupCount << "' ";
        s << "onmouseover='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"rgba(10,10,10,0.1)\";'";
        s << "onmouseout='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"transparent\";'";
        s << ">";
        s << "<i class='bi bi-info'></i>";
        s << "</button>";

        // popup window - will put them all at the end
        popups += hierarchyHTML + "\n";

        // tooltip span
        s << "<span id='tooltip" << tooltipCount << "' class='tooltip' ";
        s << "role='tooltip" << tooltipCount << "'>";
        s << tooltipText;
        s << "</span>";

        return s.str();
    }

    string cost_table_tooltip(const IRNode *op, const string &hierarchyHTML) {
        int depth = findStmtCost.get_depth(op);
        int computationCost = findStmtCost.get_calculated_computation_cost(op);
        int dataMovementCost = findStmtCost.get_data_movement_cost(op);

        stringstream tooltipText;

        tooltipText << "<table class='tooltipTable'>";

        tooltipText << "<tr>";
        tooltipText << "<td class = 'left-table'> Depth</td>";
        tooltipText << "<td class = 'right-table'> " << depth << "</td>";
        tooltipText << "</tr>";

        tooltipText << "<tr>";
        tooltipText << "<td class = 'left-table'> Computation Cost</td>";
        tooltipText << "<td class = 'right-table'> " << computationCost << "</td>";
        tooltipText << "</tr>";

        tooltipText << "<tr>";
        tooltipText << "<td class = 'left-table'> Data Movement Cost</td>";
        tooltipText << "<td class = 'right-table'> " << dataMovementCost << "</td>";
        tooltipText << "</tr>";
        tooltipText << "</table>";

        tooltipText << "<i><span style='color: grey; margin-top: 5px;'>[Click to see full "
                       "hierarchy]</span></i>";

        string tooltipTextStr = tooltipText.str();

        return tooltip(hierarchyHTML, tooltipTextStr);
    }

    /*
        <div style="display: flex; ">
            <div class="CostColor0"
                style="width: 7px;"></div>
            <div class="CostColor18"
                style="width: 7px;"></div>
            <div style="padding-left: 5px;">
                _halide_buffer_get_dimensions
                <button
                    class='stmtHierarchyButton info-button'
                    onclick='handleClick(42)'>
                    <i
                        id='stmtHierarchyButton42'></i>
                </button>
            </div>
        </div>
    */
    string get_stmt_hierarchy(const Stmt &op) {
        cout << "before" << endl;
        string hierarchyHTML = getStmtHierarchy.get_hierarchy_html(op);
        cout << "after" << endl;
        if (PRINT_HIERARCHY) cout << hierarchyHTML << endl;
        return generate_stmt_hierarchy_popup(hierarchyHTML);
    }
    string get_stmt_hierarchy(const Expr &op) {
        cout << "before" << endl;
        string hierarchyHTML = getStmtHierarchy.get_hierarchy_html(op);
        cout << "after" << endl;
        if (PRINT_HIERARCHY) cout << hierarchyHTML << endl;
        return generate_stmt_hierarchy_popup(hierarchyHTML);
    }

    string generate_stmt_hierarchy_popup(string hierarchyHTML) {
        stringstream popup;

        popupCount++;
        popup << "<div class='modal fade' id='stmtHierarchyModal" << popupCount;
        popup << "' tabindex='-1'\n";
        popup << "    aria-labelledby='stmtHierarchyModalLabel' aria-hidden='true'>\n";
        popup << "    <div class='modal-dialog modal-dialog-scrollable modal-xl'>\n";
        popup << "        <div class='modal-content'>\n";
        popup << "            <div class='modal-header'>\n";
        popup << "                <h5 class='modal-title' id='stmtHierarchyModalLabel'>Statement\n";
        popup << "                    Hierarchy\n";
        popup << "                </h5>\n";
        popup << "                <button type='button' class='btn-close'\n";
        popup << "                    data-bs-dismiss='modal' aria-label='Close'></button>\n";
        popup << "            </div>\n";
        popup << "            <div class='modal-body'>\n";
        popup << hierarchyHTML;
        popup << "            </div>\n";
        popup << "        </div>\n";
        popup << "    </div>\n";
        popup << "</div>\n";

        return popup.str();
    }

    string open_cost_span(const IRNode *op, const string &hierarchyHTML) {
        stringstream s;
        s << cost_table_tooltip(op, hierarchyHTML);
        s << "<span id='Cost" << id_count << "'>";
        return s.str();
    }
    string close_cost_span() {
        return close_span();
    }

    string open_span(const string &cls, int id = -1) {
        return open_tag("span", cls, id);
    }
    string close_span() {
        return close_tag("span");
    }
    string span(const string &cls, const string &body, int id = -1) {
        return tag("span", cls, body, id);
    }
    string matched(const string &cls, const string &body, int id = -1) {
        return span(cls + " Matched", body, id);
    }
    string matched(const string &body) {
        return span("Matched", body);
    }

    string cost_color_spacer() {
        stringstream s;
        s << open_span("CostColorSpacer");
        s << ".";
        s << close_span();
        return s.str();
    }

    string cost_colors(const IRNode *op) {
        // TODO: figure out how to get the div to be given size without needing
        //       to put a `.` in it
        //
        //       fix: ProducerConsumerHierarchy::cost_colors as well
        curr_line_num += 1;

        stringstream s;

        s << "<span id='ContextSpan" << curr_line_num << "'";
        s << "style='";
        s << "margin-left: -45px;";
        s << "width: 13px;";
        s << "display: inline-block;";
        s << "color: transparent;";
        s << "'>";
        s << curr_line_num;
        s << close_span();

        s << cost_color_spacer();

        int computation_range = findStmtCost.get_computation_color_range(op);
        s << open_span("CostColor" + to_string(computation_range) + " CostComputation");
        s << ".";
        s << close_span();

        s << cost_color_spacer();

        int data_movement_range = findStmtCost.get_data_movement_color_range(op);
        s << open_span("CostColor" + to_string(data_movement_range) + " CostMovement");
        s << ".";
        s << close_span();

        s << cost_color_spacer();

        return s.str();
    }

    string open_div(const string &cls, int id = -1) {
        return open_tag("div", cls, id) + "\n";
    }
    string close_div() {
        return close_tag("div") + "\n";
    }

    string open_anchor(const string &anchorName) {
        return "<span class='navigationAnchor' id=\"" + anchorName + "\">";
    }
    string close_anchor() {
        return "</span>";
    }

    string open_line() {
        return "<p class=WrapLine>";
    }
    string close_line() {
        return "</p>";
    }

    string keyword(const string &x) {
        return span("Keyword", x);
    }
    string type(const string &x) {
        return span("Type", x);
    }
    string symbol(const string &x) {
        return span("Symbol", x);
    }

    Scope<int> scope;
    string var(const string &x) {
        int id;
        if (scope.contains(x)) {
            id = scope.get(x);
        } else {
            id = unique_id();
            scope.push(x, id);
        }

        stringstream s;
        s << "<b class='Variable Matched' id='" << id << "-" << unique_id() << "'>";
        s << x;
        s << "</b>";
        return s.str();
    }

    void print_list(const std::vector<Expr> &args) {
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) {
                stream << matched(",") << " ";
            }
            print(args[i]);
        }
    }
    void print_list(const string &l, const std::vector<Expr> &args, const string &r) {
        stream << matched(l);
        print_list(args);
        stream << matched(r);
    }

    string open_expand_button(int id) {
        stringstream button;
        button << "<a class=ExpandButton onclick='return toggle(" << id << ");' href=_blank>"
               << "<div style='position:relative; width:0; height:0;'>"
               << "<div class=ShowHide style='display:none;' id=" << id << "-show"
               << "><i class='fa fa-plus-square-o'></i></div>"
               << "<div class=ShowHide id=" << id << "-hide"
               << "><i class='fa fa-minus-square-o'></i></div>"
               << "</div>";
        return button.str();
    }

    string close_expand_button() {
        return "</a>";
    }

    void visit(const IntImm *op) override {
        stream << open_span("IntImm Imm");
        stream << Expr(op);
        stream << close_span();
    }

    void visit(const UIntImm *op) override {
        stream << open_span("UIntImm Imm");
        stream << Expr(op);
        stream << close_span();
    }

    void visit(const FloatImm *op) override {
        stream << open_span("FloatImm Imm");
        stream << Expr(op);
        stream << close_span();
    }

    void visit(const StringImm *op) override {
        stream << open_span("StringImm");
        stream << "\"";
        for (unsigned char c : op->value) {
            if (c >= ' ' && c <= '~' && c != '\\' && c != '"') {
                stream << c;
            } else {
                stream << "\\";
                switch (c) {
                case '"':
                    stream << "\"";
                    break;
                case '\\':
                    stream << "\\";
                    break;
                case '\t':
                    stream << "t";
                    break;
                case '\r':
                    stream << "r";
                    break;
                case '\n':
                    stream << "n";
                    break;
                default:
                    string hex_digits = "0123456789ABCDEF";
                    stream << "x" << hex_digits[c >> 4] << hex_digits[c & 0xf];
                }
            }
        }
        stream << "\"" << close_span();
    }

    void visit(const Variable *op) override {

        if (is_in_context(op->name)) {
            in_context = true;
        }

        stream << var(op->name);
    }

    void visit(const Cast *op) override {
        stream << open_span("Cast");

        stream << open_span("Matched");
        stream << open_span("Type") << op->type << close_span();
        stream << "(";
        stream << close_span();
        print(op->value);
        stream << matched(")");

        stream << close_span();
    }

    void visit_binary_op(const Expr &a, const Expr &b, const char *op) {
        stream << open_span("BinaryOp");

        stream << matched("(");
        print(a);
        stream << " " << matched("Operator", op) << " ";
        print(b);
        stream << matched(")");

        stream << close_span();
    }

    void visit(const Add *op) override {
        visit_binary_op(op->a, op->b, "+");
    }
    void visit(const Sub *op) override {
        visit_binary_op(op->a, op->b, "-");
    }
    void visit(const Mul *op) override {
        visit_binary_op(op->a, op->b, "*");
    }
    void visit(const Div *op) override {
        visit_binary_op(op->a, op->b, "/");
    }
    void visit(const Mod *op) override {
        visit_binary_op(op->a, op->b, "%");
    }
    void visit(const And *op) override {
        visit_binary_op(op->a, op->b, "&amp;&amp;");
    }
    void visit(const Or *op) override {
        visit_binary_op(op->a, op->b, "||");
    }
    void visit(const NE *op) override {
        visit_binary_op(op->a, op->b, "!=");
    }
    void visit(const LT *op) override {
        visit_binary_op(op->a, op->b, "&lt;");
    }
    void visit(const LE *op) override {
        visit_binary_op(op->a, op->b, "&lt=");
    }
    void visit(const GT *op) override {
        visit_binary_op(op->a, op->b, "&gt;");
    }
    void visit(const GE *op) override {
        visit_binary_op(op->a, op->b, "&gt;=");
    }
    void visit(const EQ *op) override {
        visit_binary_op(op->a, op->b, "==");
    }

    void visit(const Min *op) override {
        stream << open_span("Min");
        print_list(symbol("min") + "(", {op->a, op->b}, ")");
        stream << close_span();
    }
    void visit(const Max *op) override {
        stream << open_span("Max");
        print_list(symbol("max") + "(", {op->a, op->b}, ")");
        stream << close_span();
    }
    void visit(const Not *op) override {
        stream << open_span("Not");
        stream << "!";
        print(op->a);
        stream << close_span();
    }
    void visit(const Select *op) override {
        stream << open_span("Select");
        print_list(symbol("select") + "(", {op->condition, op->true_value, op->false_value}, ")");
        stream << close_span();
    }
    void visit(const Load *op) override {
        stream << open_span("Load");
        stream << open_span("Matched");
        stream << var(op->name) << "[";
        stream << close_span();
        print(op->index);
        stream << matched("]");
        if (!is_const_one(op->predicate)) {
            stream << " " << keyword("if") << " ";
            print(op->predicate);
        }
        stream << close_span();
    }
    void visit(const Ramp *op) override {
        stream << open_span("Ramp");
        print_list(symbol("ramp") + "(", {op->base, op->stride, Expr(op->lanes)}, ")");
        stream << close_span();
    }
    void visit(const Broadcast *op) override {
        stream << open_span("Broadcast");
        stream << open_span("Matched");
        stream << symbol("x") << op->lanes << "(";
        stream << close_span();
        print(op->value);
        stream << matched(")");
        stream << close_span();
    }
    void visit(const Call *op) override {
        stream << open_span("Call");
        print_list(symbol(op->name) + "(", op->args, ")");
        stream << close_span();
    }

    void visit(const Let *op) override {
        bool in_context_before = in_context;
        in_context = false;

        scope.push(op->name, unique_id());
        stream << open_span("Let");
        stream << open_span("Matched");
        stream << "(" << keyword("let") << " ";
        stream << var(op->name);
        stream << close_span();
        stream << " " << matched("Operator Assign", "=") << " ";
        print(op->value);

        if (in_context) {
            curr_context.push_back(op->name);
        }
        in_context = in_context_before;

        stream << " " << matched("Keyword", "in") << " ";
        print(op->body);
        stream << matched(")");
        stream << close_span();
        scope.pop(op->name);

        remove_context(op->name);
    }
    void visit(const LetStmt *op) override {
        bool in_context_before = in_context;
        in_context = false;

        string scopeName = op->name;
        scope.push(scopeName, unique_id());
        // scope.push(op->name, unique_id());
        stream << open_div("LetStmt") << open_line();
        stream << cost_colors(op->value.get());

        stream << open_span("Matched");
        stream << keyword("let") << " ";
        stream << var(scopeName);
        stream << close_span();
        stream << " " << matched("Operator Assign", "=") << " ";

        stream << open_cost_span(op->value.get(), get_stmt_hierarchy(op->value.get()));
        print(op->value);
        stream << close_cost_span();

        if (in_context) {
            curr_context.push_back(scopeName);
            // curr_context.push_back(op->name);
        } else if (in_loop) {
            add_context_rule(curr_line_num);
        }
        in_context = in_context_before;

        stream << close_line();
        print(op->body);
        stream << close_div();

        // scope.pop(op->name);
        scope.pop(scopeName);

        remove_context(scopeName);
        // remove_context(op->name);
    }
    void visit(const AssertStmt *op) override {
        stream << open_div("AssertStmt WrapLine");
        std::vector<Expr> args;
        args.push_back(op->condition);
        args.push_back(op->message);
        print_list(symbol("assert") + "(", args, ")");
        stream << close_div();
    }
    void visit(const ProducerConsumer *op) override {
        scope.push(op->name, unique_id());
        stream << open_div(op->is_producer ? "Produce" : "Consumer");

        // anchoring
        producerConsumerCount++;
        string anchorName = "producerConsumer" + std::to_string(producerConsumerCount);

        int produce_id = unique_id();
        stream << open_span("Matched");
        stream << open_expand_button(produce_id);
        stream << open_anchor(anchorName);
        stream << keyword(op->is_producer ? "produce" : "consume") << " ";
        stream << var(op->name);
        stream << close_expand_button() << " {";
        stream << close_span();
        stream << close_anchor();

        stream << open_div(op->is_producer ? "ProduceBody Indent" : "ConsumeBody Indent",
                           produce_id);
        print(op->body);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
        scope.pop(op->name);
    }

    void visit(const For *op) override {
        bool in_loop_before = in_loop;

        vector<string> previous_context(curr_context);
        reset_context();
        curr_context.push_back(op->name);

        scope.push(op->name, unique_id());
        stream << open_div("For");

        // anchoring
        forCount++;
        string anchorName = "for" + std::to_string(forCount);

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_anchor(anchorName);
        stream << open_span("Matched");
        if (op->for_type == ForType::Serial) {
            stream << keyword("for");
        } else if (op->for_type == ForType::Parallel) {
            stream << keyword("parallel");
        } else if (op->for_type == ForType::Vectorized) {
            stream << keyword("vectorized");
        } else if (op->for_type == ForType::Unrolled) {
            stream << keyword("unrolled");
        } else if (op->for_type == ForType::GPUBlock) {
            stream << keyword("gpu_block");
        } else if (op->for_type == ForType::GPUThread) {
            stream << keyword("gpu_thread");
        } else if (op->for_type == ForType::GPULane) {
            stream << keyword("gpu_lane");
        } else {
            internal_error << "\n"
                           << "Unknown for type: " << ((int)op->for_type) << "\n\n";
        }
        stream << " (";
        stream << close_span();

        print_list({Variable::make(Int(32), op->name), op->min, op->extent});
        in_loop = true;

        stream << matched(")");
        stream << close_expand_button();
        stream << " " << matched("{");
        stream << close_anchor();
        stream << open_div("ForBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
        scope.pop(op->name);

        curr_context = previous_context;
        in_loop = in_loop_before;
    }

    void visit(const Acquire *op) override {
        stream << open_div("Acquire");
        int id = unique_id();
        stream << open_span("Matched");
        stream << open_expand_button(id);
        stream << keyword("acquire (");
        stream << close_span();
        print(op->semaphore);
        stream << ", ";
        print(op->count);
        stream << matched(")");
        stream << close_expand_button() << " {";
        stream << open_div("Acquire Indent", id);
        print(op->body);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
    }

    void visit(const Store *op) override {
        stream << open_div("Store WrapLine");

        // anchoring
        storeCount++;
        string anchorName = "store" + std::to_string(storeCount);
        stream << open_anchor(anchorName);

        stream << cost_colors(op->value.get());
        stream << open_cost_span(op, get_stmt_hierarchy(op));

        stream << open_span("Matched");
        stream << var(op->name) << "[";
        stream << close_span();

        print(op->index);
        stream << matched("]");
        stream << close_anchor();
        stream << " " << span("Operator Assign Matched", "=") << " ";

        stream << open_span("StoreValue");
        print(op->value);
        if (!is_const_one(op->predicate)) {
            stream << " " << keyword("if") << " ";
            print(op->predicate);
        }
        stream << close_span();

        if (!in_context && in_loop) {
            add_context_rule(curr_line_num);
        }

        stream << close_cost_span();
        stream << close_div();
    }
    void visit(const Provide *op) override {
        stream << open_span("Matched");
        stream << var(op->name) << "(";
        stream << close_span();
        print_list(op->args);
        stream << matched(")") << " ";
        stream << matched("=") << " ";
        if (op->values.size() > 1) {
            print_list("{", op->values, "}");
        } else {
            print(op->values[0]);
        }
        stream << close_div();
    }
    void visit(const Allocate *op) override {
        scope.push(op->name, unique_id());
        stream << open_div("Allocate");

        // anchoring
        allocateCount++;
        string anchorName = "allocate" + std::to_string(allocateCount);
        stream << open_anchor(anchorName);

        stream << cost_colors(op);

        stream << open_cost_span(op, get_stmt_hierarchy(op));

        stream << open_span("Matched");
        stream << keyword("allocate") << " ";
        stream << var(op->name) << "[";
        stream << close_span();

        stream << open_span("Type");
        stream << op->type;
        stream << close_span();

        for (const auto &extent : op->extents) {
            stream << " * ";
            print(extent);
        }
        stream << matched("]");
        if (!is_const_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        if (op->new_expr.defined()) {
            stream << open_span("Matched");
            stream << keyword("custom_new") << "{";
            print(op->new_expr);
            stream << open_div("ClosingBrace");
            stream << matched("}");
            stream << close_div();
        }
        if (!op->free_function.empty()) {
            stream << open_span("Matched");
            stream << keyword("custom_delete") << "{ " << op->free_function << "(); ";
            stream << open_div("ClosingBrace");
            stream << matched("}");
            stream << close_div();
        }
        stream << close_cost_span();

        if (!in_context && in_loop) {
            add_context_rule(curr_line_num);
        }
        stream << close_anchor();

        stream << open_div("AllocateBody");
        print(op->body);
        stream << close_div();
        stream << close_div();
        scope.pop(op->name);
    }
    void visit(const Free *op) override {
        stream << open_div("Free WrapLine");
        stream << keyword("free") << " ";
        stream << var(op->name);
        stream << close_div();
    }
    void visit(const Realize *op) override {
        scope.push(op->name, unique_id());
        stream << open_div("Realize");
        int id = unique_id();
        stream << open_expand_button(id);
        stream << keyword("realize") << " ";
        stream << var(op->name);
        stream << matched("(");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_list("[", {op->bounds[i].min, op->bounds[i].extent}, "]");
            if (i < op->bounds.size() - 1) {
                stream << ", ";
            }
        }
        stream << matched(")");
        if (!is_const_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        stream << close_expand_button();

        stream << " " << matched("{");
        stream << open_div("RealizeBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
        scope.pop(op->name);
    }

    void visit(const Prefetch *op) override {
        stream << open_span("Prefetch");
        stream << keyword("prefetch") << " ";
        stream << var(op->name);
        stream << matched("(");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_list("[", {op->bounds[i].min, op->bounds[i].extent}, "]");
            if (i < op->bounds.size() - 1) {
                stream << ", ";
            }
        }
        stream << matched(")");
        if (!is_const_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        stream << close_span();

        stream << open_div("PrefetchBody");
        print(op->body);
        stream << close_div();
    }

    // To avoid generating ridiculously deep DOMs, we flatten blocks here.
    void visit_block_stmt(const Stmt &stmt) {
        if (const Block *b = stmt.as<Block>()) {
            visit_block_stmt(b->first);
            visit_block_stmt(b->rest);
        } else if (stmt.defined()) {
            print(stmt);
        }
    }
    void visit(const Block *op) override {
        stream << open_div("Block");
        visit_block_stmt(op->first);
        visit_block_stmt(op->rest);
        stream << close_div();
    }

    // We also flatten forks
    void visit_fork_stmt(const Stmt &stmt) {
        if (const Fork *f = stmt.as<Fork>()) {
            visit_fork_stmt(f->first);
            visit_fork_stmt(f->rest);
        } else if (stmt.defined()) {
            stream << open_div("ForkTask");
            int id = unique_id();
            stream << open_expand_button(id);
            stream << matched("task {");
            stream << close_expand_button();
            stream << open_div("ForkTask Indent", id);
            print(stmt);
            stream << close_div();
            stream << open_div("ClosingBrace");
            stream << matched("}");
            stream << close_div();
            stream << close_div();
        }
    }
    void visit(const Fork *op) override {
        stream << open_div("Fork");
        int id = unique_id();
        stream << open_expand_button(id);
        stream << keyword("fork") << " " << matched("{");
        stream << close_expand_button();
        stream << open_div("Fork Indent", id);
        visit_fork_stmt(op->first);
        visit_fork_stmt(op->rest);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
    }

    void visit(const IfThenElse *op) override {
        stream << open_div("IfThenElse");

        // anchoring
        ifCount++;
        string anchorName = "if" + std::to_string(ifCount);

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_anchor(anchorName);
        stream << open_span("Matched");

        // for line numbers
        stream << open_span("IfSpan");
        stream << close_span();

        stream << keyword("if") << " (";
        stream << close_span();

        while (true) {
            print(op->condition);
            stream << matched(")");
            stream << close_expand_button() << " ";
            stream << matched("{");  // close if (or else if) span
            close_anchor();

            stream << open_div("ThenBody Indent", id);
            print(op->then_case);
            stream << close_div();  // close thenbody div

            if (!op->else_case.defined()) {
                stream << open_div("ClosingBrace");
                stream << matched("}");
                stream << close_div();
                break;
            }

            id = unique_id();

            if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
                stream << open_div("ClosingBrace");
                stream << matched("}");
                stream << close_div();

                stream << open_expand_button(id);
                stream << open_span("Matched");

                // for line numbers
                stream << open_span("IfSpan");
                stream << close_span();

                // anchoring
                ifCount++;
                string anchorName = "if" + std::to_string(ifCount);
                stream << open_anchor(anchorName);

                stream << keyword("else if") << " (";
                stream << close_span();
                op = nested_if;
            } else {
                stream << open_div("ClosingBrace");
                stream << matched("}");
                stream << close_div();

                stream << open_expand_button(id);

                // for line numbers
                stream << open_span("IfSpan");
                stream << close_span();

                // anchoring
                ifCount++;
                string anchorName = "if" + std::to_string(ifCount);
                stream << open_anchor(anchorName);

                stream << keyword("else");
                stream << close_expand_button() << "{";
                close_anchor();
                stream << close_span();
                stream << open_div("ElseBody Indent", id);
                print(op->else_case);
                stream << close_div();
                stream << open_div("ClosingBrace");
                stream << matched("}");
                stream << close_div();
                break;
            }
        }
        stream << close_div();  // Closing ifthenelse div.
    }

    void visit(const Evaluate *op) override {
        stream << open_div("Evaluate");
        stream << cost_colors(op);
        stream << open_cost_span(op, get_stmt_hierarchy(op));
        print(op->value);
        stream << close_cost_span();

        if (!in_context && in_loop) {
            add_context_rule(curr_line_num);
        }

        stream << close_div();
    }

    void visit(const Shuffle *op) override {
        stream << open_span("Shuffle");
        if (op->is_concat()) {
            print_list(symbol("concat_vectors("), op->vectors, ")");
        } else if (op->is_interleave()) {
            print_list(symbol("interleave_vectors("), op->vectors, ")");
        } else if (op->is_extract_element()) {
            std::vector<Expr> args = op->vectors;
            args.emplace_back(op->slice_begin());
            print_list(symbol("extract_element("), args, ")");
        } else if (op->is_slice()) {
            std::vector<Expr> args = op->vectors;
            args.emplace_back(op->slice_begin());
            args.emplace_back(op->slice_stride());
            args.emplace_back(static_cast<int>(op->indices.size()));
            print_list(symbol("slice_vectors("), args, ")");
        } else {
            std::vector<Expr> args = op->vectors;
            for (int i : op->indices) {
                args.emplace_back(i);
            }
            print_list(symbol("shuffle("), args, ")");
        }
        stream << close_span();
    }

    void visit(const VectorReduce *op) override {
        stream << open_span("VectorReduce");
        stream << open_span("Type") << op->type << close_span();
        print_list(symbol("vector_reduce") + "(", {op->op, op->value}, ")");
        stream << close_span();
    }

    void visit(const Atomic *op) override {
        stream << open_div("Atomic");
        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_span("Matched");
        if (op->mutex_name.empty()) {
            stream << keyword("atomic") << matched("{");
        } else {
            stream << keyword("atomic") << " (";
            stream << symbol(op->mutex_name);
            stream << ")" << matched("{");
        }
        stream << close_span();
        stream << open_div("Atomic Body Indent", id);
        print(op->body);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
    }

public:
    FindStmtCost generate_costs(const Module &m) {
        findStmtCost.generate_costs(m);
        return findStmtCost;
    }
    FindStmtCost generate_costs(const Stmt &s) {
        findStmtCost.generate_costs(s);
        return findStmtCost;
    }
    void generate_producer_consumer_hierarchy(const Module &m) {

        string prodConsHTML = producerConsumerHierarchy.generate_producer_consumer_html(m);
        if (PRINT_PROD_CONS) cout << prodConsHTML << endl;

        stream << prodConsHTML;
    }
    void generate_producer_consumer_hierarchy(const Stmt &s) {

        string prodConsHTML = producerConsumerHierarchy.generate_producer_consumer_html(s);
        if (PRINT_PROD_CONS) cout << prodConsHTML << endl;

        stream << prodConsHTML;
    }
    void generate_dependency_graph(const Module &m) {

        string dependGraphHTML = dependencyGraph.generate_dependency_graph(m);
        if (PRINT_DEPENDENCIES) cout << dependGraphHTML << endl;

        // stream << dependGraphHTML;
        stream << "In construction...\n";
    }
    void generate_dependency_graph(const Stmt &s) {
        internal_error << "\n"
                       << "\n"
                       << "StmtToViz::generate_dependency_graph(const Stmt &s): Not implemented"
                       << "\n\n";

        // TODO: fill this in

        // Stmt inlined_s = substitute_in_all_lets(s);
        // string dependGraphHTML = dependencyGraph.generate_dependency_graph(inlined_s);
        string dependGraphHTML = dependencyGraph.generate_dependency_graph(s);
    }

    void print(const Expr &ir) {
        // debug(0) << "entering: " << ir << "\n";
        // cout << "done entering" << endl;
        ir.accept(this);
        // debug(0) << "exiting: " << ir << "\n";
        // cout << "done exiting" << endl;
    }

    void print(const Stmt &ir) {
        // debug(0) << "entering: " << ir << "\n";
        // cout << "done entering" << endl;
        ir.accept(this);
        // debug(0) << "exiting: " << ir << "\n";
        // cout << "done exiting" << endl;
    }

    void print(const LoweredFunc &op) {
        scope.push(op.name, unique_id());
        stream << open_div("Function");

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_span("Matched");
        stream << keyword("func");
        stream << " " << op.name << "(";
        stream << close_span();
        for (size_t i = 0; i < op.args.size(); i++) {
            if (i > 0) {
                stream << matched(",") << " ";
            }
            stream << var(op.args[i].name);
        }
        stream << matched(")");
        stream << close_expand_button();
        stream << " " << matched("{");
        stream << open_div("FunctionBody Indent", id);

        // Stmt inlined_body = substitute_in_all_lets(op.body);
        // print(inlined_body);
        print(op.body);

        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();

        stream << close_div();
        scope.pop(op.name);
    }

    void print_cuda_gpu_source_kernels(const string &str) {
        std::istringstream ss(str);
        int current_id = -1;
        stream << "<code class='ptx'>";
        bool in_braces = false;
        bool in_func_signature = false;
        string current_kernel;
        for (string line; std::getline(ss, line);) {
            if (line.empty()) {
                stream << "\n";
                continue;
            }
            line = replace_all(line, "&", "&amp;");
            line = replace_all(line, "<", "&lt;");
            line = replace_all(line, ">", "&gt;");
            line = replace_all(line, "\"", "&quot;");
            line = replace_all(line, "/", "&#x2F;");
            line = replace_all(line, "'", "&#39;");

            if (starts_with(line, ".visible .entry")) {
                std::vector<string> parts = split_string(line, " ");
                if (parts.size() == 3) {
                    in_func_signature = true;
                    current_id = unique_id();
                    stream << open_expand_button(current_id);

                    string kernel_name = parts[2].substr(0, parts[2].length() - 1);
                    line = keyword(".visible") + " " + keyword(".entry") + " ";
                    line += var(kernel_name) + " " + matched("(");
                    current_kernel = kernel_name;
                }
            } else if (starts_with(line, ")") && in_func_signature) {
                stream << close_expand_button();
                in_func_signature = false;
                line = matched(")") + line.substr(1);
            } else if (starts_with(line, "{") && !in_braces) {
                in_braces = true;
                stream << matched("{");
                stream << close_expand_button();
                internal_assert(current_id != -1);
                stream << open_div("Indent", current_id);
                current_id = -1;
                line = line.substr(1);
                scope.push(current_kernel, unique_id());
            } else if (starts_with(line, "}") && in_braces) {
                stream << close_div();
                line = matched("}") + line.substr(1);
                in_braces = false;
                scope.pop(current_kernel);
            }

            bool indent = false;

            if (line[0] == '\t') {
                // Replace first tab with four spaces.
                line = line.substr(1);
                indent = true;
            }

            line = replace_all(line, ".f32", ".<span class='OpF32'>f32</span>");
            line = replace_all(line, ".f64", ".<span class='OpF64'>f64</span>");

            line = replace_all(line, ".s8", ".<span class='OpI8'>s8</span>");
            line = replace_all(line, ".s16", ".<span class='OpI16'>s16</span>");
            line = replace_all(line, ".s32", ".<span class='OpI32'>s32</span>");
            line = replace_all(line, ".s64", ".<span class='OpI64'>s64</span>");

            line = replace_all(line, ".u8", ".<span class='OpI8'>u8</span>");
            line = replace_all(line, ".u16", ".<span class='OpI16'>u16</span>");
            line = replace_all(line, ".u32", ".<span class='OpI32'>u32</span>");
            line = replace_all(line, ".u64", ".<span class='OpI64'>u64</span>");

            line = replace_all(line, ".b8", ".<span class='OpB8'>b8</span>");
            line = replace_all(line, ".b16", ".<span class='OpB16'>b16</span>");
            line = replace_all(line, ".b32", ".<span class='OpB32'>b32</span>");
            line = replace_all(line, ".b64", ".<span class='OpB64'>b64</span>");

            line = replace_all(line, ".v2", ".<span class='OpVec2'>v2</span>");
            line = replace_all(line, ".v4", ".<span class='OpVec4'>v4</span>");

            line = replace_all(line, "ld.", "<span class='Memory'>ld</span>.");
            line = replace_all(line, "st.", "<span class='Memory'>st</span>.");

            size_t idx;
            if ((idx = line.find("&#x2F;&#x2F")) != string::npos) {
                line.insert(idx, "<span class='Comment'>");
                line += "</span>";
            }

            // Predicated instructions
            if (line.front() == '@' && indent) {
                idx = line.find(' ');
                string pred = line.substr(1, idx - 1);
                line = "<span class='Pred'>@" + var(pred) + "</span>" + line.substr(idx);
            }

            // Labels
            if (line.front() == 'L' && !indent && (idx = line.find(':')) != string::npos) {
                string label = line.substr(0, idx);
                line = "<span class='Label'>" + var(label) + "</span>:" + line.substr(idx + 1);
            }

            // Highlight operands
            if ((idx = line.find(" \t")) != string::npos && line.back() == ';') {
                string operands_str = line.substr(idx + 2);
                operands_str = operands_str.substr(0, operands_str.length() - 1);
                std::vector<string> operands = split_string(operands_str, ", ");
                operands_str = "";
                for (size_t opidx = 0; opidx < operands.size(); ++opidx) {
                    string op = operands[opidx];
                    internal_assert(!op.empty());
                    if (opidx != 0) {
                        operands_str += ", ";
                    }
                    if (op.back() == '}') {
                        string reg = op.substr(0, op.size() - 1);
                        operands_str += var(reg) + '}';
                    } else if (op.front() == '%') {
                        operands_str += var(op);
                    } else if (op.find_first_not_of("-0123456789") == string::npos) {
                        operands_str += open_span("IntImm Imm");
                        operands_str += op;
                        operands_str += close_span();
                    } else if (starts_with(op, "0f") &&
                               op.find_first_not_of("0123456789ABCDEF", 2) == string::npos) {
                        operands_str += open_span("FloatImm Imm");
                        operands_str += op;
                        operands_str += close_span();
                    } else if (op.front() == '[' && op.back() == ']') {
                        size_t idx = op.find('+');
                        if (idx == string::npos) {
                            string reg = op.substr(1, op.size() - 2);
                            operands_str += '[' + var(reg) + ']';
                        } else {
                            string reg = op.substr(1, idx - 1);
                            string offset = op.substr(idx + 1);
                            offset = offset.substr(0, offset.size() - 1);
                            operands_str += '[' + var(reg) + "+";
                            operands_str += open_span("IntImm Imm");
                            operands_str += offset;
                            operands_str += close_span();
                            operands_str += ']';
                        }
                    } else if (op.front() == '{') {
                        string reg = op.substr(1);
                        operands_str += '{' + var(reg);
                    } else if (op.front() == 'L') {
                        // Labels
                        operands_str += "<span class='Label'>" + var(op) + "</span>";
                    } else {
                        operands_str += op;
                    }
                }
                operands_str += ";";
                line = line.substr(0, idx + 2) + operands_str;
            }

            if (indent) {
                stream << "    ";
            }
            stream << line << "\n";
        }
        stream << "</code>";
    }

    void print(const Buffer<> &op) {
        bool include_data = ends_with(op.name(), "_gpu_source_kernels");
        int id = 0;
        if (include_data) {
            id = unique_id();
            stream << open_expand_button(id);
        }
        stream << open_div("Buffer<>");
        stream << keyword("buffer ") << var(op.name());
        if (include_data) {
            stream << " = ";
            stream << matched("{");
            stream << close_expand_button();
            stream << open_div("BufferData Indent", id);
            string str((const char *)op.data(), op.size_in_bytes());
            if (starts_with(op.name(), "cuda_")) {
                print_cuda_gpu_source_kernels(str);
            } else {
                stream << "<pre>\n";
                stream << str;
                stream << "</pre>\n";
            }
            stream << close_div();

            stream << " ";
            internal_error
                << "\n\n\nlook at this line!!! make sure the closing brace is correct! \n\n\n";
            stream << open_div("ClosingBrace");
            stream << matched("}");
            stream << close_div();
        }
        stream << close_div();
    }

    void print(const Module &m) {
        scope.push(m.name(), unique_id());
        for (const auto &s : m.submodules()) {
            print(s);
        }

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_div("Module");
        stream << open_span("Matched");
        stream << keyword("module") << " name=" << m.name()
               << ", target=" << m.target().to_string();
        stream << close_span();
        stream << close_expand_button();
        stream << " " << matched("{");

        stream << open_div("ModuleBody Indent", id);

        for (const auto &b : m.buffers()) {
            print(b);
        }

        // print main function first
        for (const auto &f : m.functions()) {
            if (f.name == m.name()) {
                print(f);
            }
        }

        // print the rest of the functions
        for (const auto &f : m.functions()) {
            if (f.name != m.name()) {
                print(f);
            }
        }

        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
        scope.pop(m.name());
    }

    void start_stream(const string &filename) {
        stream.open(filename.c_str());
        stream << "<head>";

        // bootstrap links
        stream << "<!-- Bootstrap links -->\n";
        stream << "<link "
                  "href='https://cdn.jsdelivr.net/npm/bootstrap@5.2.0/dist/css/bootstrap.min.css' "
                  "rel='stylesheet' "
                  "integrity='sha384-gH2yIJqKdNHPEq0n4Mqa/HGKIhSkIHeL5AyhkYV8i59U5AR6csBvApHHNl/"
                  "vI1Bx' crossorigin='anonymous'>\n";
        stream
            << "<script "
               "src='https://cdn.jsdelivr.net/npm/bootstrap@5.2.0/dist/js/bootstrap.bundle.min.js' "
               "integrity='sha384-A3rJD856KowSb7dwlZdYEkO39Gagi7vIsF0jrRAoQmDKKtQBHUuLZ9AsSv4jD4Xa'"
               " crossorigin='anonymous'></script>\n";
        stream
            << "<link rel='stylesheet' "
               "href='https://cdn.jsdelivr.net/npm/bootstrap@5.0.2/dist/css/bootstrap.min.css'>\n";
        stream << "<link rel='stylesheet' "
                  "href='https://cdn.jsdelivr.net/npm/bootstrap-icons@1.5.0/font/"
                  "bootstrap-icons.css'>\n";
        stream << "\n";

        //   tooltip links
        stream << "<!-- Tooltip links -->\n";
        stream << "<script src='https://cdn.jsdelivr.net/npm/@floating-ui/core@1.0.1'></script>";
        stream << "<script src='https://cdn.jsdelivr.net/npm/@floating-ui/dom@1.0.1'></script>";
        stream << "\n";

        // hierarchy links
        stream << "<!-- Hierarchy links -->\n";
        stream << "<link rel='stylesheet' href='https://unpkg.com/treeflex/dist/css/treeflex.css'>";
        stream << "\n";

        // expand button links
        stream << "<!-- Expand Button links -->\n";
        stream << "<link "
                  "href='http://maxcdn.bootstrapcdn.com/font-awesome/4.1.0/css/"
                  "font-awesome.min.css' rel='stylesheet'>\n";
        stream << "<script src='http://code.jquery.com/jquery-1.10.2.js'></script>\n";
        stream << "\n";

        stream << "<style type='text/css'>";
        stream << css;
        stream << vizCss;
        stream << costColorsCSS;
        stream << ProducerConsumerHierarchy::prodConsCSS;
        if (NAVIGATION_STYLE) stream << navigationCSS;
        else
            stream << scrollToCSS;
        stream << lineNumbersCSS;
        stream << tooltipCSS;
        stream << GetStmtHierarchy::stmtHierarchyCSS;
        stream << "</style>\n";
        stream << "<script language='javascript' type='text/javascript'>" + js + "</script>\n";
        stream << "</head>\n <body>\n";
        if (NAVIGATION_STYLE) stream << navigationHTML;
    }

    void end_stream() {
        cout << "in destructor" << endl;
        stream << popups;
        cout << "here" << endl;

        stream << "<script>\n";
        stream << "$( '.Matched' ).each( function() {\n"
               << "    this.onmouseover = function() { $('.Matched[id^=' + this.id.split('-')[0] + "
                  "'-]').addClass('Highlight'); }\n"
               << "    this.onmouseout = function() { $('.Matched[id^=' + this.id.split('-')[0] + "
                  "'-]').removeClass('Highlight'); }\n"
               << "} );\n";

        cout << "here5" << endl;
        stream << content_rule_script_stream;
        cout << "here6" << endl;
        stream << generatetooltipJS(tooltipCount);
        cout << "here7" << endl;
        stream << getStmtHierarchy.generate_collapse_expand_js();
        cout << "here8" << endl;
        stream << producerConsumerHierarchy.generate_prodCons_js();
        cout << "here9" << endl;
        stream << ProducerConsumerHierarchy::scrollToFunctionJS;
        if (NAVIGATION_STYLE) stream << expandCodeJS;
        cout << "here10" << endl;
        stream << "</script>\n";
        cout << "here11" << endl;
        stream << "</body>";
        cout << "here12" << endl;
    }

    void navigation_header() {
        stream << "<div class='tab-content' id='myTabContent'>\n";
    }
    void navigation_footer() {
        stream << "</div>\n";
    }
    void open_code_navigation() {
        stream << "<div class='tab-pane fade show active' id='IRCode' role='tabpanel' "
                  "aria-labelledby='IRCode-tab'>\n";
    }
    void close_code_navigation() {
        stream << "</div>\n";
    }

    void open_prod_cons_navigation() {
        stream << "<div class='tab-pane fade' id='ProdCons' role='tabpanel' "
                  "aria-labelledby='ProdCons-tab'>\n";
    }
    void close_prod_cons_navigation() {
        stream << "</div>\n";
    }
    void open_dependency_navigation() {
        stream << "<div class='tab-pane fade' id='Dependency' role='tabpanel' "
                  "aria-labelledby='Dependency-tab'>\n";
    }
    void close_dependency_navigation() {
        stream << "</div>\n";
    }

    void generate_html(const string &filename, const Module &m) {
        // opening parts of the html
        start_stream(filename);

        if (NAVIGATION_STYLE) navigation_header();
        else {
            stream << "<div class='outerDiv'>\n";

            stream << "<div class='buttons'>\n";
            stream
                << "<button class='info-button' onclick='expandCodeDiv()'>Expand Code</button>\n";
            stream << "<button class='info-button' onclick='expandVizDiv()'>";
            stream << "Expand Visualization</button>\n";
            stream << "<button class='info-button' onclick='resetRatio()'>Reset Ratio</button>\n";
            stream << "</div>\n";

            stream << "<div class='mainContent'>\n";
        }

        // print main html page
        if (NAVIGATION_STYLE) open_code_navigation();
        stream << "<div class='IRCode-code' id='IRCode-code'>\n";
        print(m);
        stream << "</div>\n";  // close IRCode-code div
        if (NAVIGATION_STYLE) close_code_navigation();

        if (NAVIGATION_STYLE) open_prod_cons_navigation();
        stream << "<div class='ProducerConsumerViz' id='ProducerConsumerViz'>\n";
        generate_producer_consumer_hierarchy(m);
        stream << "</div>\n";  // close ProducerConsumerViz div
        if (NAVIGATION_STYLE) close_prod_cons_navigation();

        if (NAVIGATION_STYLE) open_dependency_navigation();
        if (NAVIGATION_STYLE) stream << "<div class='DependencyViz'>\n";
        if (NAVIGATION_STYLE) generate_dependency_graph(m);
        if (NAVIGATION_STYLE) stream << "</div>\n";  // close DependencyViz div
        if (NAVIGATION_STYLE) close_dependency_navigation();

        if (NAVIGATION_STYLE) navigation_footer();
        else {
            stream << "</div>\n";  // close mainContent div
            stream << "</div>\n";  // close outerDiv div
        }

        // closing parts of the html
        end_stream();
    }

    StmtToViz(const string &filename, const Module &m)
        : getStmtHierarchy(generate_costs(m)),
          producerConsumerHierarchy(get_file_name(filename), findStmtCost), id_count(0),
          in_loop(false), context_stack(1, 0) {
    }

    StmtToViz(const string &filename, const Stmt &s)
        : getStmtHierarchy(generate_costs(s)),
          producerConsumerHierarchy(get_file_name(filename), findStmtCost), id_count(0),
          in_loop(false), context_stack(1, 0) {
    }

    ~StmtToViz() = default;

    string generatetooltipJS(int &tooltipCount) {
        stringstream tooltipJS;
        tooltipJS << "\n// Tooltip JS\n";
        tooltipJS << "function update(buttonElement, tooltipElement) { \n";
        tooltipJS << "    window.FloatingUIDOM.computePosition(buttonElement, tooltipElement, { \n";
        tooltipJS << "        placement: 'top', \n";
        tooltipJS << "        middleware: [ \n";
        tooltipJS << "            window.FloatingUIDOM.offset(6), \n";
        tooltipJS << "            window.FloatingUIDOM.flip(), \n";
        tooltipJS << "            window.FloatingUIDOM.shift({ padding: 5 }), \n";
        tooltipJS << "        ], \n";
        tooltipJS << "    }).then(({ x, y, placement, middlewareData }) => { \n";
        tooltipJS << "        Object.assign(tooltipElement.style, { \n";
        tooltipJS << "            left: `${x}px`, \n";
        tooltipJS << "            top: `${y}px`, \n";
        tooltipJS << "        }); \n";
        tooltipJS << "        // Accessing the data \n";
        tooltipJS << "        const staticSide = { \n";
        tooltipJS << "            top: 'bottom', \n";
        tooltipJS << "            right: 'left', \n";
        tooltipJS << "            bottom: 'top', \n";
        tooltipJS << "            left: 'right', \n";
        tooltipJS << "        }[placement.split('-')[0]]; \n";
        tooltipJS << "    }); \n";
        tooltipJS << "} \n";
        tooltipJS << "function showTooltip(buttonElement, tooltipElement) { \n";
        tooltipJS << "    tooltipElement.style.display = 'block'; \n";
        tooltipJS << "    tooltipElement.style.opacity = '1'; \n";
        tooltipJS << "    update(buttonElement, tooltipElement); \n";
        tooltipJS << "} \n";
        tooltipJS << "function hideTooltip(tooltipElement) { \n";
        tooltipJS << "    tooltipElement.style.display = ''; \n";
        tooltipJS << "    tooltipElement.style.opacity = '0'; \n";
        tooltipJS << "} \n";
        tooltipJS << "for (let i = 1; i <= " << tooltipCount << "; i++) { \n";
        tooltipJS << "    const button = document.querySelector('#button' + i); \n";
        tooltipJS << "    const tooltip = document.querySelector('#tooltip' + i); \n";
        tooltipJS << "    button.addEventListener('mouseenter', () => { \n";
        tooltipJS << "        showTooltip(button, tooltip); \n";
        tooltipJS << "    }); \n";
        tooltipJS << "    button.addEventListener('mouseleave', () => { \n";
        tooltipJS << "        hideTooltip(tooltip); \n";
        tooltipJS << "    } \n";
        tooltipJS << "    ); \n";
        tooltipJS << "    tooltip.addEventListener('focus', () => { \n";
        tooltipJS << "        showTooltip(button, tooltip); \n";
        tooltipJS << "    } \n";
        tooltipJS << "    ); \n";
        tooltipJS << "    tooltip.addEventListener('blur', () => { \n";
        tooltipJS << "        hideTooltip(tooltip); \n";
        tooltipJS << "    } \n";
        tooltipJS << "    ); \n";
        tooltipJS << "} \n";

        return tooltipJS.str();
    }
};

const string StmtToViz::css = "\n \
/* Normal CSS */\n \
body { font-family: Consolas, 'Liberation Mono', Menlo, Courier, monospace; font-size: 12px; background: #f8f8f8; margin-left:15px; } \n \
a, a:hover, a:visited, a:active { color: inherit; text-decoration: none; } \n \
b { font-weight: normal; }\n \
p.WrapLine { margin: 0px; margin-left: 30px; text-indent:-30px; } \n \
div.WrapLine { margin-left: 30px; text-indent:-30px; } \n \
div.Indent { padding-left: 15px; }\n \
div.ShowHide { position:absolute; left:-12px; width:12px; height:12px; } \n \
span.Comment { color: #998; font-style: italic; }\n \
span.Keyword { color: #333; font-weight: bold; }\n \
span.Assign { color: #d14; font-weight: bold; }\n \
span.Symbol { color: #990073; }\n \
span.Type { color: #445588; font-weight: bold; }\n \
span.StringImm { color: #d14; }\n \
span.IntImm { color: #099; }\n \
span.FloatImm { color: #099; }\n \
b.Highlight { font-weight: bold; background-color: #DDD; }\n \
span.Highlight { font-weight: bold; background-color: #FF0; }\n \
span.OpF32 { color: hsl(106deg 100% 40%); font-weight: bold; }\n \
span.OpF64 { color: hsl(106deg 100% 30%); font-weight: bold; }\n \
span.OpB8  { color: hsl(208deg 100% 80%); font-weight: bold; }\n \
span.OpB16 { color: hsl(208deg 100% 70%); font-weight: bold; }\n \
span.OpB32 { color: hsl(208deg 100% 60%); font-weight: bold; }\n \
span.OpB64 { color: hsl(208deg 100% 50%); font-weight: bold; }\n \
span.OpI8  { color: hsl( 46deg 100% 45%); font-weight: bold; }\n \
span.OpI16 { color: hsl( 46deg 100% 40%); font-weight: bold; }\n \
span.OpI32 { color: hsl( 46deg 100% 34%); font-weight: bold; }\n \
span.OpI64 { color: hsl( 46deg 100% 27%); font-weight: bold; }\n \
span.OpVec2 { background-color: hsl(100deg 100% 90%); font-weight: bold; }\n \
span.OpVec4 { background-color: hsl(100deg 100% 80%); font-weight: bold; }\n \
span.Memory { color: #d22; font-weight: bold; }\n \
span.Pred { background-color: #ffe8bd; font-weight: bold; }\n \
span.Label { background-color: #bde4ff; font-weight: bold; }\n \
code.ptx { tab-size: 26; white-space: pre; }\n \
";

const string StmtToViz::scrollToCSS = "\n \
/* Scroll to CSS */\n \
div.buttons { \n \
    padding-left: 20px; \n \
    padding-top: 20px; \n \
    padding-bottom: 10px; \n \
    border-bottom: grey dashed 1px; \n \
} \n \
div.outerDiv { \n \
    height: 100vh; \n \
    display: flex; \n \
    flex-direction: column; \n \
} \n \
div.mainContent { \n \
    display: flex; \n \
    flex-grow: 1; \n \
    width: 100%; \n \
    overflow: hidden; \n \
} \n \
div.IRCode-code { \n \
    counter-reset: line; \n \
    padding-left: 40px; \n \
    padding-top: 20px; \n \
    flex: 0 50%; \n \
    overflow-y: scroll; \n \
    position: relative; \n \
 \n \
} \n \
div.ProducerConsumerViz { \n \
    flex: 0 50%; \n \
    overflow-y: scroll; \n \
    padding-top: 20px; \n \
} \n \
";

const string StmtToViz::navigationCSS = "\n \
div.IRCode-code { \n \
    counter-reset: line; \n \
    margin-left: 40px; \n \
    margin-top: 20px; \n \
} \n \
div.ProducerConsumerViz { \n \
    margin-left: 20px; \n \
    margin-top: 20px; \n \
} \n \
div.DependencyViz { \n \
    margin-left: 20px; \n \
    margin-top: 20px; \n \
} \n \
";

const string StmtToViz::lineNumbersCSS = "\n \
/* Line Numbers CSS */\n \
p.WrapLine,\n\
div.WrapLine,\n\
div.Consumer,\n\
div.Produce,\n\
div.For,\n\
span.IfSpan,\n\
div.Evaluate,\n\
div.Allocate,\n\
div.ClosingBrace,\n\
div.Module,\n\
div.Function {\n\
    counter-increment: line;\n\
}\n\
p.WrapLine:before,\n\
div.WrapLine:before {\n\
    content: counter(line) '. ';\n\
    display: inline-block;\n\
    position: absolute;\n\
    left: 40px;\n\
    color: rgb(175, 175, 175);\n\
    user-select: none;\n\
    -webkit-user-select: none;\n\
}\n\
div.Consumer:before,\n\
div.Produce:before,\n\
div.For:before,\n\
span.IfSpan:before,\n\
div.Evaluate:before,\n\
div.Allocate:before, \n\
div.ClosingBrace:before,\n\
div.Module:before, \n\
div.Function:before {\n\
    content: counter(line) '. ';\n\
    display: inline-block;\n\
    position: absolute;\n\
    left: 10px;\n\
    color: rgb(175, 175, 175);\n\
    user-select: none;\n\
    -webkit-user-select: none;\n\
}\n\
";

const string StmtToViz::vizCss = "\n \
/* Additional Code Visualization CSS */\n \
span.ButtonSpacer { width: 5px; color: transparent; display: inline-block; }\n \
.info-button { \n \
    background-color: #fff; \n \
    border: 1px solid #d5d9d9; \n \
    border-radius: 8px; \n \
    box-shadow: rgba(213, 217, 217, .5) 0 2px 5px 0; \n \
    box-sizing: border-box; \n \
    display: inline-block; \n \
    position: relative; \n \
    text-align: center; \n \
    text-decoration: none; \n \
    -webkit-user-select: none; \n \
    user-select: none; \n \
    touch-action: manipulation; \n \
    vertical-align: middle; \n \
    margin-right: 5px; \n \
    font-size: 15px; \n \
} \n \
.info-button:hover, .see-code-button:hover { \n \
    background-color: #f7fafa; \n \
} \n \
.see-code-button { \n \
    background-color: #fff; \n \
    border: transparent; \n \
    display: inline-block; \n \
    position: relative; \n \
    margin-left: 5px; \n \
    font-size: 20px; \n \
    padding: 5px; \n \
    vertical-align: middle; \n \
    border-radius: 8px; \n \
    box-shadow: rgba(213, 217, 217, .5) 0 2px 5px 0; \n \
    box-sizing: border-box; \n \
    text-align: center; \n \
    -webkit-user-select: none; \n \
    user-select: none; \n \
    touch-action: manipulation; \n \
} \n \
";

const string StmtToViz::costColorsCSS = "\n \
/* Cost Colors CSS */\n \
span.CostColor19, div.CostColor19, .CostColor19 { background-color: rgb(130,31,27); } \n \
span.CostColor18, div.CostColor18, .CostColor18 { background-color: rgb(145,33,30); } \n \
span.CostColor17, div.CostColor17, .CostColor17 { background-color: rgb(160,33,32); } \n \
span.CostColor16, div.CostColor16, .CostColor16 { background-color: rgb(176,34,34); } \n \
span.CostColor15, div.CostColor15, .CostColor15 { background-color: rgb(185,47,32); } \n \
span.CostColor14, div.CostColor14, .CostColor14 { background-color: rgb(193,59,30); } \n \
span.CostColor13, div.CostColor13, .CostColor13 { background-color: rgb(202,71,27); } \n \
span.CostColor12, div.CostColor12, .CostColor12 { background-color: rgb(210,82,22); } \n \
span.CostColor11, div.CostColor11, .CostColor11 { background-color: rgb(218,93,16); } \n \
span.CostColor10, div.CostColor10, .CostColor10 { background-color: rgb(226,104,6); } \n \
span.CostColor9, div.CostColor9, .CostColor9 { background-color: rgb(229,118,9); } \n \
span.CostColor8, div.CostColor8, .CostColor8 { background-color: rgb(230,132,15); } \n \
span.CostColor7, div.CostColor7, .CostColor7 { background-color: rgb(231,146,20); } \n \
span.CostColor6, div.CostColor6, .CostColor6 { background-color: rgb(232,159,25); } \n \
span.CostColor5, div.CostColor5, .CostColor5 { background-color: rgb(233,172,30); } \n \
span.CostColor4, div.CostColor4, .CostColor4 { background-color: rgb(233,185,35); } \n \
span.CostColor3, div.CostColor3, .CostColor3 { background-color: rgb(233,198,40); } \n \
span.CostColor2, div.CostColor2, .CostColor2 { background-color: rgb(232,211,45); } \n \
span.CostColor1, div.CostColor1, .CostColor1 { background-color: rgb(231,223,50); } \n \
span.CostColor0, div.CostColor0, .CostColor0 { background-color: rgb(236,233,89); } \n \
span.CostColorSpacer { width: 2px; color: transparent; display: inline-block; }\n \
span.CostComputation { width: 13px; display: inline-block; color: transparent; } \n \
span.CostMovement { width: 13px; display: inline-block;  color: transparent; } \n \
";

const string StmtToViz::navigationHTML = "\n \
<ul class='nav nav-tabs' id='myTab' role='tablist'> \n \
    <li class='nav-item' role='presentation'> \n \
        <button class='nav-link active' id='IRCode-tab' data-bs-toggle='tab' \n \
            data-bs-target='#IRCode' type='button' role='tab' \n \
            aria-controls='IRCode' aria-selected='true'>IR Code</button> \n \
    </li> \n \
    <li class='nav-item' role='presentation'> \n \
        <button class='nav-link' id='ProdCons-tab' data-bs-toggle='tab' \n \
            data-bs-target='#ProdCons' type='button' role='tab' \n \
            aria-controls='ProdCons' aria-selected='false'>Producer/Consumer \n \
            Visualization</button> \n \
    </li> \n \
    <li class='nav-item' role='presentation'> \n \
        <button class='nav-link' id='Dependency-tab' data-bs-toggle='tab' \n \
            data-bs-target='#Dependency' type='button' role='tab' \n \
            aria-controls='Dependency' aria-selected='false'>Dependency \n \
            Graph</button> \n \
    </li> \n \
</ul> \n \
";

const string StmtToViz::tooltipCSS = "\n \
/* Tooltip CSS */\n \
.left-table { text-align: right; color: grey; vertical-align: middle; font-size: 12px; }\n \
.right-table { text-align: left; vertical-align: middle; font-size: 12px; font-weight: bold; padding-left: 3px; }\n \
.tooltipTable { border: 0px; } \n \
.tooltip { \n \
    display: none; \n \
    position: absolute; \n \
    top: 0; \n \
    left: 0; \n \
    background: white; \n \
    padding: 5px; \n \
    font-size: 90%; \n \
    pointer-events: none; \n \
    border-radius: 5px; \n \
    border: 1px dashed #aaa; \n \
    z-index: 9999; \n \
    box-shadow: rgba(100, 100, 100, 0.8) 0 2px 5px 0; \n \
} \n \
.conditionTooltip { \n \
    width: 300px; \n \
    padding: 5px; \n \
    font-family: Consolas, 'Liberation Mono', Menlo, Courier, monospace; \n \
} \n \
";

const string StmtToViz::expandCodeJS = "\n \
/* expand code div */\n \
function expandCodeDiv() { \n \
    var codeDiv = document.getElementById('IRCode-code'); \n \
    var prodConsDiv = document.getElementById('ProducerConsumerViz'); \n \
    codeDiv.style.flex = '0 75%'; \n \
    prodConsDiv.style.flex = '0 25%'; \n \
} \n \
function expandVizDiv() { \n \
    var codeDiv = document.getElementById('IRCode-code'); \n \
    var prodConsDiv = document.getElementById('ProducerConsumerViz'); \n \
    console.log(codeDiv.style.flex); \n \
    codeDiv.style.flex = '0 25%'; \n \
    prodConsDiv.style.flex = '0 75%'; \n \
} \n \
function resetRatio() { \n \
    var codeDiv = document.getElementById('IRCode-code'); \n \
    var prodConsDiv = document.getElementById('ProducerConsumerViz'); \n \
    codeDiv.style.flex = '0 50%'; \n \
    prodConsDiv.style.flex = '0 50%'; \n \
} \n \
resetRatio(); \n \
";

const string StmtToViz::js = "\n \
/* Expand/Collapse buttons */\n \
function toggle(id) { \n \
    e = document.getElementById(id); \n \
    show = document.getElementById(id + '-show'); \n \
    hide = document.getElementById(id + '-hide'); \n \
    if (e.style.visibility != 'hidden') { \n \
        e.style.height = '0px'; \n \
        e.style.visibility = 'hidden'; \n \
        show.style.display = 'block'; \n \
        hide.style.display = 'none'; \n \
    } else { \n \
        e.style = ''; \n \
        show.style.display = 'none'; \n \
        hide.style.display = 'block'; \n \
    } \n \
    return false; \n \
}\n \
function openNewWindow(innerHtml) { \n \
    var newWindow = window.open('', '_blank'); \n \
    newWindow.document.write(innerHtml); \n \
}\n \
";
}  // namespace

void print_to_viz(const string &filename, const Stmt &s) {
    internal_error << "\n"
                   << "\n"
                   << "Exiting early: print_to_viz is being run with a Stmt! how exciting"
                   << "\n"
                   << "\n"
                   << "\n";

    StmtToViz sth(filename, s);

    sth.print(s);
}

void print_to_viz(const string &filename, const Module &m) {

    StmtToViz sth(filename, m);

    sth.generate_html(filename, m);
    cout << "Donezoooooooo printing to " << filename << endl;
}

}  // namespace Internal
}  // namespace Halide
