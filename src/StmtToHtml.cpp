#include "StmtToHtml.h"
#include "IROperator.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>



namespace Halide {
namespace Internal {

using std::string;


namespace {
template <typename T>
std::string to_string(T value) {
    std::ostringstream os ;
    os << value ;
    return os.str() ;
}

class StmtToHtml : public IRVisitor {

    static const std::string css, js;

    // This allows easier access to individual elements.
    int id_count;

private:
    std::ofstream stream;

    int unique_id() { return id_count++; }

    string open_span(string cls, int id = -1) {
        if (id == -1) {
            id = unique_id();
        }
        return "<span class=\""+cls+"\" id=\""+ to_string(id) +"\">";
    }

    string close_span() {
        return "</span>";
    }

    string open_div(string cls, int id = -1) {
        if (id == -1) {
            id = unique_id();
        }
        return "<div class=\""+cls+"\" id=\""+ to_string(id) +"\">";
    }

    string close_div() {
        return "</div>\n";
    }

    string keyword(string k) {
        return open_span("Keyword") + k + close_span();
    }

    string type(string t) {
        return open_span("Type") + t + close_span();
    }

    string symbol(string s) {
        return open_span("Symbol") + s + close_span();
    }

    string var(string v) {
        return open_span("Variable") + v + close_span();
    }

    void print(Expr ir) {
        ir.accept(this);
    }

    void print(Stmt ir) {
        ir.accept(this);
    }

    string open_expand_button(int &id) {
        id = unique_id();
        std::stringstream button;
        button << "<a class=ExpandButton onclick=\"return toggle(" << id << ");\" href=_blank>"
               << "<div style=\"position:relative; width:0; height:0;\">"
               << "<div class=ShowHide style=\"display:none;\" id=" << id << "-show" << "><i class=\"fa fa-plus-square-o\"></i></div>"
               << "<div class=ShowHide id=" << id << "-hide" << "><i class=\"fa fa-minus-square-o\"></i></div>"
               << "</div>";
        return button.str();
    }

    string close_expand_button() {
        return "</a>";
    }

public:
    void visit(const IntImm *op){
        stream <<  open_span("IntImm Imm") << op->value << close_span();
    }
    void visit(const FloatImm *op){
        stream <<  open_span("FloatImm Imm") << op->value << 'f' << close_span();
    }
    void visit(const StringImm *op){
        stream << open_span("StringImm");
        stream << '"';
        for (size_t i = 0; i < op->value.size(); i++) {
            unsigned char c = op->value[i];
            if (c >= ' ' && c <= '~' && c != '\\' && c != '"') {
                stream << c;
            } else {
                stream << '\\';
                switch (c) {
                case '"':
                    stream << '"';
                    break;
                case '\\':
                    stream << '\\';
                    break;
                case '\t':
                    stream << 't';
                    break;
                case '\r':
                    stream << 'r';
                    break;
                case '\n':
                    stream << 'n';
                    break;
                default:
                    string hex_digits = "0123456789ABCDEF";
                    stream << 'x' << hex_digits[c >> 4] << hex_digits[c & 0xf];
                }
            }
        }
        stream << '"';
        stream << close_span();
    }

    void visit(const Variable *op){
        stream << var(op->name);
    }

    void visit(const Cast *op){
        stream << open_span("Cast");
        stream << open_span("Type") << op->type << close_span();
        stream << '(';
        print(op->value);
        stream << ')';
        stream << close_span();
    }

    void visit_binary_op(Expr a, Expr b, const char *op) {
        stream << open_span("BinaryOp");
        stream << '(';
        print(a);
        stream << " " << open_span("Operator") << op << close_span() << " ";
        print(b);
        stream << ')';
        stream << close_span();
    }

    void visit(const Add *op) { visit_binary_op(op->a, op->b, "+"); }
    void visit(const Sub *op) { visit_binary_op(op->a, op->b, "-"); }
    void visit(const Mul *op) { visit_binary_op(op->a, op->b, "*"); }
    void visit(const Div *op) { visit_binary_op(op->a, op->b, "/"); }
    void visit(const Mod *op) { visit_binary_op(op->a, op->b, "%"); }
    void visit(const And *op) { visit_binary_op(op->a, op->b, "&amp;&amp;"); }
    void visit(const Or *op) { visit_binary_op(op->a, op->b, "||"); }
    void visit(const NE *op) { visit_binary_op(op->a, op->b, "!="); }
    void visit(const LT *op) { visit_binary_op(op->a, op->b, "&lt;"); }
    void visit(const LE *op) { visit_binary_op(op->a, op->b, "&lt="); }
    void visit(const GT *op) { visit_binary_op(op->a, op->b, "&gt;"); }
    void visit(const GE *op) { visit_binary_op(op->a, op->b, "&gt;="); }
    void visit(const EQ *op) { visit_binary_op(op->a, op->b, "=="); }

    void visit(const Min *op) {
        stream << open_span("Min");
        stream << symbol("min") << "(";
        print(op->a);
        stream << ", ";
        print(op->b);
        stream << ")";
        stream << close_span();
    }
    void visit(const Max *op) {
        stream << open_span("Max");
        stream << symbol("max") << "(";
        print(op->a);
        stream << ", ";
        print(op->b);
        stream << ")";
        stream << close_span();
    }
    void visit(const Not *op) {
        stream << open_span("Not");
        stream << '!';
        print(op->a);
        stream << close_span();
    }
    void visit(const Select *op) {
        stream << open_span("Select");
        stream << symbol("select") << "(";
        print(op->condition);
        stream << ", ";
        print(op->true_value);
        stream << ", ";
        print(op->false_value);
        stream << ")";
        stream << close_span();
    }
    void visit(const Load *op) {
        stream << open_span("Load");
        stream << var(op->name);
        stream << "[";
        print(op->index);
        stream << "]";
        stream << close_span();
    }
    void visit(const Ramp *op) {
        stream << open_span("Ramp");
        stream << symbol("ramp") << "<b>(</b>";
        print(op->base);
        stream << ", ";
        print(op->stride);
        stream << ", ";
        print(op->width);
        stream << "<b>)</b>";
        stream << close_span();
    }
    void visit(const Broadcast *op) {
        stream << open_span("Broadcast");
        stream << open_span("function");
        stream << "x" << op->width;
        stream << close_span();
        stream << "<b>(</b>";
        print(op->value);
        stream << "<b>)</b>";
        stream << close_span();
    }
    void visit(const Call *op) {
        stream << open_span("Call");
        if (op->call_type == Call::Intrinsic) {
            if (op->name == Call::extract_buffer_min) {
                print(op->args[0]);
                stream << ".min[";
                print(op->args[1]);
                stream << "]";
                stream << close_span();
                return;
            } else if (op->name == Call::extract_buffer_max) {
                print(op->args[0]);
                stream << ".max[";
                print(op->args[1]);
                stream << "]";
                stream << close_span();
                return;
            }
        }
        stream << symbol(op->name);
        stream << "<b>(</b>";
        stream << open_span("CallArgs");
        for (size_t i = 0; i < op->args.size(); i++) {
            print(op->args[i]);
            if (i < op->args.size() - 1) {
                stream << ", ";
            }
        }
        stream << close_span();
        stream << "<b>)</b>";
        stream << close_span();
    }
    void visit(const Let *op) {
        stream << open_span("Let");
        stream << "(" << keyword("let") << " ";
        stream << var(op->name);
        stream << " = ";
        print(op->value);
        stream << " " << keyword("in") << " ";
        print(op->body);
        stream << ")";
        stream << close_span();
    }

    // Divs
    void visit(const LetStmt *op) {
        stream << open_div("LetStmt");
        stream << keyword("let") << " ";
        stream << var(op->name);
        stream << " " << open_span("Operator") << "=" << close_span() << " ";
        print(op->value);
        op->body.accept(this);
        stream << close_div();
    }
    void visit(const AssertStmt *op) {
        stream << open_div("AssertStmt");
        stream << symbol("assert") << "<b>(</b>";
        print(op->condition);
        stream << ", ";
        print(op->message);
        for (size_t i = 0; i < op->args.size(); i++) {
            stream << ", ";
            print(op->args[i]);
        }
        stream << "<b>)</b>";
        stream << close_div();
    }
    void visit(const Pipeline *op) {
        stream << open_div("Produce");
        int produce_id = 0;
        stream << open_expand_button(produce_id);
        stream << keyword("produce")
               << " "
               << var(op->name);
        stream << close_expand_button();
        stream << " {";
        stream << open_div("ProduceBody Indent", produce_id);
        print(op->produce);
        stream << close_div();
        stream << '}';
        stream << close_div();
        if (op->update.defined()) {
            stream << open_div("Update");
            int update_id = 0;
            stream << open_expand_button(update_id);
            stream << keyword("update")
                   << " "
                   << var(op->name);
            stream << close_expand_button();
            stream << " {";
            stream << open_div("UpdateBody Indent", update_id);
            print(op->update);
            stream << close_div();
            stream << '}';
            stream << close_div();
        }
        print(op->consume);

    }
    void visit(const For *op) {
        stream << open_div("For");

        int id = 0;
        stream << open_expand_button(id);
        if (op->for_type == 0) {
            stream << keyword("for");
        } else {
            stream << keyword("parallel");
        }
        stream << " (";
        stream << var(op->name);
        stream << ", ";
        print(op->min);
        stream << ", ";
        print(op->extent);
        stream << ")";
        stream << close_expand_button();
        stream << " {";
        stream << open_div("ForBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << '}';
        stream << close_div();
    }
    void visit(const Store *op) {
        stream << open_div("Store");
        stream << var(op->name);
        stream << "[";
        stream << open_span("StoreIndex");
        print(op->index);
        stream << close_span();
        stream << "] " << open_span("Operator") << "=" << close_span() << " ";
        stream << open_span("StoreValue");
        print(op->value);
        stream << close_span();
        stream << close_div();
    }
    void visit(const Provide *op) {
        stream << open_div("Provide");
        stream << var(op->name);
        stream << "(";
        for (size_t i = 0; i < op->args.size(); i++) {
            print(op->args[i]);
            if (i < op->args.size() - 1) stream << ", ";
        }
        stream << ") = ";
        if (op->values.size() > 1) {
            stream << "{";
            stream << open_span("ProvideValues");
        }
        for (size_t i = 0; i < op->values.size(); i++) {
            if (i > 0) {
                stream << ", ";
            }
            print(op->values[i]);
        }
        if (op->values.size() > 1) {
            stream << close_span();
            stream << "}";
        }
        stream << close_div();
    }
    void visit(const Allocate *op) {
        stream << open_div("Allocate");
        stream << keyword("allocate") << " ";
        stream << var(op->name);
        stream << "[";
        stream << open_span("Type") << op->type << close_span();
        for (size_t i = 0; i < op->extents.size(); i++) {
            stream  << " * ";
            print(op->extents[i]);
        }
        stream << "]";
        if (!is_one(op->condition)) {
            stream << " if ";
            print(op->condition);
        }
        stream << open_div("AllocateBody");
        print(op->body);
        stream << close_div();
        stream << close_div();
    }
    void visit(const Free *op) {
        stream << open_div("Free");
        stream << keyword("free") << " ";
        stream << var(op->name);
        stream << close_div();
    }
    void visit(const Realize *op) {
        stream << open_div("Realize");
        int id;
        stream << open_expand_button(id);
        stream << keyword("realize") << " " << var(op->name) << "(";
        stream << open_span("RealizeArgs");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            stream << "[";
            print(op->bounds[i].min);
            stream << ", ";
            print(op->bounds[i].extent);
            stream << "]";
            if (i < op->bounds.size() - 1) stream << ", ";
        }
        stream << ")";
        stream << close_span();
        if (!is_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        stream << close_expand_button();

        stream << " {";
        stream << open_div("RealizeBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << close_div();
    }
    void visit(const Block *op) {
        stream << open_div("Block");
        print(op->first);
        if (op->rest.defined()) print(op->rest);
        stream << close_div();
    }
    void visit(const IfThenElse *op) {
        stream << open_div("IfThenElse");
        stream << keyword("if") << " (";
        stream << open_span("IfStmt");
        while (1) {
            print(op->condition);
            stream << ")";
            stream << close_span() << "{"; // close if (or else if) span
            stream << open_div("ThenBody Indent");
            print(op->then_case);
            stream << close_div(); // close thenbody div

            if (!op->else_case.defined()) {
                break;
            }

            if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
                stream << "} " << keyword("else if") << " (";
                stream << open_span("ElseIfStmt");
                op = nested_if;
            } else {
                stream << "} " << keyword("else") << " {";
                stream << open_div("ElseBody Indent");
                print(op->else_case);
                stream << close_div();
                break;
            }
        }
        stream << close_div(); // Closing ifthenelse div.
    }

    void visit(const Evaluate *op) {
        stream << open_div("Evaluate");
        print(op->value);
        stream << close_div();
    }

    StmtToHtml(string filename){
        id_count = 0;
        stream.open(filename.c_str());
        stream << "<head>";
        stream << "<style type=\"text/css\">" << css << "</style>\n";
        stream << "<script language=\"javascript\" type=\"text/javascript\">" + js + "</script>";
        stream <<"<link rel=\"stylesheet\" type=\"text/css\" href=\"my.css\">";
        stream << "<script language=\"javascript\" type=\"text/javascript\" src=\"my.js\"></script>";
        stream << "<link href=\"http://maxcdn.bootstrapcdn.com/font-awesome/4.1.0/css/font-awesome.min.css\" rel=\"stylesheet\">\n";
        stream << "</head>\n <body>\n";
    }

    void generate(Stmt s){
        print(s);
        stream << "</body>";
        stream.close();
    }

    ~StmtToHtml(){}
};

const std::string StmtToHtml::css = "\n \
body { font-family: Consolas, \"Liberation Mono\", Menlo, Courier, monospace; font-size: 12px; background: #f8f8f8; } \n \
a, a:hover, a:visited, a:active { color: inherit; text-decoration: none; } \n \
div.Indent { padding-left: 15px; }\n \
div.ShowHide { position:absolute; left:-12px; top:-1px; width:12px; height:12px; } \n \
span.Comment { color: #998; font-style: italic; }\n \
span.Keyword { color: #333; font-weight: bold; }\n \
span.Symbol { color: #990073; }\n \
span.Type { color: #445588; font-weight: bold; }\n \
span.StringImm { color: #d14; }\n \
span.IntImm { color: #099; }\n \
span.FloatImm { color: #099; }\n \
";

const std::string StmtToHtml::js = "\n \
window.onload = function () { \n \
// adding jquery \n \
var script = document.createElement('script'); \n \
script.src = 'http://code.jquery.com/jquery-2.1.1.js'; \n \
script.type = 'text/javascript'; \n \
document.getElementsByTagName('head')[0].appendChild(script); \n \
fold = function(selector) { \n \
    selector.each(function() { \n \
        $(this).attr('title', $(this).text().replace(/\"/g, \"'\")); \n \
        $(this).text(\"...\"); \n \
    }); \n \
}; \n \
unfold = function(select) { \n \
    selector.each(function() { \n \
        $(this).text($(this).attr('title')); \n \
        // $(this).attr('title', $(this).text().replace('\"',\"'\")); \n \
        // $(this).text(\"...\"); \n \
    }); \n \
}; \n \
foldClass = function(className) { fold($('.'+className)); }; \n \
unfoldClass = function(className) { unfold($('.'+className)); }; \n \
};\n \
function toggle(id) { \n \
    e = document.getElementById(id); \n \
    show = document.getElementById(id + '-show'); \n \
    hide = document.getElementById(id + '-hide'); \n \
    if (e.style.display != 'none') { \n \
        e.style.display = 'none'; \n \
        show.style.display = 'block'; \n \
        hide.style.display = 'none'; \n \
    } else { \n \
        e.style.display = 'block'; \n \
        show.style.display = 'none'; \n \
        hide.style.display = 'block'; \n \
    } \n \
    return false; \n \
}";
}

void print_to_html(string filename, Stmt s) {
    StmtToHtml sth(filename);
    sth.generate(s);
}

}
}
