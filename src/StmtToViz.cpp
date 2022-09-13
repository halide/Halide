#include "StmtToViz.h"
#include "Error.h"
#include "FindStmtCost.h"
#include "GetAssemblyInfoViz.cpp"
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
    static const string flexboxDivCSS;
    static const string scrollToFunctionJSCodeToViz;
    static const string lineNumbersCSS;
    static const string tooltipCSS;
    static const string expandCodeJS;
    static const string assemblyCodeJS;

    // This allows easier access to individual elements.
    int id_count;

private:
    std::ofstream stream;

    FindStmtCost findStmtCost;                            // used for finding the cost of statements
    GetStmtHierarchy getStmtHierarchy;                    // used for getting the hierarchy of
                                                          // statements
    ProducerConsumerHierarchy producerConsumerHierarchy;  // used for getting the hierarchy of
                                                          // producer/consumer
    GetAssemblyInfoViz getAssemblyInfoViz;  // used for getting the assembly line numbers

    int curr_line_num;  // for accessing div of that line

    // used for getting anchor names
    int ifCount = 0;
    int producerConsumerCount = 0;
    int forCount = 0;
    int storeCount = 0;
    int allocateCount = 0;
    int functionCount = 0;

    // used for tooltip stuff
    int tooltipCount = 0;

    // used for getStmtHierarchy popup stuff
    int popupCount = 0;
    string popups;

    int unique_id() {
        return ++id_count;
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

    StmtHierarchyInfo get_stmt_hierarchy(const Stmt &op) {
        StmtHierarchyInfo stmtHierarchyInfo = getStmtHierarchy.get_hierarchy_html(op);
        string &html = stmtHierarchyInfo.html;
        string popup = generate_stmt_hierarchy_popup(html);
        stmtHierarchyInfo.html = popup;

        return stmtHierarchyInfo;
    }
    StmtHierarchyInfo get_stmt_hierarchy(const Expr &op) {
        StmtHierarchyInfo stmtHierarchyInfo = getStmtHierarchy.get_hierarchy_html(op);
        string &html = stmtHierarchyInfo.html;
        string popup = generate_stmt_hierarchy_popup(html);
        stmtHierarchyInfo.html = popup;

        return stmtHierarchyInfo;
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

    string open_cost_span(const Stmt &stmt_op) {
        StmtHierarchyInfo stmtHierarchyInfo = get_stmt_hierarchy(stmt_op);

        stringstream s;

        s << cost_colors(stmt_op.get(), stmtHierarchyInfo);

        // popup window - will put them all at the end
        popups += stmtHierarchyInfo.html + "\n";

        s << "<span id='Cost" << id_count << "'>";
        return s.str();
    }
    string open_cost_span(const Expr &stmt_op) {
        StmtHierarchyInfo stmtHierarchyInfo = get_stmt_hierarchy(stmt_op);

        stringstream s;

        s << cost_colors(stmt_op.get(), stmtHierarchyInfo);

        // popup window - will put them all at the end
        popups += stmtHierarchyInfo.html + "\n";

        s << "<span id='Cost" << id_count << "'>";
        return s.str();
    }

    string close_cost_span() {
        return "<!-- closing_cost_span --></span>";
    }
    string open_cost_span_else_case(Stmt else_case) {
        Stmt new_node =
            IfThenElse::make(Variable::make(Int(32), "canIgnoreVariableName"), else_case, nullptr);

        StmtHierarchyInfo stmtHierarchyInfo = getStmtHierarchy.get_else_hierarchy_html();
        string popup = generate_stmt_hierarchy_popup(stmtHierarchyInfo.html);

        // popup window - will put them all at the end
        popups += popup + "\n";

        stringstream s;

        curr_line_num += 1;

        s << "<span class='smallColorIndent'>";

        s << computation_button(new_node.get(), stmtHierarchyInfo);
        s << data_movement_button(new_node.get(), stmtHierarchyInfo);

        s << "</span>";

        s << "<span id='Cost" << id_count << "'>";
        return s.str();
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

    string color_button(const IRNode *op, bool is_computation,
                        const StmtHierarchyInfo &stmtHierarchyInfo) {

        int colorRangeInclusive, colorRangeExclusive;

        if (is_computation) {
            colorRangeInclusive = findStmtCost.get_combined_computation_color_range(op);
            colorRangeExclusive = findStmtCost.get_computation_color_range(op, false);
        } else {
            colorRangeInclusive = findStmtCost.get_combined_data_movement_color_range(op);
            colorRangeExclusive = findStmtCost.get_data_movement_color_range(op, false);
        }
        tooltipCount++;

        stringstream s;
        s << "<button ";

        // tooltip information
        s << "id='button" << tooltipCount << "' ";
        s << "aria-describedby='tooltip" << tooltipCount << "' ";

        // cost colors
        s << "class='colorButton CostColor" + to_string(colorRangeExclusive) + "' role='button' ";

        // showing StmtHierarchy popup
        s << "data-bs-toggle='modal' data-bs-target='#stmtHierarchyModal" << popupCount << "' ";

        // for collapsing and expanding StmtHierarchy nodes
        s << "onclick='collapseAllNodes(" << stmtHierarchyInfo.start_node << ", "
          << stmtHierarchyInfo.end_node << "); expandNodesUpToDepth(4, "
          << stmtHierarchyInfo.viz_num << ");'";

        // highlighting selected line in grey
        s << "onmouseover='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"rgba(10,10,10,0.1)\";'";
        s << "onmouseout='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"transparent\";'";

        // for collapsing and expanding and adjusting colors accordingly
        s << "inclusiverange='" << colorRangeInclusive << "' ";
        s << "exclusiverange='" << colorRangeExclusive << "' ";

        s << ">";
        s << "</button>";

        return s.str();
    }

    string computation_button(const IRNode *op, const StmtHierarchyInfo &stmtHierarchyInfo) {
        stringstream s;
        s << color_button(op, true, stmtHierarchyInfo);

        string tooltipText = findStmtCost.generate_computation_cost_tooltip(
            op, false, "[Click to see full hierarchy]");

        // tooltip span
        s << "<span id='tooltip" << tooltipCount << "' class='tooltip CostTooltip' ";
        s << "role='tooltip" << tooltipCount << "'>";
        s << tooltipText;
        s << "</span>";

        return s.str();
    }
    string data_movement_button(const IRNode *op, const StmtHierarchyInfo &stmtHierarchyInfo) {
        stringstream s;
        s << color_button(op, false, stmtHierarchyInfo);

        string tooltipText = findStmtCost.generate_data_movement_cost_tooltip(
            op, false, "[Click to see full hierarchy]");

        // tooltip span
        s << "<span id='tooltip" << tooltipCount << "' class='tooltip CostTooltip' ";
        s << "role='tooltip" << tooltipCount << "'>";
        s << tooltipText;
        s << "</span>";

        return s.str();
    }
    string cost_colors(const IRNode *op, const StmtHierarchyInfo &stmtHierarchyInfo) {
        curr_line_num += 1;

        stringstream s;

        if (op->node_type == IRNodeType::Allocate || op->node_type == IRNodeType::Evaluate ||
            op->node_type == IRNodeType::IfThenElse || op->node_type == IRNodeType::For ||
            op->node_type == IRNodeType::ProducerConsumer) {
            s << "<span class='smallColorIndent'>";
        } else {
            s << "<span class='bigColorIndent'>";
        }

        s << computation_button(op, stmtHierarchyInfo);
        s << data_movement_button(op, stmtHierarchyInfo);

        s << "</span>";

        return s.str();
    }

    string open_div(const string &cls, int id = -1) {
        return open_tag("div", cls, id) + "\n";
    }
    string close_div() {
        return close_tag("div") + "\n";
    }

    string open_anchor(const string &anchorName) {
        return "<span class='navigationAnchor' id='" + anchorName + "'>";
    }
    string close_anchor() {
        return "<!-- close_anchor --></span>";
    }

    string see_viz_button(const string &anchorName) {
        stringstream s;

        s << "<button class='iconButton' ";
        s << "onclick='scrollToFunctionCodeToViz(\"" + anchorName + "_viz\")'>";
        s << "<i class='bi bi-arrow-right-square'></i>";
        s << "</button>";

        return s.str();
    }

    string see_assembly_button(const int &assemblyLineNum) {
        stringstream s;

        tooltipCount++;
        s << "<button class='iconButton assemblyIcon' ";
        s << "id='button" << tooltipCount << "' ";
        s << "aria-describedby='tooltip" << tooltipCount << "' ";
        s << "onclick='openAssembly(" << assemblyLineNum << ");'>";
        s << "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' "
             "class='bi bi-filetype-raw' viewBox='0 0 16 16'>";
        s << "<path fill-rule='evenodd' d='M14 4.5V14a2 2 0 0 1-2 2v-1a1 1 0 0 0 1-1V4.5h-2A1.5 "
             "1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v9H2V2a2 2 0 0 1 2-2h5.5L14 4.5ZM1.597 "
             "11.85H0v3.999h.782v-1.491h.71l.7 1.491h1.651l.313-1.028h1.336l.314 1.028h.84L5.31 "
             "11.85h-.925l-1.329 3.96-.783-1.572A1.18 1.18 0 0 0 3 "
             "13.116c0-.256-.056-.479-.167-.668a1.098 1.098 0 0 0-.478-.44 1.669 1.669 0 0 "
             "0-.758-.158Zm-.815 1.913v-1.292h.7a.74.74 0 0 1 .507.17c.13.113.194.276.194.49 0 "
             ".21-.065.368-.194.474-.127.105-.3.158-.518.158H.782Zm4.063-1.148.489 "
             "1.617H4.32l.49-1.617h.035Zm4.006.445-.74 2.789h-.73L6.326 11.85h.855l.601 "
             "2.903h.038l.706-2.903h.683l.706 2.903h.04l.596-2.903h.858l-1.055 "
             "3.999h-.73l-.74-2.789H8.85Z'/></svg>";
        s << "</button>";

        // tooltip span
        s << "<span id='tooltip" << tooltipCount << "' class='tooltip' ";
        s << "role='tooltip" << tooltipCount << "'>";
        s << "Click to see assembly code";
        s << "</span>";

        return s.str();
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
        button << "<a class=ExpandButton onclick='return toggle(" << id << ", " << tooltipCount
               << ");'>"
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

    void visit(const Reinterpret *op) override {
        stream << open_span("Reinterpret");

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

        scope.push(op->name, unique_id());
        stream << open_span("Let");
        stream << open_span("Matched");
        stream << "(" << keyword("let") << " ";
        stream << var(op->name);
        stream << close_span();
        stream << " " << matched("Operator Assign", "=") << " ";
        print(op->value);

        stream << " " << matched("Keyword", "in") << " ";
        print(op->body);
        stream << matched(")");
        stream << close_span();
        scope.pop(op->name);
    }
    void visit(const LetStmt *op) override {

        scope.push(op->name, unique_id());
        stream << open_div("LetStmt") << open_line();

        stream << open_cost_span(op);
        stream << open_span("Matched");
        stream << keyword("let") << " ";
        stream << var(op->name);
        stream << close_span();
        stream << " " << matched("Operator Assign", "=") << " ";

        print(op->value);
        stream << close_cost_span();

        stream << close_line();
        print(op->body);
        stream << close_div();

        scope.pop(op->name);
    }
    void visit(const AssertStmt *op) override {
        stream << open_div("AssertStmt WrapLine");
        std::vector<Expr> args;
        args.push_back(op->condition);
        args.push_back(op->message);
        stream << open_cost_span(op);
        print_list(symbol("assert") + "(", args, ")");
        stream << close_cost_span();
        stream << close_div();
    }
    void visit(const ProducerConsumer *op) override {
        scope.push(op->name, unique_id());
        stream << open_div(op->is_producer ? "Produce" : "Consumer");

        // anchoring
        producerConsumerCount++;
        string anchorName = "producerConsumer" + std::to_string(producerConsumerCount);

        // for assembly
        int assemblyLineNum = getAssemblyInfoViz.get_line_number(op);

        int produce_id = unique_id();

        stream << open_cost_span(op);
        stream << open_span("Matched");
        stream << open_expand_button(produce_id);
        stream << open_anchor(anchorName);
        stream << keyword(op->is_producer ? "produce" : "consume") << " ";
        stream << var(op->name);
        stream << close_expand_button() << " {";
        stream << close_span();
        stream << close_cost_span();
        stream << close_anchor();
        if (assemblyLineNum != -1) stream << see_assembly_button(assemblyLineNum);
        stream << see_viz_button(anchorName);

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

        scope.push(op->name, unique_id());
        stream << open_div("For");

        // anchoring
        forCount++;
        string anchorName = "for" + std::to_string(forCount);

        // for assembly
        int assemblyLineNum = getAssemblyInfoViz.get_line_number(op);

        int id = unique_id();
        stream << open_cost_span(op);
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

        stream << matched(")");
        stream << close_expand_button();
        stream << " " << matched("{");
        stream << close_cost_span();
        stream << close_anchor();
        if (assemblyLineNum != -1) stream << see_assembly_button(assemblyLineNum);
        stream << see_viz_button(anchorName);

        stream << open_div("ForBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << open_div("ClosingBrace");
        stream << matched("}");
        stream << close_div();
        stream << close_div();
        scope.pop(op->name);
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

        stream << open_cost_span(op);
        stream << open_anchor(anchorName);

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

        stream << close_anchor();
        stream << close_cost_span();
        stream << see_viz_button(anchorName);
        stream << close_div();
    }
    void visit(const Provide *op) override {
        stream << open_div("Provide WrapLine");
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

        stream << open_cost_span(op);

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

        stream << close_anchor();
        stream << see_viz_button(anchorName);

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
        stream << open_cost_span(op);
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
            stream << close_cost_span();
            close_anchor();
            stream << see_viz_button(anchorName);

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

                stream << open_cost_span(nested_if);
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

                stream << open_cost_span_else_case(op->else_case);
                stream << open_expand_button(id);

                // for line numbers
                stream << open_span("IfSpan");
                stream << close_span();

                // anchoring
                ifCount++;
                string anchorName = "if" + std::to_string(ifCount);
                stream << open_anchor(anchorName);

                stream << keyword("else ");
                stream << close_expand_button() << "{";
                stream << close_cost_span();
                close_anchor();
                stream << see_viz_button(anchorName);

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
        stream << open_cost_span(op);
        print(op->value);
        stream << close_cost_span();

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

        // anchoring
        functionCount++;
        string anchorName = "loweredFunc" + std::to_string(functionCount);

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_anchor(anchorName);
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
        stream << close_anchor();
        stream << close_expand_button();
        stream << " " << matched("{");
        stream << see_viz_button(anchorName);

        stream << open_div("FunctionBody Indent", id);

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
            internal_error << "\n\n\nvoid print(const Buffer<> &op): look at this line!!! make "
                              "sure the closing brace is correct! \n\n\n";
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
        stream
            << "<link rel='stylesheet' href='https://unpkg.com/treeflex/dist/css/treeflex.css'>\n";
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
        stream << flexboxDivCSS;
        stream << lineNumbersCSS;
        stream << tooltipCSS;
        stream << GetStmtHierarchy::stmtHierarchyCSS;
        stream << "</style>\n";
        stream << "<script language='javascript' type='text/javascript'>" + js + "</script>\n";
        stream << "</head>\n <body>\n";
    }

    void end_stream() {
        stream << "<div class='popups'>\n";
        stream << popups;
        stream << "</div>\n";

        stream << "<script>\n";
        stream << "$( '.Matched' ).each( function() {\n"
               << "    this.onmouseover = function() { $('.Matched[id^=' + this.id.split('-')[0] + "
                  "'-]').addClass('Highlight'); }\n"
               << "    this.onmouseout = function() { $('.Matched[id^=' + this.id.split('-')[0] + "
                  "'-]').removeClass('Highlight'); }\n"
               << "} );\n";

        stream << generatetooltipJS(tooltipCount);
        stream << getStmtHierarchy.generate_collapse_expand_js();
        stream << getStmtHierarchy.generate_stmtHierarchy_js();
        stream << producerConsumerHierarchy.generate_prodCons_js();
        stream << ProducerConsumerHierarchy::scrollToFunctionJSVizToCode;
        stream << scrollToFunctionJSCodeToViz;
        stream << expandCodeJS;
        stream << assemblyCodeJS;
        stream << "</script>\n";
        stream << "</body>";
    }

    string informationPopup() {

        stringstream popup;

        popupCount++;
        popup << "<div class='modal fade' id='stmtHierarchyModal" << popupCount;
        popup << "' tabindex='-1'\n";
        popup << "    aria-labelledby='stmtHierarchyModalLabel' aria-hidden='true'>\n";
        popup << "    <div class='modal-dialog modal-dialog-scrollable modal-xl'>\n";
        popup << "        <div class='modal-content'>\n";
        popup << "            <div class='modal-header'>\n";
        popup << "                <h5 class='modal-title' id='stmtHierarchyModalLabel'>How to read "
                 "this document\n";
        popup << "                </h5>\n";
        popup << "                <button type='button' class='btn-close'\n";
        popup << "                    data-bs-dismiss='modal' aria-label='Close'></button>\n";
        popup << "            </div>\n";
        popup << "            <div class='modal-body'>\n";
        popup << "<p style='font-size: 20px;'><b\n";
        popup << "        style='font-weight: bold;'>Two Columns</b>\n";
        popup << "</p>\n";
        popup << "<p>There are 2 columns on this page:</p>\n";
        popup << "<ul>\n";
        popup << "    <li><b style='font-weight: bold;'>Left side:</b>\n";
        popup << "        Halide Intermediate Representation (IR) - the\n";
        popup << "        code that\n";
        popup << "        Halide generates.</li>\n";
        popup << "    <li><b style='font-weight: bold;'>Right side:</b>\n";
        popup << "        Visualization - represents, at\n";
        popup << "        a high level, the structure of the IR.</li>\n";
        popup << "</ul>\n";
        popup << "<p>You can adjust the size of the columns using the\n";
        popup << "    green resize bar in the middle of the 2. The buttons\n";
        popup << "    in the middle of this bar will also expand either\n";
        popup << "    left or right column completely.</p>\n";
        popup << "<p style='font-size: 20px;'><b\n";
        popup << "        style='font-weight: bold;'>Left Column\n";
        popup << "        Functionality</b>\n";
        popup << "</p>\n";
        popup << "<p>Here are the different features of the left column:\n";
        popup << "</p>\n";
        popup << "<ul>\n";

        tooltipCount++;
        popup << "<span id='tooltip" << tooltipCount << "' class='tooltip CostTooltip' ";
        popup << "role='tooltip" << tooltipCount << "'>";
        popup << "Costs will be shown here. Click to see statement hierarchy.";
        popup << "</span>";

        popup << "    <li><button id='button" << tooltipCount << "' aria-describedby='tooltip"
              << tooltipCount << "'\n";
        popup << "            class='colorButton CostColor0' role='button'\n";
        popup << "            inclusiverange='0'\n";
        popup << "            exclusiverange='0'></button>";
        popup << "<b\n";
        popup << "            style='font-weight: bold;'>Cost\n";
        popup << "            Colors:</b>\n";
        popup << "            Next to every line, there are 2 buttons that are\n";
        popup << "            colored based on the cost of the line.\n";
        popup << "            Hovering over them will give more information\n";
        popup << "            about the cost of that line. If you click on the\n";
        popup << "            button, a hierarchy of that line will appear,\n";
        popup << "            which you can explore to see the contents of the\n";
        popup << "            line. There are 2 buttons because they each\n";
        popup << "            represent a different type of color:\n";
        popup << "            <ul>\n";
        popup << "                <li><b style='font-weight: bold;'>Computation\n";
        popup << "                        Cost:</b> This is the cost\n";
        popup << "                    associated with how much computation\n";
        popup << "                    went\n";
        popup << "                    into that line of code.</li>\n";
        popup << "                <li><b style='font-weight: bold;'>Data\n";
        popup << "                        Movement Cost:</b> This is the\n";
        popup << "                    cost associated with how much data was\n";
        popup << "                    moved\n";
        popup << "                    around in that line of code\n";
        popup << "                    (read/written).</li>\n";
        popup << "            </ul>\n";
        popup << "    </li>\n";
        popup << "    <br>\n";
        popup << "    <li>\n";
        popup << "        <button class='iconButton'><i\n";
        popup << "                class='bi bi-arrow-right-square'></i></button><b\n";
        popup << "        style='font-weight: bold;'>See\n";
        popup << "            Visualization:</b>\n";
        popup << "        If you click this button, the right column will\n";
        popup << "        scroll to the related block of that line of\n";
        popup << "        code.\n";
        popup << "    </li>\n";
        popup << "    <li>\n";
        popup << "        <button class='iconButton assemblyIcon'>";
        popup
            << "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' "
               "class='bi bi-filetype-raw' viewBox='0 0 16 16'> <path fill-rule='evenodd' d='M14 "
               "4.5V14a2 2 0 0 1-2 2v-1a1 1 0 0 0 1-1V4.5h-2A1.5 "
               "1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v9H2V2a2 2 0 0 1 2-2h5.5L14 4.5ZM1.597 "
               "11.85H0v3.999h.782v-1.491h.71l.7 1.491h1.651l.313-1.028h1.336l.314 1.028h.84L5.31 "
               "11.85h-.925l-1.329 3.96-.783-1.572A1.18 1.18 0 0 0 3 "
               "13.116c0-.256-.056-.479-.167-.668a1.098 1.098 0 0 0-.478-.44 1.669 1.669 0 0 "
               "0-.758-.158Zm-.815 1.913v-1.292h.7a.74.74 0 0 1 .507.17c.13.113.194.276.194.49 0 "
               ".21-.065.368-.194.474-.127.105-.3.158-.518.158H.782Zm4.063-1.148.489 "
               "1.617H4.32l.49-1.617h.035Zm4.006.445-.74 2.789h-.73L6.326 11.85h.855l.601 "
               "2.903h.038l.706-2.903h.683l.706 2.903h.04l.596-2.903h.858l-1.055 "
               "3.999h-.73l-.74-2.789H8.85Z'/></svg>";
        popup << "</button><b \n";
        popup << "        style='font-weight: bold;'>See Assembly:</b>\n";
        popup << "        If you click this button, a new tab will open\n";
        popup << "        with the assembly scrolled to the related\n";
        popup << "        assembly instruction of that line of code.\n";
        popup << "    </li>\n";
        popup << "</ul>\n";
        popup << "<p style='font-size: 20px;'><b\n";
        popup << "        style='font-weight: bold;'>Right Column\n";
        popup << "        Functionality</b>\n";
        popup << "</p>\n";
        popup << "<p>Here are the different features of the right column:\n";
        popup << "</p>\n";
        popup << "<ul>\n";

        tooltipCount++;
        popup << "<span id='tooltip" << tooltipCount << "' class='tooltip' ";
        popup << "role='tooltip" << tooltipCount << "'>";
        popup << "Costs will be shown here.";
        popup << "</span>";

        popup << "    <li><button id='button" << tooltipCount << "' aria-describedby='tooltip"
              << tooltipCount << "'\n";
        popup << "            class='colorButton CostColor0' role='button'\n";
        popup << "            inclusiverange='0'\n";
        popup << "            exclusiverange='0'></button><b ";
        popup << "    style='font-weight: bold;'>Cost Colors:</b>\n";
        popup << "        Cost colors on the right work the same way as\n";
        popup << "        they do on the left - hovering over them will give\n";
        popup << "        information about the cost.\n";
        popup << "    </li>\n";
        popup << "    <li>\n";
        popup << "        <button class='iconButton'><i\n";
        popup << "                class='bi bi-code-square'></i></button><b\n";
        popup << "        style='font-weight: bold;'>See\n";
        popup << "            Code:</b>\n";
        popup << "        If you click this button, the left column will\n";
        popup << "        scroll to the related line of code of that block\n";
        popup << "        in the visualization.\n";
        popup << "    </li>\n";
        popup << "    <li>\n";

        tooltipCount++;
        popup << "<span id='tooltip" << tooltipCount << "' class='tooltip' ";
        popup << "role='tooltip" << tooltipCount << "'>";
        popup << "More information about the node will appear here.";
        popup << "</span>";

        popup << "        <button class='info-button' id='button" << tooltipCount
              << "' aria-describedby='tooltip" << tooltipCount << "'><i\n";
        popup << "                class='bi bi-info'></i></button><b \n";
        popup << "        style='font-weight: bold;'>Info Button:</b>\n";
        popup << "        If you hover over these buttons, they will\n";
        popup << "        offer more information about that block (eg.\n";
        popup << "        load/store size, allocation type, etc.)\n";
        popup << "    </li>\n";
        popup << "</ul>        \n";
        popup << "            </div>\n";
        popup << "        </div>\n";
        popup << "    </div>\n";
        popup << "</div>\n";

        return popup.str();
    }

    void informationBar(const Module &m) {
        popups += informationPopup();

        stream << "<div class='informationBar'>\n";
        stream << "<div class='title'>\n";
        stream << "<h3>" << m.name() << "</h3>\n";
        stream << "</div>\n";
        stream << "<div class='spacing'></div>\n";
        stream << "<div class='button'>\n";
        stream << "<h3><button class='informationBarButton'><i\n";
        stream << "class='bi bi-info-square' data-bs-toggle='modal'\n";
        stream << "data-bs-target='#stmtHierarchyModal" << popupCount << "'></i></button>\n";
        stream << "</h3>\n";
        stream << "</div>\n";
        stream << "</div>\n";
    }

    void resizeBar() {
        stream << "<div class='ResizeBar' id='ResizeBar'>\n";
        stream << "<div class='collapseButtons'>\n";
        stream << "<button class='iconButton' onclick='collapseViz()'>";
        stream << "<i class='bi bi-arrow-bar-right'></i></button>\n";
        stream << "<button class='iconButton' onclick='collapseCode()'>";
        stream << "<i class='bi bi-arrow-bar-left'></i></button>\n";
        stream << "</div>\n";
        stream << "</div>\n";
    }

    void generate_html(const string &filename, const Module &m) {
        getAssemblyInfoViz.generate_assembly_information(m, filename);

        // opening parts of the html
        start_stream(filename);

        stream << "<div class='outerDiv'>\n";

        informationBar(m);

        stream << "<div class='mainContent'>\n";

        // print main html page
        stream << "<div class='IRCode-code' id='IRCode-code'>\n";
        print(m);
        stream << "</div>\n";

        // for resizing the code and visualization divs
        resizeBar();

        stream << "<div class='ProducerConsumerViz' id='ProducerConsumerViz'>\n";
        generate_producer_consumer_hierarchy(m);
        stream << "</div>\n";

        stream << "</div>\n";  // close mainContent div
        stream << "</div>\n";  // close outerDiv div

        // put assembly code in a div
        stream << getAssemblyInfoViz.get_assembly_html();

        // closing parts of the html
        end_stream();
    }

    StmtToViz(const string &filename, const Module &m)
        : id_count(0), getStmtHierarchy(generate_costs(m)), producerConsumerHierarchy(findStmtCost),
          context_stack(1, 0) {
    }

    StmtToViz(const string &filename, const Stmt &s)
        : id_count(0), getStmtHierarchy(generate_costs(s)), producerConsumerHierarchy(findStmtCost),
          context_stack(1, 0) {
    }

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
        tooltipJS << "    const button = document.getElementById('button' + i); \n";
        tooltipJS << "    const tooltip = document.getElementById('tooltip' + i); \n";
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
.tf-tree { overflow: unset; }\n \
";

const string StmtToViz::flexboxDivCSS = "\n \
/* Flexbox Div Styling CSS */ \n \
div.outerDiv { \n \
    height: 100vh; \n \
    display: flex; \n \
    flex-direction: column; \n \
} \n \
div.informationBar { \n \
    display: flex; \n \
} \n \
div.mainContent { \n \
    display: flex; \n \
    flex-grow: 1; \n \
    width: 100%; \n \
    overflow: hidden; \n \
    border-top: 1px solid rgb(200,200,200) \n \
} \n \
div.IRCode-code { \n \
    counter-reset: line; \n \
    padding-left: 50px; \n \
    padding-top: 20px; \n \
    overflow-y: scroll; \n \
    position: relative; \n \
} \n \
div.ProducerConsumerViz { \n \
    overflow-y: scroll; \n \
    padding-top: 20px; \n \
    padding-left: 20px; \n \
    position: relative; \n \
} \n \
div.ResizeBar { \n \
    background: rgb(201, 231, 190); \n \
    cursor: col-resize; \n \
    border-left: 1px solid rgb(0, 0, 0); \n \
    border-right: 1px solid rgb(0, 0, 0); \n \
} \n \
div.collapseButtons { \n \
    position: relative; \n \
    top: 50%; \n \
} \n \
";

const string StmtToViz::scrollToFunctionJSCodeToViz = "\n \
// scroll to function - code to viz \n \
function makeVisibleViz(element) { \n \
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
    makeVisibleViz(element.parentNode); \n \
} \n \
function getOffsetTop(element) { \n \
    if (!element) return 0; \n \
    if (element.id == 'ProducerConsumerViz') return 0; \n \
    return getOffsetTop(element.offsetParent) + element.offsetTop; \n \
} \n \
function getOffsetLeft(element) { \n \
    if (!element) return 0; \n \
    if (element.id == 'ProducerConsumerViz') return 0; \n \
    return getOffsetLeft(element.offsetParent) + element.offsetLeft; \n \
} \n \
function scrollToFunctionCodeToViz(id) { \n \
    var container = document.getElementById('ProducerConsumerViz'); \n \
    var scrollToObject = document.getElementById(id); \n \
    makeVisibleViz(scrollToObject); \n \
    container.scrollTo({ \n \
        top: getOffsetTop(scrollToObject) - 20, \n \
        left: getOffsetLeft(scrollToObject) - 40, \n \
        behavior: 'smooth' \n \
    }); \n \
    scrollToObject.style.backgroundColor = 'yellow'; \n \
    scrollToObject.style.fontSize = '20px'; \n \
    // change content for 1 second   \n \
    setTimeout(function () { \n \
        scrollToObject.style.backgroundColor = 'transparent'; \n \
        scrollToObject.style.fontSize = '12px'; \n \
    }, 1000); \n \
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
    left: 30px;\n\
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
    left: 0px;\n\
    color: rgb(175, 175, 175);\n\
    user-select: none;\n\
    -webkit-user-select: none;\n\
}\n\
";

const string StmtToViz::vizCss = "\n \
/* Additional Code Visualization CSS */\n \
span.ButtonSpacer { width: 5px; color: transparent; display: inline-block; }\n \
.info-button { \n \
    background-color: rgba(113, 113, 113, 0.1); \n \
    border: 1px solid rgb(113, 113, 113); \n \
    color: rgb(113, 113, 113); \n \
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
    margin-left: 5px; \n \
    margin-right: 5px; \n \
    font-size: 15px; \n \
} \n \
.info-button:hover, .function-scroll-button:hover { \n \
    background-color: #f7fafa; \n \
} \n \
.function-scroll-button { \n \
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
    margin-left: 5px; \n \
    font-size: 15px; \n \
    padding: 3px; \n \
} \n \
.colorButton { \n \
    height: 15px; \n \
    width: 5px; \n \
    margin-right: 2px; \n \
    border: 1px solid rgba(0, 0, 0, 0); \n \
    vertical-align: middle; \n \
    border-radius: 2px; \n \
} \n \
.colorButton:hover { \n \
    border: 1px solid grey; \n \
} \n \
.iconButton { \n \
    border: 0px; \n \
    background: transparent; \n \
    font-size: 20px; \n \
    display: inline-block; \n \
    vertical-align: middle; \n \
    margin-right: 5px; \n \
    margin-left: 5px; \n \
} \n \
.iconButton:hover { \n \
    color: blue; \n \
} \n \
.assemblyIcon { \n \
    color: red; \n \
} \n \
.informationBarButton { \n \
    border: 0px; \n \
    background: transparent; \n \
    display: inline-block; \n \
    vertical-align: middle; \n \
    margin-right: 5px; \n \
    margin-top: 5px; \n \
} \n \
.informationBarButton:hover { \n \
    color: blue; \n \
} \n \
.assemblyIcon { \n \
    color: red; \n \
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
span.smallColorIndent { position: absolute; left: 35px; } \n \
span.bigColorIndent { position: absolute; left: 65px; } \n \
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
.CostTooltip { \n \
    width: 240px; \n \
} \n \
.conditionTooltip { \n \
    width: 300px; \n \
    padding: 5px; \n \
    font-family: Consolas, 'Liberation Mono', Menlo, Courier, monospace; \n \
} \n \
span.tooltipHelperText { \n \
    color: red; \n \
    margin-top: 5px; \n \
} \n \
";

const string StmtToViz::expandCodeJS = "\n \
// expand code div\n \
var codeDiv = document.getElementById('IRCode-code'); \n \
var prodConsDiv = document.getElementById('ProducerConsumerViz'); \n \
var resizeBar = document.getElementById('ResizeBar'); \n \
\n \
codeDiv.style.flexGrow = '0'; \n \
prodConsDiv.style.flexGrow = '0'; \n \
resizeBar.style.flexGrow = '0'; \n \
codeDiv.style.flexBasis = 'calc(50% - 16px)'; \n \
resizeBar.style.flexBasis = '16px'; \n \
prodConsDiv.style.flexBasis = 'calc(50% - 8px)'; \n \
\n \
resizeBar.addEventListener('mousedown', (event) => { \n \
    document.addEventListener('mousemove', resize, false); \n \
    document.addEventListener('mouseup', () => { \n \
        document.removeEventListener('mousemove', resize, false); \n \
    }, false); \n \
}); \n \
function resize(e) { \n \
    const size = `${e.x}px`; \n \
    codeDiv.style.flexBasis = 'calc(' + size + ' - 24px)'; \n \
    prodConsDiv.style.flexBasis = 'calc(100% - ' + size + ' + 8px)'; \n \
} \n \
function collapseCode() { \n \
    codeDiv.style.flexBasis = '0px'; \n \
    prodConsDiv.style.flexBasis = 'calc(100%)'; \n \
} \n \
function collapseViz() { \n \
    prodConsDiv.style.flexBasis = '0px'; \n \
    codeDiv.style.flexBasis = 'calc(100% - 16px)'; \n \
} \n \
";

const string StmtToViz::assemblyCodeJS = "\n \
// open assembly code  \n \
function openAssembly(lineNum) {\n \
    var innerHTML = '<head><link rel=\\'stylesheet\\'' +\n \
        'href=\\'https://cdnjs.cloudflare.com/ajax/libs/codemirror/5.52.2/codemirror.min.css\\'>' +\n \
        '</link>' +\n \
        '<scr' + 'ipt' +\n \
        '	src=\\'https://cdnjs.cloudflare.com/ajax/libs/codemirror/5.52.2/codemirror.min.js\\'></scr' + 'ipt>' +\n \
        '<scri' + 'pt' +\n \
        '	src=\\'https://cdnjs.cloudflare.com/ajax/libs/codemirror/6.65.7/mode/z80/z80.min.js\\'></scr' + 'ipt>' +\n \
        '<scri' + 'pt' +\n \
        '	src=\\'https://cdnjs.cloudflare.com/ajax/libs/codemirror/6.65.7/addon/selection/mark-selection.min.js\\'></scri' + 'pt>' +\n \
        '</head>';\n \
\n \
    innerHTML += '<style>' +\n \
        '    .CodeMirror {' +\n \
        '        height: 100%;' +\n \
        '        width: 100%;' +\n \
        '    }' +\n \
        '    .styled-background {' +\n \
        '        background-color: #ff7;' +\n \
        '    }' +\n \
        '</style>';\n \
\n \
    var assembly = document.getElementById('assemblyContent');\n \
    console.log(assembly);\n \
    innerHTML += '<div id=\\'codeValue\\' style=\\'display: none\\'>' + assembly.innerHTML + '</div>';\n \
    innerHTML += '<scr' + 'ipt>' +\n \
        'var codeHTML = document.getElementById(\\'codeValue\\'); ' +\n \
        'var code = codeHTML.textContent; ' +\n \
        'code = code.trimLeft(); ' +\n \
        'var myCodeMirror = CodeMirror(document.body, {value: code, lineNumbers: true, mode: \\'z80\\', readOnly: true,});' +\n \
        'function jumpToLine(i) {' +\n \
        '	i -= 1;' +\n \
        '	var t = myCodeMirror.charCoords({ line: i, ch: 0 }, \\'local\\').top;' +\n \
        '	var middleHeight = myCodeMirror.getScrollerElement().offsetHeight / 2;' +\n \
        '	myCodeMirror.scrollIntoView({ line: i+20, ch: 0 });' +\n \
        '	myCodeMirror.markText({ line: i, ch: 0 }, { line: i, ch: 200 }, { className: \\'styled-background\\' });' +\n \
        '}' +\n \
        'jumpToLine('+lineNum+');' +\n \
\n \
        '</scri' + 'pt>';\n \
\n \
    var newWindow = window.open('', '_blank');\n \
    newWindow.document.write(innerHTML);\n \
}\n \
";

const string StmtToViz::js = "\n \
/* Expand/Collapse buttons */\n \
function toggle(id, buttonId) { \n \
    e = document.getElementById(id); \n \
    show = document.getElementById(id + '-show'); \n \
    hide = document.getElementById(id + '-hide'); \n \
    button1 = document.getElementById('button' + buttonId); \n \
    button2 = document.getElementById('button' + (buttonId - 1)); \n \
    if (e.style.visibility != 'hidden') { \n \
        e.style.height = '0px'; \n \
        e.style.visibility = 'hidden'; \n \
        show.style.display = 'block'; \n \
        hide.style.display = 'none'; \n \
        // make inclusive  \n \
        if (button1 != null && button2 != null) { \n \
            inclusiverange1 = button1.getAttribute('inclusiverange'); \n \
            newClassName = button1.className.replace(/CostColor\\d+/, 'CostColor' + inclusiverange1); \n \
            button1.className = newClassName; \n \
            inclusiverange2 = button2.getAttribute('inclusiverange'); \n \
            newClassName = button2.className.replace(/CostColor\\d+/, 'CostColor' + inclusiverange2); \n \
            button2.className = newClassName; \n \
        } \n \
    } else { \n \
        e.style = ''; \n \
        show.style.display = 'none'; \n \
        hide.style.display = 'block'; \n \
        // make exclusive  \n \
        if (button1 != null && button2 != null) { \n \
            exclusiverange1 = button1.getAttribute('exclusiverange'); \n \
            newClassName = button1.className.replace(/CostColor\\d+/, 'CostColor' + exclusiverange1); \n \
            button1.className = newClassName; \n \
            exclusiverange2 = button2.getAttribute('exclusiverange'); \n \
            newClassName = button2.className.replace(/CostColor\\d+/, 'CostColor' + exclusiverange2); \n \
            button2.className = newClassName; \n \
        } \n \
    } \n \
    return false; \n \
} \n \
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
