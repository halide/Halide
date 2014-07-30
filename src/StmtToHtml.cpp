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

    string open_span(string cls, string data="") {
        id_count++;
        return "<span class=\""+cls+"\" "+data+" id=\""+ to_string(id_count) +"\">";
    }

    string close_span() {
        return "</span>";
    }

    string open_div(string cls, string data="") {
        id_count++;
        return "<div class=\""+cls+"\" \""+data+"\" id=\""+ to_string(id_count) +"\">";
    }

    string close_div() {
        return "</div>\n";
    }

    string generate_data(string name, string content) {
        return "data-"+name+"=\""+content+'"';
    }

    string keyword(const char *k) {
        return open_span("Keyword") + k + close_span();
    }

    void print(Expr ir) {
        ir.accept(this);
    }

    void print(Stmt ir) {
        ir.accept(this);
    }

public:
    void visit(const IntImm *op){
        stream <<  open_span("IntImm") << op->value << close_span();
    }
    void visit(const FloatImm *op){
        stream <<  open_span("FloatImm") << op->value << 'f' << close_span();
    }
    void visit(const StringImm *op){
        //TODO Sanitize the string so it doesn't mess with the html
        // This would be the proper way to modify string imm
        // however this means that every function name and varaible name would have
        // double quotes surrounding it which is kinda annoying.
        /*
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
        */

        // This is a temporary solution.
        stream << open_span("StringImm");
        stream << op->value;
        stream << close_span();
    }

    void visit(const Variable *op){
        stream << open_span("Variable") << op->name << close_span();
    }

    void visit(const Cast *op){
        stream << open_span("Cast");
        stream << op->type << '(';
        print(op->value);
        stream << ')' << close_span();
    }

    void visit_binary_op(Expr a, Expr b, const char * op) {
        stream << open_span("BinaryOp");
        stream << '(';
        print(a);
        stream << " " << op << " ";
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
        stream << "min(";
        print(op->a);
        stream << ", ";
        print(op->b);
        stream << ")";
        stream << close_span();
    }
    void visit(const Max *op) {
        stream << open_span("Max");
        stream << "max(";
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
        stream << "select(";
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
        print(op->name);
        stream << "[";
        print(op->index);
        stream << "]";
        stream << close_span();
    }
    void visit(const Ramp *op) {
        stream << open_span("Ramp");
        stream << "ramp(";
        print(op->base);
        stream << ", ";
        print(op->stride);
        stream << ", ";
        print(op->width);
        stream << ")";
        stream << close_span();
    }
    void visit(const Broadcast *op) {
        stream << open_span("Broadcast");
        stream << "x";
        print(op->width);
        stream << "(";
        print(op->value);
        stream << ")";
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

        print(op->name);
        stream << "(";
        stream << open_span("CallArgs");
        for (size_t i = 0; i < op->args.size(); i++) {
            print(op->args[i]);
            if (i < op->args.size() - 1) {
                stream << ", ";
            }
        }
        stream << close_span();
        stream << ")";
        stream << close_span();
    }
    void visit(const Let *op) {
        stream << open_span("Let");
        stream << "(" << keyword("let") << " ";
        print(op->name);
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
        print(op->name);
        stream << " = ";
        print(op->value);
        stream << close_div();
        op->body.accept(this);
    }
    void visit(const AssertStmt *op) {
        stream << open_div("AssertStmt");
        stream << "assert(";
        print(op->condition);
        stream << ", ";
        stream << open_span("AssertMsg");
        stream << '"' << op->message << '"';
        stream << close_span();
        for (size_t i = 0; i < op->args.size(); i++) {
            stream << ", ";
            print(op->args[i]);
        }
        stream << ')';
        stream << close_div();
    }
    void visit(const Pipeline *op) {
        stream << open_div("Produce");
        stream << keyword("produce") << " ";
        print(op->name);
        stream << " {";
        stream << open_div("ProduceBody Indent");
        print(op->produce);
        stream << close_div();
        stream << '}';
        stream << close_div();
        if (op->update.defined()) {
            stream << open_div("Update");
            stream << keyword("update") << " ";
            print(op->name);
            stream << " {";
            stream << open_div("UpdateBody Indent");
            print(op->update);
            stream << close_div();
            stream << '}';
            stream << close_div();
        }
        print(op->consume);

    }
    void visit(const For *op) {
        stream << open_div("For");
        if (op->for_type == 0) {
            stream << keyword("for");
        } else {
            stream << keyword("parallel");
        }
        stream << " (";
        print(op->name);
        stream << ", ";
        print(op->min);
        stream << ", ";
        print(op->extent);
        stream << ") {";
        stream << open_div("ForBody Indent");
        print(op->body);
        stream << close_div();
        stream << '}';
        stream << close_div();
    }
    void visit(const Store *op) {
        stream << open_div("Store");
        print(op->name);
        stream << "[";
        stream << open_span("StoreIndex");
        print(op->index);
        stream << close_span();
        stream << "] = ";
        stream << open_span("StoreValue");
        print(op->value);
        stream << close_span();
        stream << close_div();
    }
    void visit(const Provide *op) {
        stream << open_div("Provide");
        print(op->name);
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
        print(op->name);
        stream << "[" << op->type;
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
        print(op->name);
        stream << close_div();
    }
    void visit(const Realize *op) {
        stream << open_div("Realize");
        stream << keyword("realize") << " ";
        print(op->name);
        stream << "(";
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
        stream << " {";
        stream << open_div("RealizeBody Indent");
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
body { font-family: \"Courier New\" } \n \
div.Indent { padding-left: 15px; }\n \
span.Keyword { color: blue; font-weight: bold; }\n \
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
};";
}

void print_to_html(string filename, Stmt s) {
    StmtToHtml sth(filename);
    sth.generate(s);
}

}
}
