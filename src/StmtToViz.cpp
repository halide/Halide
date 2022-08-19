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

namespace Halide {
namespace Internal {

using std::string;

namespace {
template<typename T>
std::string to_string(T value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

class StmtToViz : public IRVisitor {

    static const std::string css, js;
    static const std::string vizCss;
    static const std::string costColorsCSS;
    static const std::string formHTML, formCSS;
    static const std::string navigationHTML;
    static const std::string prodConsCSS;
    static const std::string lineNumbersCSS;

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
    stringstream script_stream;
    bool in_loop;

    // used for getting anchor names
    int ifCount = 0;
    int producerConsumerCount = 0;
    int forCount = 0;
    int storeCount = 0;
    int allocateCount = 0;

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
        script_stream << "document.getElementById('" << id << "').style.backgroundColor = 'blue';"
                      << endl;
    }

    // All spans and divs will have an id of the form "x-y", where x
    // is shared among all spans/divs in the same context, and y is unique.
    std::vector<int> context_stack;
    string open_tag(const string &tag, const string &cls, int id = -1) {
        std::stringstream s;
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
        std::stringstream s;
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
        std::stringstream s;
        // TODO: fix this!
        // s << open_span("tooltip");

        s << "<button class='info-button' role='button'";
        s << "onclick=\"openNewWindow('";
        s << hierarchyHTML;
        s << "')\"";
        s << "onmouseover='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"rgba(10,10,10,0.1)\";'";
        s << "onmouseout='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"transparent\";'";
        s << ">";
        s << "<i class='bi bi-info'></i>";
        s << "</button>";

        // s << open_span("tooltiptext");
        // s << tooltipText;
        // s << close_span();

        // s << close_span();
        return s.str();
    }

    string cost_table_tooltip(const IRNode *op, const string &hierarchyHTML) {
        int depth = findStmtCost.get_depth(op);
        int computationCost = findStmtCost.get_calculated_computation_cost(op);
        int dataMovementCost = findStmtCost.get_data_movement_cost(op);

        std::stringstream tooltipText;

        tooltipText << "<table>";

        tooltipText << "<tr>";
        tooltipText << "<td class = 'left-table'> Depth</ td>";
        tooltipText << "<td class = 'right-table'> " << depth << "</ td>";
        tooltipText << "</ tr>";

        tooltipText << "<tr>";
        tooltipText << "<td class = 'left-table'> Computation Cost</ td>";
        tooltipText << "<td class = 'right-table'> " << computationCost << "</ td>";
        tooltipText << "</ tr>";

        tooltipText << "<tr>";
        tooltipText << "<td class = 'left-table'> Data Movement Cost</ td>";
        tooltipText << "<td class = 'right-table'> " << dataMovementCost << "</ td>";
        tooltipText << "</ tr>";
        tooltipText << "</table>";

        return tooltip(hierarchyHTML, tooltipText.str());
    }

    string get_stmt_hierarchy(const Stmt &op) {
        if (PRINT_HIERARCHY) cout << getStmtHierarchy.get_hierarchy_html(op) << endl;
        return getStmtHierarchy.get_hierarchy_html(op);
    }
    string get_stmt_hierarchy(const Expr &op) {
        if (PRINT_HIERARCHY) cout << getStmtHierarchy.get_hierarchy_html(op) << endl;
        return getStmtHierarchy.get_hierarchy_html(op);
    }

    string open_cost_span(const IRNode *op, const string &hierarchyHTML) {
        std::stringstream s;
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

        std::stringstream s;

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

        int computation_range = findStmtCost.get_computation_range(op);
        s << open_span("CostColor" + to_string(computation_range) + " CostComputation");
        s << ".";
        s << close_span();

        s << cost_color_spacer();

        int data_movement_range = findStmtCost.get_data_movement_range(op);
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
        return "<a name=\"" + anchorName + "\"></a>";
    }
    string close_anchor() {
        return "</a>";
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

        std::stringstream s;
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
        std::stringstream button;
        button << "<a class=ExpandButton onclick='return toggle(" << id << ");' href=_blank>"
               << "<div class='expandButton' style='position:relative; width:0; height:0;'>"
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

        scope.push(op->name, unique_id());
        stream << open_div("LetStmt") << open_line();
        stream << cost_colors(op->value.get());

        stream << open_span("Matched");
        stream << keyword("let") << " ";
        stream << var(op->name);
        stream << close_span();
        stream << " " << matched("Operator Assign", "=") << " ";

        stream << open_cost_span(op->value.get(), get_stmt_hierarchy(op->value.get()));
        print(op->value);
        stream << close_cost_span();

        if (in_context) {
            curr_context.push_back(op->name);
        } else if (in_loop) {
            add_context_rule(curr_line_num);
        }
        in_context = in_context_before;

        stream << close_line();
        print(op->body);
        stream << close_div();
        scope.pop(op->name);

        remove_context(op->name);
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
        stream << open_anchor(anchorName);
        stream << close_anchor();

        int produce_id = unique_id();
        stream << open_span("Matched");
        stream << open_expand_button(produce_id);
        stream << keyword(op->is_producer ? "produce" : "consume") << " ";
        stream << var(op->name);
        stream << close_expand_button() << " {";
        stream << close_span();

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
        stream << open_anchor(anchorName);
        stream << close_anchor();

        int id = unique_id();
        stream << open_expand_button(id);
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
        stream << close_anchor();

        stream << cost_colors(op->value.get());
        stream << open_cost_span(op, get_stmt_hierarchy(op));

        stream << open_span("Matched");
        stream << var(op->name) << "[";
        stream << close_span();

        print(op->index);
        stream << matched("]");
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
        stream << close_anchor();

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

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_span("Matched");
        stream << keyword("if") << " (";
        stream << close_span();

        while (true) {
            // anchoring
            ifCount++;
            string anchorName = "if" + std::to_string(ifCount);
            stream << open_anchor(anchorName);
            close_anchor();

            print(op->condition);
            stream << matched(")");
            stream << close_expand_button() << " ";
            stream << matched("{");  // close if (or else if) span

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
                stream << matched("}") << " ";
                stream << open_expand_button(id);
                stream << open_span("Matched");
                stream << keyword("else if") << " (";
                stream << close_span();
                op = nested_if;
            } else {
                stream << open_span("Matched") << "} ";
                stream << open_expand_button(id);
                stream << keyword("else");
                stream << close_expand_button() << "{";
                stream << close_span();
                stream << open_div("ElseBody Indent", id);
                print(op->else_case);
                stream << close_div() << matched("}");
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
        stream << close_div() << matched("}");
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
        // open the prod cons div for navigation
        stream << "<div class='tab-pane fade' id='ProdCons' role='tabpanel' "
                  "aria-labelledby='ProdCons-tab'>\n";

        string prodConsHTML = producerConsumerHierarchy.generate_producer_consumer_html(m);
        if (PRINT_PROD_CONS) cout << prodConsHTML << endl;

        stream << prodConsHTML;
        stream << "</div>\n";
    }
    void generate_producer_consumer_hierarchy(const Stmt &s) {
        // open the prod cons div for navigation
        stream << "<div class='tab-pane fade' id='ProdCons' role='tabpanel' "
                  "aria-labelledby='ProdCons-tab'>\n";

        string prodConsHTML = producerConsumerHierarchy.generate_producer_consumer_html(s);
        if (PRINT_PROD_CONS) cout << prodConsHTML << endl;

        stream << prodConsHTML;
        stream << "</div>\n";
    }
    void generate_dependency_graph(const Module &m) {
        // open the dependency graph div for navigation
        stream << "<div class='tab-pane fade' id='Dependency' role='tabpanel' "
                  "aria-labelledby='Dependency-tab'>\n";

        string dependGraphHTML = dependencyGraph.generate_dependency_graph(m);
        if (PRINT_DEPENDENCIES) cout << dependGraphHTML << endl;

        // stream << dependGraphHTML;
        stream << "In construction...\n";
        stream << "</div>\n";
    }
    void generate_dependency_graph(const Stmt &s) {
        internal_error << "\n"
                       << "\n"
                       << "StmtToViz::generate_dependency_graph(const Stmt &s): Not implemented"
                       << "\n\n";

        // Stmt inlined_s = substitute_in_all_lets(s);
        // string dependGraphHTML = dependencyGraph.generate_dependency_graph(inlined_s);
        string dependGraphHTML = dependencyGraph.generate_dependency_graph(s);
    }

    void print(const Expr &ir) {
        ir.accept(this);
    }

    void print(const Stmt &ir) {
        ir.accept(this);
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

    void print_cuda_gpu_source_kernels(const std::string &str) {
        std::istringstream ss(str);
        int current_id = -1;
        stream << "<code class='ptx'>";
        bool in_braces = false;
        bool in_func_signature = false;
        std::string current_kernel;
        for (std::string line; std::getline(ss, line);) {
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
                std::vector<std::string> parts = split_string(line, " ");
                if (parts.size() == 3) {
                    in_func_signature = true;
                    current_id = unique_id();
                    stream << open_expand_button(current_id);

                    std::string kernel_name = parts[2].substr(0, parts[2].length() - 1);
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
            if ((idx = line.find("&#x2F;&#x2F")) != std::string::npos) {
                line.insert(idx, "<span class='Comment'>");
                line += "</span>";
            }

            // Predicated instructions
            if (line.front() == '@' && indent) {
                idx = line.find(' ');
                std::string pred = line.substr(1, idx - 1);
                line = "<span class='Pred'>@" + var(pred) + "</span>" + line.substr(idx);
            }

            // Labels
            if (line.front() == 'L' && !indent && (idx = line.find(':')) != std::string::npos) {
                std::string label = line.substr(0, idx);
                line = "<span class='Label'>" + var(label) + "</span>:" + line.substr(idx + 1);
            }

            // Highlight operands
            if ((idx = line.find(" \t")) != std::string::npos && line.back() == ';') {
                std::string operands_str = line.substr(idx + 2);
                operands_str = operands_str.substr(0, operands_str.length() - 1);
                std::vector<std::string> operands = split_string(operands_str, ", ");
                operands_str = "";
                for (size_t opidx = 0; opidx < operands.size(); ++opidx) {
                    std::string op = operands[opidx];
                    internal_assert(!op.empty());
                    if (opidx != 0) {
                        operands_str += ", ";
                    }
                    if (op.back() == '}') {
                        std::string reg = op.substr(0, op.size() - 1);
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
                        if (idx == std::string::npos) {
                            std::string reg = op.substr(1, op.size() - 2);
                            operands_str += '[' + var(reg) + ']';
                        } else {
                            std::string reg = op.substr(1, idx - 1);
                            std::string offset = op.substr(idx + 1);
                            offset = offset.substr(0, offset.size() - 1);
                            operands_str += '[' + var(reg) + "+";
                            operands_str += open_span("IntImm Imm");
                            operands_str += offset;
                            operands_str += close_span();
                            operands_str += ']';
                        }
                    } else if (op.front() == '{') {
                        std::string reg = op.substr(1);
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
            std::string str((const char *)op.data(), op.size_in_bytes());
            if (starts_with(op.name(), "cuda_")) {
                print_cuda_gpu_source_kernels(str);
            } else {
                stream << "<pre>\n";
                stream << str;
                stream << "</pre>\n";
            }
            stream << close_div();

            stream << " " << matched("}");
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
        for (const auto &f : m.functions()) {
            print(f);
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

        stream << "<style type='text/css'>";
        stream << css;
        stream << vizCss;
        stream << costColorsCSS;
        stream << prodConsCSS;
        stream << lineNumbersCSS;
        stream << "</style>\n";
        stream << "<script language='javascript' type='text/javascript'>" + js + "</script>\n";
        stream << "<link "
                  "href='http://maxcdn.bootstrapcdn.com/font-awesome/4.1.0/css/"
                  "font-awesome.min.css' rel='stylesheet'>\n";
        stream << "<link rel='stylesheet' href='https://unpkg.com/treeflex/dist/css/treeflex.css'>";
        stream << "<script src='http://code.jquery.com/jquery-1.10.2.js'></script>\n";
        stream << "</head>\n <body>\n";
        stream << navigationHTML;

        // bootstrap stuff
        stream << "<div class='tab-content' id='myTabContent'>\n";
    }

    StmtToViz(const string &filename, const Module &m)
        : getStmtHierarchy(generate_costs(m)),
          producerConsumerHierarchy(get_file_name(filename), findStmtCost), id_count(0),
          in_loop(false), context_stack(1, 0) {

        start_stream(filename);

        generate_producer_consumer_hierarchy(m);
        generate_dependency_graph(m);

        // open div for navigation
        stream << "<div class='tab-pane fade show active' id='IRCode' role='tabpanel' "
                  "aria-labelledby='IRCode-tab'>\n";
        stream << "<div class='IRCode-code'>\n";
    }

    StmtToViz(const string &filename, const Stmt &s)
        : getStmtHierarchy(generate_costs(s)),
          producerConsumerHierarchy(get_file_name(filename), findStmtCost), id_count(0),
          in_loop(false), context_stack(1, 0) {

        start_stream(filename);

        generate_producer_consumer_hierarchy(s);
        generate_dependency_graph(s);

        // open div for navigation
        stream << "<div class='tab-pane fade show active' id='IRCode' role='tabpanel' "
                  "aria-labelledby='IRCode-tab'>\n";
        stream << "<div class='IRCode-code'>\n";
    }

    ~StmtToViz() override {
        stream << "</div>\n";  // close IRCode-code div
        stream << "</div>\n";  // close IRCode div
        stream << "</div>\n";  // close bootstrap tab-content div

        stream << "<script>\n";
        stream << "$( '.Matched' ).each( function() {\n"
               << "    this.onmouseover = function() { $('.Matched[id^=' + this.id.split('-')[0] + "
                  "'-]').addClass('Highlight'); }\n"
               << "    this.onmouseout = function() { $('.Matched[id^=' + this.id.split('-')[0] + "
                  "'-]').removeClass('Highlight'); }\n"
               << "} );\n";
        stream << script_stream.str();
        stream << "</script>\n";
        stream << "</body>";
    }
};

const std::string StmtToViz::css = "\n \
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

const std::string StmtToViz::lineNumbersCSS = "\n \
div.IRCode-code { \n \
    counter-reset: line; \n \
    margin-left: 40px; \n \
    margin-top: 20px; \n \
} \n \
p.WrapLine,\n\
div.WrapLine,\n\
div.Consumer,\n\
div.Produce,\n\
div.For,\n\
div.IfThenElse,\n\
div.Evaluate,\n\
div.Allocate,\n\
div.ClosingBrace,\n\
div.Module,\n\
div.ModuleBody {\n\
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
div.IfThenElse:before,\n\
div.Evaluate:before,\n\
div.Allocate:before, \n\
div.ClosingBrace:before,\n\
div.Module:before, \n\
div.ModuleBody:before {\n\
    content: counter(line) '. ';\n\
    display: inline-block;\n\
    position: absolute;\n\
    left: 10px;\n\
    color: rgb(175, 175, 175);\n\
    user-select: none;\n\
    -webkit-user-select: none;\n\
}\n\
";

const std::string StmtToViz::vizCss = "\n \
span.ButtonSpacer { width: 5px; color: transparent; display: inline-block; }\n \
span.LowCost { background: rgba(10,10,10,0.1); }\n \
span.MediumCost { background: rgba(10,10,10,0.2); }\n \
span.HighCost { background: rgba(10,10,10,0.3); }\n \
.tooltip .tooltiptext { visibility: hidden; text-align: center; border-radius: 3px; padding: 5px; background: #FFFFFF; color: #313639; border: 1px solid #313639; border-radius: 8px; pointer-events: none; width: fit-content; position: absolute; z-index: 1; margin-top: -75px; margin-left: -40px; z-index: 1000; }\n \
.tooltip:hover .tooltiptext { visibility: visible; }\n \
.left-table { text-align: right; color: grey; vertical-align: middle; font-size: 12px; }\n \
.right-table { text-align: left; vertical-align: middle; font-size: 12px; font-weight: bold; padding-left: 3px; }\n \
.tf-custom .tf-nc { border-radius: 5px; border: 1px solid; }\n \
.tf-custom .tf-nc:before, .tf-custom .tf-nc:after { border-left-width: 1px; }\n \
.tf-custom li li:before { border-top-width: 1px; }\n \
.tf-custom .end-node { border-style: dashed; }\n \
.tf-custom .children-node { background-color: lightgrey; }\n \
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
} \n \
";

const std::string StmtToViz::costColorsCSS = "\n \
span.CostColor19 { background: rgb(130,31,27); } \n \
span.CostColor18 { background: rgb(145,33,30); } \n \
span.CostColor17 { background: rgb(160,33,32); } \n \
span.CostColor16 { background: rgb(176,34,34); } \n \
span.CostColor15 { background: rgb(185,47,32); } \n \
span.CostColor14 { background: rgb(193,59,30); } \n \
span.CostColor13 { background: rgb(202,71,27); } \n \
span.CostColor12 { background: rgb(210,82,22); } \n \
span.CostColor11 { background: rgb(218,93,16); } \n \
span.CostColor10 { background: rgb(226,104,6); } \n \
span.CostColor9 { background: rgb(229,118,9); } \n \
span.CostColor8 { background: rgb(230,132,15); } \n \
span.CostColor7 { background: rgb(231,146,20); } \n \
span.CostColor6 { background: rgb(232,159,25); } \n \
span.CostColor5 { background: rgb(233,172,30); } \n \
span.CostColor4 { background: rgb(233,185,35); } \n \
span.CostColor3 { background: rgb(233,198,40); } \n \
span.CostColor2 { background: rgb(232,211,45); } \n \
span.CostColor1 { background: rgb(231,223,50); } \n \
span.CostColor0 { background: rgb(236,233,89); } \n \
span.CostColorSpacer { width: 2px; color: transparent; display: inline-block; }\n \
span.CostComputation { width: 13px; display: inline-block; color: transparent; } \n \
span.CostMovement { width: 13px; display: inline-block;  color: transparent; } \n \
";

const std::string StmtToViz::formCSS = "\n \
form { \n \
    outline: solid 1px black; \n \
    padding: 10px; \n \
    font-size: 14px; \n \
    font-weight: bold; \n \
    position: fixed; \n \
    width: 100%; \n \
    background: white; \n \
    left: 2px; \n \
    top: 1px; \n \
    z-index: 999;\n \
} \n \
";

const std::string StmtToViz::formHTML = "\n \
<form> \n \
    <input type='checkbox' id='showAsserts' \n \
        onclick='showAssertsClicked(this);' checked> \n \
    <label for='showAsserts'> Show assert statements (not implemented \n \
        yet)</label><br> \n \
    <input type='checkbox' id='showMemAlloc' \n \
        onclick='showMemAllocClicked(this);' checked> \n \
    <label for='showMemAlloc'> Show memory allocation / \n \
        de-allocation (not implemented yet)</label><br> \n \
    <input type='checkbox' id='showCompute' \n \
        onclick='showComputeClicked(this);' checked> \n \
    <label for='showCompute'> Show compute (not implemented yet)</label><br> \n \
</form> \n \
<div style='height: 80px;'></div> \n \
";

const std::string StmtToViz::navigationHTML = "\n \
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

const std::string StmtToViz::prodConsCSS = "\n \
.tf-custom .tf-nc { \n \
border-radius: 5px; \n \
border: 1px solid; \n \
font-size: 12px; \n \
background-color: #e6eeff;\n \
}\n \
.tf-custom .end-node { border-style: dashed; font-size: 12px; } \n \
.tf-custom .tf-nc:before, .tf-custom .tf-nc:after { border-left-width: 1px; } \n \
.tf-custom li li:before { border-top-width: 1px; }\n \
.tf-custom .tf-nc .if-node { background-color: #e6eeff; }\n \
table { \n \
border-radius: 5px; \n \
font-size: 12px; \n \
border: 1px dashed grey; \n \
border-collapse: separate; \n \
border-spacing: 0; \n \
} \n \
.center { \n \
margin-left: auto; \n \
margin-right: auto; \n \
}  \n \
.ifElseTable { \n \
border: 0px; \n \
}  \n \
.costTable { \n \
float: right; \n \
text-align: center; \n \
border: 0px; \n \
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
.tf-custom .tf-nc { \n \
border-radius: 5px; \n \
border: 1px solid; \n \
font-size: 12px; \n \
padding: 5px; \n \
} \n \
 \n \
.tf-custom .end-node { \n \
border-style: dashed; \n \
font-size: 12px; \n \
} \n \
 \n \
.tf-custom .tf-nc:before, \n \
.tf-custom .tf-nc:after { \n \
border-left-width: 1px; \n \
} \n \
 \n \
.tf-custom li li:before { \n \
border-top-width: 1px; \n \
} \n \
";

const std::string StmtToViz::js = "\n \
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

    if (m.functions().size() > 1) {
        internal_error << "\n"
                       << "\n"
                       << "Exiting early: printing to viz only works for modules with "
                          "one function (for now)"
                       << "\n"
                       << "\n"
                       << "\n";
        return;
    }

    StmtToViz sth(filename, m);

    sth.print(m);
}

}  // namespace Internal
}  // namespace Halide
