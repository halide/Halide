#include "StmtToHtml.h"
#include "IRVisitor.h"
#include "IROperator.h"
#include "Scope.h"

#include <iterator>
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

    int unique_id() { return ++id_count; }

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

    string open_div(const string &cls, int id = -1) {
        return open_tag("div", cls, id) + "\n";
    }
    string close_div() {
        return close_tag("div") + "\n";
    }

    string open_line() { return "<p class=WrapLine>"; }
    string close_line() { return "</p>"; }

    string keyword(const string &x) { return span("Keyword", x); }
    string type(const string &x) { return span("Type", x); }
    string symbol(const string &x) { return span("Symbol", x); }

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
               << "<div style='position:relative; width:0; height:0;'>"
               << "<div class=ShowHide style='display:none;' id=" << id << "-show" << "><i class='fa fa-plus-square-o'></i></div>"
               << "<div class=ShowHide id=" << id << "-hide" << "><i class='fa fa-minus-square-o'></i></div>"
               << "</div>";
        return button.str();
    }

    string close_expand_button() {
        return "</a>";
    }

    void visit(const IntImm *op){
        stream << open_span("IntImm Imm");
        stream << Expr(op);
        stream << close_span();
    }

    void visit(const UIntImm *op){
        stream << open_span("UIntImm Imm");
        stream << Expr(op);
        stream << close_span();
    }

    void visit(const FloatImm *op){
        stream << open_span("FloatImm Imm");
        stream << Expr(op);
        stream << close_span();
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

        stream << open_span("Matched");
        stream << open_span("Type") << op->type << close_span();
        stream << "(";
        stream << close_span();
        print(op->value);
        stream << matched(")");

        stream << close_span();
    }

    void visit_binary_op(Expr a, Expr b, const char *op) {
        stream << open_span("BinaryOp");

        stream << matched("(");
        print(a);
        stream << " " << matched("Operator", op) << " ";
        print(b);
        stream << matched(")");

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
        print_list(symbol("min") + "(", {op->a, op->b}, ")");
        stream << close_span();
    }
    void visit(const Max *op) {
        stream << open_span("Max");
        print_list(symbol("max") + "(", {op->a, op->b}, ")");
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
        print_list(symbol("select") + "(", {op->condition, op->true_value, op->false_value}, ")");
        stream << close_span();
    }
    void visit(const Load *op) {
        stream << open_span("Load");
        stream << open_span("Matched");
        stream << var(op->name) << "[";
        stream << close_span();
        print(op->index);
        stream << matched("]");
        if (!is_one(op->predicate)) {
            stream << " " << keyword("if") << " ";
            print(op->predicate);
        }
        stream << close_span();
    }
    void visit(const Ramp *op) {
        stream << open_span("Ramp");
        print_list(symbol("ramp") + "(", {op->base, op->stride, Expr(op->lanes)}, ")");
        stream << close_span();
    }
    void visit(const Broadcast *op) {
        stream << open_span("Broadcast");
        stream << open_span("Matched");
        stream << symbol("x") << op->lanes << "(";
        stream << close_span();
        print(op->value);
        stream << matched(")");
        stream << close_span();
    }
    void visit(const Call *op) {
        stream << open_span("Call");
        print_list(symbol(op->name) + "(", op->args, ")");
        stream << close_span();
    }

    void visit(const Let *op) {
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
    void visit(const LetStmt *op) {
        scope.push(op->name, unique_id());
        stream << open_div("LetStmt") << open_line();
        stream << open_span("Matched");
        stream << keyword("let") << " ";
        stream << var(op->name);
        stream << close_span();
        stream << " " << matched("Operator Assign", "=") << " ";
        print(op->value);
        stream << close_line();
        print(op->body);
        stream << close_div();
        scope.pop(op->name);
    }
    void visit(const AssertStmt *op) {
        stream << open_div("AssertStmt WrapLine");
        std::vector<Expr> args;
        args.push_back(op->condition);
        args.push_back(op->message);
        print_list(symbol("assert") + "(", args, ")");
        stream << close_div();
    }
    void visit(const ProducerConsumer *op) {
        scope.push(op->name, unique_id());
        stream << open_div(op->is_producer ? "Produce" : "Consumer");
        int produce_id = unique_id();
        stream << open_span("Matched");
        stream << open_expand_button(produce_id);
        stream << keyword(op->is_producer ? "produce" : "consume") << " ";
        stream << var(op->name);
        stream << close_expand_button() << " {";
        stream << close_span();;
        stream << open_div(op->is_producer ? "ProduceBody Indent" : "ConsumeBody Indent", produce_id);
        print(op->body);
        stream << close_div();
        stream << matched("}");
        stream << close_div();
        scope.pop(op->name);
    }

    void visit(const For *op) {
        scope.push(op->name, unique_id());
        stream << open_div("For");

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
            internal_assert(false) << "Unknown for type: " << ((int)op->for_type) << "\n";
        }
        stream << " (";
        stream << close_span();
        print_list({Variable::make(Int(32), op->name), op->min, op->extent});
        stream << matched(")");
        stream << close_expand_button();
        stream << " " << matched("{");
        stream << open_div("ForBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << matched("}");

        stream << close_div();
        scope.pop(op->name);
    }

    void visit(const Acquire *op) {
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
        stream << matched("}");
        stream << close_div();
    }

    void visit(const Store *op) {
        stream << open_div("Store WrapLine");
        stream << open_span("Matched");
        stream << var(op->name) << "[";
        stream << close_span();
        print(op->index);
        stream << matched("]");
        stream << " " << span("Operator Assign Matched", "=") << " ";
        stream << open_span("StoreValue");
        print(op->value);
        if (!is_one(op->predicate)) {
            stream << " " << keyword("if") << " ";
            print(op->predicate);
        }
        stream << close_span();
        stream << close_div();
    }
    void visit(const Provide *op) {
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
    void visit(const Allocate *op) {
        scope.push(op->name, unique_id());
        stream << open_div("Allocate");
        stream << open_span("Matched");
        stream << keyword("allocate") << " ";
        stream << var(op->name) << "[";
        stream << close_span();

        stream << open_span("Type");
        stream << op->type;
        stream << close_span();

        for (size_t i = 0; i < op->extents.size(); i++) {
            stream  << " * ";
            print(op->extents[i]);
        }
        stream << matched("]");
        if (!is_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        if (op->new_expr.defined()) {
            stream << open_span("Matched");
            stream << keyword("custom_new") << "{";
            print(op->new_expr);
            stream << matched("}");
        }
        if (!op->free_function.empty()) {
            stream << open_span("Matched");
            stream << keyword("custom_delete") << "{ " << op->free_function << "(); ";
            stream << matched("}");
        }

        stream << open_div("AllocateBody");
        print(op->body);
        stream << close_div();

        stream << close_div();
        scope.pop(op->name);
    }
    void visit(const Free *op) {
        stream << open_div("Free WrapLine");
        stream << keyword("free") << " ";
        stream << var(op->name);
        stream << close_div();
    }
    void visit(const Realize *op) {
        scope.push(op->name, unique_id());
        stream << open_div("Realize");
        int id = unique_id();
        stream << open_expand_button(id);
        stream << keyword("realize") << " ";
        stream << var(op->name);
        stream << matched("(");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_list("[", {op->bounds[i].min, op->bounds[i].extent}, "]");
            if (i < op->bounds.size() - 1) stream << ", ";
        }
        stream << matched(")");
        if (!is_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        stream << close_expand_button();

        stream << " " << matched("{");
        stream << open_div("RealizeBody Indent", id);
        print(op->body);
        stream << close_div();
        stream << matched("}");
        stream << close_div();
        scope.pop(op->name);
    }

    void visit(const Prefetch *op) {
        stream << open_span("Prefetch");
        stream << keyword("prefetch") << " ";
        stream << var(op->name);
        stream << matched("(");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_list("[", {op->bounds[i].min, op->bounds[i].extent}, "]");
            if (i < op->bounds.size() - 1) stream << ", ";
        }
        stream << matched(")");
        if (!is_one(op->condition)) {
            stream << " " << keyword("if") << " ";
            print(op->condition);
        }
        stream << close_span();

        stream << open_div("PrefetchBody");
        print(op->body);
        stream << close_div();
    }

    // To avoid generating ridiculously deep DOMs, we flatten blocks here.
    void visit_block_stmt(Stmt stmt) {
        if (const Block *b = stmt.as<Block>()) {
            visit_block_stmt(b->first);
            visit_block_stmt(b->rest);
        } else if (stmt.defined()) {
            print(stmt);
        }
    }
    void visit(const Block *op) {
        stream << open_div("Block");
        visit_block_stmt(op->first);
        visit_block_stmt(op->rest);
        stream << close_div();
    }

    // We also flatten forks
    void visit_fork_stmt(Stmt stmt) {
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
            stream << matched("}");
            stream << close_div();
        }
    }
    void visit(const Fork *op) {
        stream << open_div("Fork");
        int id = unique_id();
        stream << open_expand_button(id);
        stream << keyword("fork") << " " << matched("{");
        stream << close_expand_button();
        stream << open_div("Fork Indent", id);
        visit_fork_stmt(op->first);
        visit_fork_stmt(op->rest);
        stream << close_div();
        stream << matched("}");
        stream << close_div();
    }

    void visit(const IfThenElse *op) {
        stream << open_div("IfThenElse");
        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_span("Matched");
        stream << keyword("if") << " (";
        stream << close_span();
        while (1) {
            print(op->condition);
            stream << matched(")");
            stream << close_expand_button() << " ";
            stream << matched("{"); // close if (or else if) span

            stream << open_div("ThenBody Indent", id);
            print(op->then_case);
            stream << close_div(); // close thenbody div

            if (!op->else_case.defined()) {
                stream << matched("}");
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
        stream << close_div(); // Closing ifthenelse div.
    }

    void visit(const Evaluate *op) {
        stream << open_div("Evaluate");
        print(op->value);
        stream << close_div();
    }

    void visit(const Shuffle *op) {
        stream << open_span("Shuffle");
        if (op->is_concat()) {
            print_list(symbol("concat_vectors("), op->vectors, ")");
        } else if (op->is_interleave()) {
            print_list(symbol("interleave_vectors("), op->vectors, ")");
        } else if (op->is_extract_element()) {
            std::vector<Expr> args = op->vectors;
            args.push_back(op->slice_begin());
            print_list(symbol("extract_element("), args, ")");
        } else if (op->is_slice()) {
            std::vector<Expr> args = op->vectors;
            args.push_back(op->slice_begin());
            args.push_back(op->slice_stride());
            args.push_back(static_cast<int>(op->indices.size()));
            print_list(symbol("slice_vectors("), args, ")");
        } else {
            std::vector<Expr> args = op->vectors;
            for (int i : op->indices) {
                args.push_back(i);
            }
            print_list(symbol("shuffle("), args, ")");
        }
        stream << close_span();
    }

public:
    void print(Expr ir) {
        ir.accept(this);
    }

    void print(Stmt ir) {
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
        print(op.body);
        stream << close_div();
        stream << matched("}");

        stream << close_div();
        scope.pop(op.name);
    }

    void print(const Buffer<> &op) {
        stream << open_div("Buffer<>");
        stream << keyword("buffer ") << var(op.name());
        stream << close_div();
    }

    void print(const Module& m) {
        scope.push(m.name(), unique_id());
        for (const auto &s : m.submodules()) {
            print(s);
        }

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_div("Module");
        stream << open_span("Matched");
        stream << keyword("module") << " name=" << m.name() << ", target=" << m.target().to_string();
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
        stream << matched("}");
        stream << close_div();
        scope.pop(m.name());
    }

    StmtToHtml(string filename) : id_count(0), context_stack(1, 0) {
        stream.open(filename.c_str());
        stream << "<head>";
        stream << "<style type='text/css'>" << css << "</style>\n";
        stream << "<script language='javascript' type='text/javascript'>" + js + "</script>\n";
        stream <<"<link rel='stylesheet' type='text/css' href='my.css'>\n";
        stream << "<script language='javascript' type='text/javascript' src='my.js'></script>\n";
        stream << "<link href='http://maxcdn.bootstrapcdn.com/font-awesome/4.1.0/css/font-awesome.min.css' rel='stylesheet'>\n";
        stream << "<script src='http://code.jquery.com/jquery-1.10.2.js'></script>\n";
        stream << "</head>\n <body>\n";
    }

    ~StmtToHtml() {
        stream << "<script>\n"
               << "$( '.Matched' ).each( function() {\n"
               << "    this.onmouseover = function() { $('.Matched[id^=' + this.id.split('-')[0] + '-]').addClass('Highlight'); }\n"
               << "    this.onmouseout = function() { $('.Matched[id^=' + this.id.split('-')[0] + '-]').removeClass('Highlight'); }\n"
               << "} );\n"
               << "</script>\n";
        stream << "</body>";
    }
};

const std::string StmtToHtml::css = "\n \
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
";

const std::string StmtToHtml::js = "\n \
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
    sth.print(s);
}

void print_to_html(string filename, const Module &m) {
    StmtToHtml sth(filename);
    sth.print(m);
}

}
}
