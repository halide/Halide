#include "StmtToViz.h"
#include "Debug.h"
#include "Error.h"
#include "FindStmtCost.h"
#include "GetAssemblyInfoViz.h"
#include "GetStmtHierarchy.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "IRPrinter.h"
#include "IRVisualization.h"
#include "Module.h"
#include "Scope.h"
#include "Substitute.h"
#include "Util.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <utility>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;

// Classes define within this file
class HTMLCodePrinter;
class HTMLVisualizationPrinter;
class IRVisualizer;

/************** HTMLCodePrinter Class **************/

class HTMLCodePrinter : public IRVisitor {
public:
    HTMLCodePrinter(std::ofstream &os)
        : stream(os), _id(0), context_stack(1, 0) {
    }

    void print(const Module &m) {
        // Generate a unique ID for this module
        int id = gen_unique_id();

        // Enter new scope for this module
        scope.push(m.name(), id);

        // The implementation currently does not support submodules.
        // TODO: test it out on a benchmark with submodules
        internal_assert(m.submodules().empty()) << "StmtToViz does not yet support submodules.";
        
        // Open div to hold this module
        print_opening_tag("div", "Module");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "module");
        print_text(" name=" + m.name() + ", target=" + m.target().to_string());
        print_closing_tag("span");

        print_toggle_anchor_closing_tag();
        
        // Open code block to hold module body
        print_html_element("span", "matched", " {");

        // Open indented div to hold body code
        print_opening_tag("div", "indent ModuleBody", id);

        // Print module buffers
        for (const auto &buf : m.buffers()) {
            print(buf);
        }

        // Print module functions
        for (const auto &fn : m.functions()) {
            print(fn);
        }

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding module body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this module
        print_closing_tag("div");
        
        // Pop out to outer scope
        scope.pop(m.name());
    }

    void print(const Buffer<> &buf) {
        // Generate a unique ID for this module
        int id = gen_unique_id();

        // Determine whether to print buffer data
        bool print_data = ends_with(buf.name(), "_gpu_source_kernels");

        // Open div to hold this buffer
        print_opening_tag("div", "Buffer");

        if (print_data) {
            // Generate the show hide icon/text buttons
            print_toggle_anchor_opening_tag(id);

            // -- print icon
            print_show_hide_icon(id);

            // -- print text
            print_html_element("span", "keyword", "buffer ");
            print_variable(buf.name());

            print_toggle_anchor_closing_tag();

            // Print data
            print_text(" = ");
            
            // Open code block to hold module body
            print_html_element("span", "matched", " {");
            
            // Open indented div to hold buffer data
            print_opening_tag("div", "indent BufferData", id);
            
            string str((const char *)buf.data(), buf.size_in_bytes());
            if (starts_with(buf.name(), "cuda_")) 
                print_cuda_gpu_source_kernels(str);
            else 
                stream << "<pre>\n" << str << "</pre>\n";
            
            print_closing_tag("div");

            // Close code block holding buffer body
            print_html_element("span", "matched ClosingBrace", " }", "cb-" + std::to_string(id));
        } 
        else {
            // Print buffer name and move on
            print_html_element("span", "keyword", "buffer ");
            print_variable(buf.name());
        }

        // Close div holding this buffer
        print_closing_tag("div");
    }

    void print(const LoweredFunc &fn) {
        // Generate a unique ID for this function
        int id = gen_unique_id();

        // Enter new scope for this function
        scope.push(fn.name, id);

        // Open div to hold this buffer
        print_opening_tag("div", "Function");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text (fn name and args)
        //    Note: We wrap the show/hide buttons in a navigation anchor
        //    that lets us sync text and visualization tabs.
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor", "func ", "lowered-func-" + std::to_string(id));
        print_text(fn.name + "(");
        print_closing_tag("span");
        print_fndecl_args(fn.args);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this function in the viz
        print_visualization_button(id);

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

        // Open indented div to hold body code
        print_opening_tag("div", "indent FunctionBody", id);

        // Print function body
        print(fn.body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding func body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this function
        print_closing_tag("div");

        // Pop out to outer scope
        scope.pop(fn.name);
    }

    void print(const Expr &ir) {
        ir.accept(this);
    }

    void print(const Stmt &ir) {
        ir.accept(this);
    }

private:
    // Handle to output file stream
    std::ofstream &stream;

    // Used to generate unique ids
    int _id;

    // Used to track scope during IR traversal
    Scope<int> scope;

    /* All spans and divs will have an id of the form "x-y", where x
     * is shared among all spans/divs in the same context, and y is unique.
     * These variables are used to track the context within generated HTML */
    std::vector<int> context_stack;
    std::vector<string> context_stack_tags;

    /* Misc methods used for generating common HTML elements*/
    
    // Prints the opening tag for the specified html element. A unique ID is 
    // auto-generated unless provided.
    void print_opening_tag(const string &tag, const string &cls, int id = -1) {
        stream << "<" << tag << " class='" << cls << "' id='";
        if (id == -1) {
            stream << context_stack.back() << "-";
            stream << gen_unique_id();
        } else {
            stream << id;
        }
        stream << "'>";
        context_stack.push_back(gen_unique_id());
        context_stack_tags.push_back(tag);
    }

    void print_opening_tag(const string &tag, const string &cls, string id) {
        stream << "<" << tag << " class='" << cls << "' id='" << id << "'>";
        context_stack.push_back(gen_unique_id());
        context_stack_tags.push_back(tag);
    }

    // Prints the closing tag for the specified html element.
    void print_closing_tag(const string &tag) {
        internal_assert(!context_stack.empty() && tag == context_stack_tags.back()) << tag << " " << context_stack_tags.back();
        context_stack.pop_back();
        context_stack_tags.pop_back();
        stream << "</" + tag + ">";
    }

    // Prints an html element: opening tag, body and closing tag
    void print_html_element(const string &tag, const string &cls, const string &body, int id = -1) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    void print_html_element(const string &tag, const string &cls, const string &body, string id) {
        print_opening_tag(tag, cls, id);
        stream << body;
        print_closing_tag(tag);
    }

    // Prints the opening/closing tags for an anchor that toggles code block view
    void print_toggle_anchor_opening_tag(int id) {
        stream << "<a onclick='return toggle(" << id << ");' href=_blank>";
    }

    void print_toggle_anchor_closing_tag() {
        stream << "</a>";
    }

    // Prints a variable to stream
    void print_variable(const string &x) {
        stream << variable(x);
    }

    string variable(const string &x) {
        int id;
        if (scope.contains(x)) {
            id = scope.get(x);
        } else {
            id = gen_unique_id();
            scope.push(x, id);
        }
        ostringstream s;
        s << "<b class='variable matched' id='" << id << "-" << gen_unique_id() << "'>";
        s << x;
        s << "</b>";
        return s.str();
    }

    // Prints text to stream
    void print_text(const string &x) {
        stream << x;
    }

    // Prints the button to show or hide a code scope
    void print_show_hide_icon(int id) {
        stream << "<div class='show-hide-btn-wrapper'>"
               << "  <div class='show-hide-btn' style='display:none;' id=" << id << "-show>"
               << "    <i class='fa fa-plus-square-o'></i>"
               << "  </div>"
               << "  <div class='show-hide-btn' id=" << id << "-hide>"
               << "    <i class='fa fa-minus-square-o'></i>"
               << "  </div>"
               << "</div>";
    }

    // Prints a button to sync text with visualization
    void print_visualization_button(int id) {
        stream << "<button class='icon-btn sync-btn' onclick='scrollToFunctionCodeToViz(\"" << id << "_viz\")'>"
               << "  <i class='bi bi-arrow-right-square'></i>"
               << "</button>";
    }

    // Maaz: This is a legacy function from the old html generator. I made sure it works
    // with the refactored code but I did not dig into why this function does what it does.
    void print_cuda_gpu_source_kernels(const string &str) {
        print_opening_tag("code", "ptx");

        int current_id = -1;
        bool in_braces = false;
        bool in_func_signature = false;
        
        string current_kernel;
        std::istringstream ss(str);

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
                    current_id = gen_unique_id();
                    print_toggle_anchor_opening_tag(current_id);
                    print_show_hide_icon(current_id);
                    string kernel_name = parts[2].substr(0, parts[2].length() - 1);
                    line = "<span class='keyword'>.visible</span> <span class='keyword'>.entry</span> ";
                    line += variable(kernel_name) + " <span class='matched'>(</span>";
                    current_kernel = kernel_name;
                }
            } else if (starts_with(line, ")") && in_func_signature) {
                print_toggle_anchor_closing_tag();
                in_func_signature = false;
                line = "<span class='matched'>)</span>" + line.substr(1);
            } else if (starts_with(line, "{") && !in_braces) {
                in_braces = true;
                print_toggle_anchor_closing_tag();
                print_html_element("span", "matched", "{");
                internal_assert(current_id != -1);
                print_opening_tag("div", "indent", current_id);
                current_id = -1;
                line = line.substr(1);
                scope.push(current_kernel, gen_unique_id());
            } else if (starts_with(line, "}") && in_braces) {
                print_closing_tag("div");
                line = "<span class='matched'>}</span>" + line.substr(1);
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
                line = "<span class='Pred'>@" + variable(pred) + "</span>" + line.substr(idx);
            }

            // Labels
            if (line.front() == 'L' && !indent && (idx = line.find(':')) != string::npos) {
                string label = line.substr(0, idx);
                line = "<span class='Label'>" + variable(label) + "</span>:" + line.substr(idx + 1);
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
                        operands_str += variable(reg) + '}';
                    } else if (op.front() == '%') {
                        operands_str += variable(op);
                    } else if (op.find_first_not_of("-0123456789") == string::npos) {
                        operands_str += "<span class='IntImm Imm'>";
                        operands_str += op;
                        operands_str += "</span>";
                    } else if (starts_with(op, "0f") &&
                               op.find_first_not_of("0123456789ABCDEF", 2) == string::npos) {
                        operands_str += "<span class='FloatImm Imm'>";
                        operands_str += op;
                        operands_str += "</span>";
                    } else if (op.front() == '[' && op.back() == ']') {
                        size_t idx = op.find('+');
                        if (idx == string::npos) {
                            string reg = op.substr(1, op.size() - 2);
                            operands_str += '[' + variable(reg) + ']';
                        } else {
                            string reg = op.substr(1, idx - 1);
                            string offset = op.substr(idx + 1);
                            offset = offset.substr(0, offset.size() - 1);
                            operands_str += '[' + variable(reg) + "+";
                            operands_str += "<span class='IntImm Imm'>";
                            operands_str += offset;
                            operands_str += "</span>";
                            operands_str += ']';
                        }
                    } else if (op.front() == '{') {
                        string reg = op.substr(1);
                        operands_str += '{' + variable(reg);
                    } else if (op.front() == 'L') {
                        // Labels
                        operands_str += "<span class='Label'>" + variable(op) + "</span>";
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

    // Prints the args in a function declaration
    void print_fndecl_args(std::vector<LoweredArgument> args) {
        bool print_delim = false;
        for (const auto arg : args) {
            if (print_delim) {
                print_html_element("span", "matched", ",");
                print_text(" ");
            }
            print_variable(arg.name);
            print_delim = true;
        }
    }

    /* Misc utility methods */
    int gen_unique_id() {
        return _id++;
    }

private:
    /* All visitor functions inherited from IRVisitor*/

    void visit(const IntImm *op) override {
        print_constant("IntImm Imm", Expr(op));
    }

    void visit(const UIntImm *op) override {
        print_constant("UIntImm Imm", Expr(op));
    }

    void visit(const FloatImm *op) override {
        print_constant("FloatImm Imm", Expr(op));
    }

    void visit(const StringImm *op) override {
        print_constant("StringImm Imm", Expr(op));
    }

    void visit(const Variable *op) override {
        print_variable(op->name);
    }

    void visit(const Cast *op) override {
        print_opening_tag("span", "Cast");
        print_opening_tag("span", "matched");
        print_type(op->type);
        print_text("(");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void visit(const Reinterpret *op) override {
        print_opening_tag("span", "Reinterpret");
        print_opening_tag("span", "matched");
        print_type(op->type);
        print_text("(");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void visit(const Add *op) override {
        print_binary_op(op->a, op->b, "+");
    }

    void visit(const Sub *op) override {
        print_binary_op(op->a, op->b, "-");
    }

    void visit(const Mul *op) override {
        print_binary_op(op->a, op->b, "*");
    }
    
    void visit(const Div *op) override {
        print_binary_op(op->a, op->b, "/");
    }

    void visit(const Mod *op) override {
        print_binary_op(op->a, op->b, "%");
    }

    void visit(const Min *op) override {
        print_opening_tag("span", "Min");
        print_function_call("min", {op->a, op->b});
        print_closing_tag("span");
    }

    void visit(const Max *op) override {
        print_opening_tag("span", "Max");
        print_function_call("max", {op->a, op->b});
        print_closing_tag("span");
    }
    
    void visit(const EQ *op) override {
        print_binary_op(op->a, op->b, "==");
    }
    
    void visit(const NE *op) override {
        print_binary_op(op->a, op->b, "!=");
    }

    void visit(const LT *op) override {
        print_binary_op(op->a, op->b, "&lt;");
    }

    void visit(const LE *op) override {
        print_binary_op(op->a, op->b, "&lt=");
    }

    void visit(const GT *op) override {
        print_binary_op(op->a, op->b, "&gt;");
    }

    void visit(const GE *op) override {
        print_binary_op(op->a, op->b, "&gt;=");
    }
    
    void visit(const And *op) override {
        print_binary_op(op->a, op->b, "&amp;&amp;");
    }

    void visit(const Or *op) override {
        print_binary_op(op->a, op->b, "||");
    }

    void visit(const Not *op) override {
        print_opening_tag("span", "Not");
        print_text("!");
        print(op->a);
        print_closing_tag("span");
    }

    void visit(const Select *op) override {
        print_opening_tag("span", "Select");
        print_function_call("select", {op->condition, op->true_value, op->false_value});
        print_closing_tag("span");
    }

    void visit(const Load *op) override {
        print_opening_tag("span", "Load");
        print_opening_tag("span", "matched");
        print_variable(op->name);
        print_text("[");
        print_closing_tag("span");
        print(op->index);
        print_html_element("span", "matched", "]");
        if (!is_const_one(op->predicate)) {
            print_html_element("span", "keyword", " if ");
            print(op->predicate);
        }
        print_closing_tag("span");
    }

    void visit(const Ramp *op) override {
        print_opening_tag("span", "Ramp");
        print_function_call("ramp", {op->base, op->stride, Expr(op->lanes)});
        print_closing_tag("span");
    }

    void visit(const Broadcast *op) override {
        print_opening_tag("span", "Broadcast");
        print_opening_tag("span", "matched");
        print_text("x" + std::to_string(op->lanes) + "(");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void visit(const Call *op) override {
        print_opening_tag("span", "Call");
        print_function_call(op->name, op->args);
        print_closing_tag("span");
    }

    void visit(const Let *op) override {
        scope.push(op->name, gen_unique_id());
        print_opening_tag("span", "Let");
        print_opening_tag("span", "matched");
        print_text("(");
        print_html_element("span", "keyword", "let ");
        print_variable(op->name);
        print_html_element("span", "Operator Assign", " = ");
        print_closing_tag("span");
        print(op->value);
        print_html_element("span", "matched keyword", " in ");
        print(op->body);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
        scope.pop(op->name);
    }

    void visit(const LetStmt *op) override {
        scope.push(op->name, gen_unique_id());
        print_opening_tag("div", "LetStmt");
        print_opening_tag("p", "WrapLine");
        //stream << open_cost_span(op);
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "let ");
        print_variable(op->name);
        print_html_element("span", "Operator Assign", " = ");
        print_closing_tag("span");
        print(op->value);
        //stream << close_cost_span();
        print_closing_tag("p");        
        print(op->body); 
        print_closing_tag("div");
        scope.pop(op->name);
    }

    void visit(const AssertStmt *op) override {
        print_opening_tag("div", "AssertStmt WrapLine");
        //stream << open_cost_span(op);
        print_function_call("assert", {op->condition, op->message});
        //stream << close_cost_span();
        print_closing_tag("div");
    }

    void visit(const ProducerConsumer *op) override {
        // Give this Producer/Consumer a unique id
        int id = gen_unique_id();

        // Push a new scope
        scope.push(op->name, id);

        // Start a dive to hold code for this Producer/Consumer
        print_opening_tag("div", op->is_producer ? "Produce" : "Consumer");

        // TODO: Assembly support
        // int assembly_line_num = get_assembly_info_viz.get_line_number_prod_cons(op);

        //stream << open_cost_span(op);

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor", op->is_producer ? "produce " : "consume ", 
            "for-loop-" + std::to_string(id));
        print_variable(op->name);
        print_closing_tag("span");

        print_toggle_anchor_closing_tag();

        //stream << close_cost_span();

        // Add a button to jump to this producer/consumer in the viz
        print_visualization_button(id);

        //if (assembly_line_num != -1) {
        //stream << see_assembly_button(assembly_line_num);
        //}
        //stream << see_viz_button(anchor_name);

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

        // Open indented div to hold body code
        print_opening_tag("div", "indent ProducerConsumerBody", id);

        // Print producer/consumer body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding producer/consumer body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this producer/consumer
        print_closing_tag("div");

        // Pop out of loop scope
        scope.pop(op->name);
    }

    void visit(const For *op) override {
        // Give this loop a unique id
        int id = gen_unique_id();

        // Push scope
        scope.push(op->name, id);

        // TODO: Connect with Assembly Code
        // ForLoopLineNumber assembly_line_info = get_assembly_info_viz.get_line_numbers_for_loops(op);
        // int assembly_line_num_start = assembly_line_info.start_line;
        // int assembly_line_num_end = assembly_line_info.end_line;
        
        // Start a dive to hold code for this allocate
        print_opening_tag("div", "For");

        // stream << open_cost_span(op);

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_opening_tag("span", "keyword nav-anchor", "for-loop-" + std::to_string(id));
        stream << op->for_type << op->device_api;
        print_closing_tag("span");
        print_text(" (");
        print_closing_tag("span");
        print_variable(op->name);
        print_html_element("span", "matched", ", ");
        print(op->min);
        print_html_element("span", "matched", ", ");
        print(op->extent);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        //stream << close_cost_span();

        // Add a button to jump to this loop in the viz
        print_visualization_button(id);

        //if (assembly_line_num_start != -1) {
        //  stream << see_assembly_button(assembly_line_num_start, assembly_line_num_end);
        //}

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

        // Open indented div to hold body code
        print_opening_tag("div", "indent ForBody", id);

        // Print loop body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding loop body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this for loop
        print_closing_tag("div");

        // Pop out of loop scope
        scope.pop(op->name);
    }

    void visit(const Acquire *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this acquire
        print_opening_tag("div", "Acquire");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "acquire");
        print_text(" (");
        print_closing_tag("span");
        print(op->semaphore);
        print_html_element("span", "matched", ", ");
        print(op->count);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        //stream << close_cost_span();

        // Add a button to jump to this acquire in the viz
        print_visualization_button(id);

        // Open code block to hold function body
        print_html_element("span", "matched", "{");

        // Open indented div to hold body code
        print_opening_tag("div", "indent AcquireBody", id);

        // Print aquire body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding acquire body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this acquire
        print_closing_tag("div");
    }

    void visit(const Store *op) override {
        // Give this store a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this acquire
        print_opening_tag("div", "Store WrapLine");

        //stream << open_cost_span(op);

        // Print store target
        print_opening_tag("span", "matched");
        print_variable(op->name);
        print_html_element("span", "nav-anchor", "[", "store-" + std::to_string(id));
        print_closing_tag("span");
        print(op->index);
        print_html_element("span", "matched", "]");
        print_html_element("span", "Operator Assign Matched", " = ");

        // Print store value
        print_opening_tag("span", "StoreValue");
        print(op->value);
        if (!is_const_one(op->predicate)) {
            print_html_element("span", "keyword", " if ");
            print(op->predicate);
        }
        print_closing_tag("span");

        // Close div holding this store
        print_closing_tag("div");
    }

    void visit(const Provide *op) override {
        print_opening_tag("div", "Provide WrapLine");
        print_function_call(op->name, op->args);
        if (op->values.size() > 1) {
            print_html_element("span", "matched", " = {");
            for (size_t i = 0; i < op->args.size(); i++) {
                if (i > 0) {
                    print_html_element("span", "matched", ", ");
                }
                print(op->args[i]);
            }
            print_html_element("span", "matched", "}");
        } else {
            print(op->values[0]);
        }
        print_closing_tag("div");
    }

    void visit(const Allocate *op) override {
        // Give this allocation a unique id
        int id = gen_unique_id();

        // Push scope
        scope.push(op->name, gen_unique_id());

        // Start a dive to hold code for this allocate
        print_opening_tag("div", "Allocate");

        //stream << open_cost_span(op);

        //  Print allocation name, type and extents
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor", "allocate ", "allocate-" + std::to_string(id));
        print_variable(op->name);
        print_text("[");
        print_closing_tag("span");
        print_type(op->type);
        for (const auto &extent : op->extents) {
            print_text(" * ");
            print(extent);
        }
        print_html_element("span", "matched", "]");
        
        // Print memory type
        if (op->memory_type != MemoryType::Auto) {
            print_html_element("span", "keyword", " in ");
            stream << op->memory_type;
        }

        // Print allocation condition
        if (!is_const_one(op->condition)) {
            print_html_element("span", "keyword", " if ");
            print(op->condition);
        }

        // Print custom new and free expressions
        if (op->new_expr.defined()) {
            print_opening_tag("span", "matched");
            print_html_element("span", "keyword", "custom_new");
            print_text(" {");
            print_closing_tag("span");
            print(op->new_expr);
            print_html_element("span", "matched", "}");
        }

        if (!op->free_function.empty()) {
            print_opening_tag("span", "matched");
            print_html_element("span", "keyword", "custom_free");
            print_text(" {");
            print_closing_tag("span");
            print_text(" " + op->free_function + "(); ");
            print_html_element("span", "matched", "}");
        }
        
        //stream << close_cost_span();

        // Add a button to jump to this allocation in the viz
        print_visualization_button(id);

        // Print allocation body
        print_opening_tag("div", "AllocateBody");
        print(op->body);
        print_closing_tag("div");

        // Close dive holding the allocate
        print_closing_tag("div");

        // Pop out of allocate scope
        scope.pop(op->name);
    }
    
    void visit(const Free *op) override {
        print_opening_tag("div", "Free WrapLine");
        //stream << open_cost_span(op);
        print_html_element("span", "keyword", "free ");
        print_variable(op->name);
        //stream << close_cost_span();
        print_closing_tag("div");
    }

    void visit(const Realize *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Push scope
        scope.push(op->name, id);

        // Start a dive to hold code for this realize
        print_opening_tag("div", "Realize");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword", "realize");
        print_variable(op->name);
        print_text(" (");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_html_element("span", "matched", "[");
            print(op->bounds[i].min);
            print_html_element("span", "matched", ", ");
            print(op->bounds[i].extent);
            print_html_element("span", "matched", "]");
            if (i < op->bounds.size() - 1) {
                print_html_element("span", "matched", ", ");
            }
        }
        print_html_element("span", "matched", ")");

        // Print predicate
        if (!is_const_one(op->condition)) {
            print_html_element("span", "keyword", " if ");
            print(op->condition);
        }

        print_toggle_anchor_closing_tag();

        //stream << close_cost_span();

        // Add a button to jump to this realize in the viz
        print_visualization_button(id);

        // Open code block to hold function body
        print_html_element("span", "matched", " {");

        // Open indented div to hold body code
        print_opening_tag("div", "indent RealizeBody", id);

        // Print realization body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding realize body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this realize
        print_closing_tag("div");

        // Pop out of this scope
        scope.pop(op->name);
    }

    void visit(const Block *op) override {
        print_opening_tag("div", "Block");
        print_block_stmt(op->first);
        print_block_stmt(op->rest);
        print_closing_tag("div");
    }

    void visit(const Fork *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this realize
        print_opening_tag("div", "Fork");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_html_element("span", "keyword matched", "fork");
        
        print_toggle_anchor_closing_tag();

        // Open code block to hold fork body
        print_html_element("span", "matched", " {");

        // Open indented div to hold body code
        print_opening_tag("div", "indent ForkBody", id);

        // Print fork body
        visit_fork_stmt(op->first);
        visit_fork_stmt(op->rest);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding fork body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this fork
        print_closing_tag("div");
    }

    void visit(const IfThenElse *op) override {
        // Give this acquire a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this conditional
        print_opening_tag("div", "IfThenElse");

        //stream << open_cost_span(op);

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_opening_tag("span", "matched");
        print_html_element("span", "keyword nav-anchor IfSpan", "if", "conditional-" + std::to_string(id));
        print_text(" (");
        print_closing_tag("span");
        print(op->condition);
        print_html_element("span", "matched", ")");

        print_toggle_anchor_closing_tag();

        // Add a button to jump to this conditional in the viz
        print_visualization_button(id);

        // Flatten nested if's in the else case as an 
        // `if-then-else_if-else` sequence
        while (true) {
            /* Handle the `then` case */

            // Open code block to hold `then` case
            print_html_element("span", "matched", " {");

            // stream << close_cost_span();

            // Open indented div to hold code for the `then` case
            print_opening_tag("div", "indent ThenBody", id);

            // Print then case body
            print(op->then_case);

            // Close indented div holding `then` case
            print_closing_tag("div");

            // Close code block holding `then` case
            print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

            // If there is no `else` case, we are done!
            if (!op->else_case.defined())
                break;

            /* Handle the `else` case */

            // If the else-case is another if-then-else, flatten it
            if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
                // Generate a new id for the `else-if` case
                id = gen_unique_id();

                // stream << open_cost_span(nested_if);

                // Generate the show hide icon/text buttons
                print_toggle_anchor_opening_tag(id);

                // -- print icon
                print_show_hide_icon(id);

                // -- print text
                print_opening_tag("span", "matched");
                print_html_element("span", "keyword nav-anchor IfSpan", "else if", "conditional-" + std::to_string(id));
                print_text(" (");
                print_closing_tag("span");
                print(nested_if->condition);
                print_html_element("span", "matched", ")");

                print_toggle_anchor_closing_tag();

                // Add a button to jump to this conditional branch in the viz
                print_visualization_button(id);

                // Update op to the nested if for next loop iteration
                op = nested_if;
            }
            // Otherwise, print it and we are done!
            else {
                int else_id = gen_unique_id();

                // stream << open_cost_span_else_case(op->else_case);

                // Generate the show hide icon/text buttons
                print_toggle_anchor_opening_tag(else_id);

                // -- print icon
                print_show_hide_icon(else_id);

                // -- print text
                print_opening_tag("span", "matched");
                print_html_element("span", "keyword nav-anchor IfSpan", "else", "conditional-" + std::to_string(else_id));
                
                print_toggle_anchor_closing_tag();

                // Add a button to jump to this conditional branch in the viz
                print_visualization_button(else_id);

                // Open code block to hold `else` case
                print_html_element("span", "matched", " {");

                // stream << close_cost_span();

                // Open indented div to hold code for the `then` case
                print_opening_tag("div", "indent ElseBody", else_id);

                // Print `else` case body
                print(op->else_case);

                // Close indented div holding `else` case
                print_closing_tag("div");

                // Close code block holding `else` case
                print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(else_id));

                break;
            }
        }

        // Close div holding the conditional
        print_closing_tag("div");
    }

    void visit(const Evaluate *op) override {
        print_opening_tag("div", "Block");
        //stream << open_cost_span(op);
        print(op->value);
        //stream << close_cost_span();
        print_closing_tag("div");
    }

    void visit(const Shuffle *op) override {
        print_opening_tag("div", "Block");
        if (op->is_concat()) {
            print_function_call("concat_vectors", op->vectors);
        } else if (op->is_interleave()) {
            print_function_call("interleave_vectors", op->vectors);
        } else if (op->is_extract_element()) {
            std::vector<Expr> args = op->vectors;
            args.emplace_back(op->slice_begin());
            print_function_call("extract_element", args);
        } else if (op->is_slice()) {
            std::vector<Expr> args = op->vectors;
            args.emplace_back(op->slice_begin());
            args.emplace_back(op->slice_stride());
            args.emplace_back(static_cast<int>(op->indices.size()));
            print_function_call("slice_vectors", args);
        } else {
            std::vector<Expr> args = op->vectors;
            for (int i : op->indices) {
                args.emplace_back(i);
            }
            print_function_call("shuffle", args);
        }
        print_closing_tag("div");
    }

    void visit(const VectorReduce *op) override {
        print_opening_tag("div", "VectorReduce");
        print_type(op->type);
        print_function_call("vector_reduce", {op->op, op->value});
        print_closing_tag("div");
    }

    void visit(const Prefetch *op) override {
        print_opening_tag("div", "Prefetch");

        // Print prefetch
        print_html_element("span", "matched keyword", "prefetch ");
        print_variable(op->name);
        print_html_element("span", "matched", "(");
        for (size_t i = 0; i < op->bounds.size(); i++) {
            print_html_element("span", "matched", "[");
            print(op->bounds[i].min);
            print_html_element("span", "matched", ",");
            print(op->bounds[i].extent);
            print_html_element("span", "matched", "]");
            if (i < op->bounds.size() - 1) {
                print_html_element("span", "matched", ", ");
            }
        }
        print_html_element("span", "matched", ")");
        
        // Print condition
        if (!is_const_one(op->condition)) {
            print_html_element("span", "keyword", " if ");
            print(op->condition);
        }

        // Print prefetch body
        print_opening_tag("div", "indent PrefetchBody");
        print(op->body);
        print_closing_tag("div");

        print_closing_tag("div");
    }

    void visit(const Atomic *op) override {
        // Give this node a unique id
        int id = gen_unique_id();

        // Start a dive to hold code for this atomic
        print_opening_tag("div", "Atomic");

        // Generate the show hide icon/text buttons
        print_toggle_anchor_opening_tag(id);

        // -- print icon
        print_show_hide_icon(id);

        // -- print text
        print_html_element("span", "matched keyword", "atomic");
        if (!op->mutex_name.empty()) {
            print_html_element("span", "matched", "(");
            print_html_element("span", "Symbol", op->mutex_name);
            print_html_element("span", "matched", ")");
        }
        
        print_toggle_anchor_closing_tag();

         // Open code block to hold atomic body
        print_html_element("span", "matched", " {");

        // Open indented div to hold atomic code
        print_opening_tag("div", "indent AtomicBody", id);

        // Print atomic body
        print(op->body);

        // Close indented div holding body code
        print_closing_tag("div");

        // Close code block holding fork body
        print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

        // Close div holding this atomic
        print_closing_tag("div");
    }

    /* Helper functions for printing IR nodes */
    void print_constant(string cls, Expr c) {
        print_opening_tag("span", cls);
        stream << c;
        print_closing_tag("span");
    }

    void print_type(Type t) {
        print_opening_tag("span", "Type");
        stream << t;
        print_closing_tag("span");
    }

    void print_binary_op(const Expr &a, const Expr &b, string op) {
        print_opening_tag("span", "BinaryOp");
        print_html_element("span", "matched", "(");
        print(a);
        print_text(" ");
        print_html_element("span", "matched Operator", op);
        print_text(" ");
        print(b);
        print_html_element("span", "matched", ")");
        print_closing_tag("span");
    }

    void print_function_call(string fn_name, const std::vector<Expr> &args) {
        print_opening_tag("span", "matched");
        print_html_element("span", "Symbol", fn_name);
        print_text("(");
        print_closing_tag("span");
        bool print_delim = false;
        for (auto arg : args) {
            if (print_delim) {
                print_html_element("span", "matched", ", ");
            }
            print(arg);
            print_delim = true;
        }
        print_html_element("span", "matched", ")");
    }

    // To avoid generating ridiculously deep DOMs, we flatten blocks here.
    void print_block_stmt(const Stmt &stmt) {
        if (const Block *b = stmt.as<Block>()) {
            print_block_stmt(b->first);
            print_block_stmt(b->rest);
        } else if (stmt.defined()) {
            print(stmt);
        }
    }

    // We also flatten forks
    void visit_fork_stmt(const Stmt &stmt) {
        if (const Fork *f = stmt.as<Fork>()) {
            visit_fork_stmt(f->first);
            visit_fork_stmt(f->rest);
        } else if (stmt.defined()) {
            // Give task a unique id
            int id = gen_unique_id();

            // Start a dive to hold code for this task
            print_opening_tag("div", "ForkTask");

            // Generate the show hide icon/text buttons
            print_toggle_anchor_opening_tag(id);

            // -- print icon
            print_show_hide_icon(id);

            // -- print text
            print_html_element("span", "keyword matched", "task");

            print_toggle_anchor_closing_tag();

            // Open code block to hold task body
            print_html_element("span", "matched", " {");

            // Open indented div to hold body code
            print_opening_tag("div", "indent ForkTask", id);

            // Print task body
            print(stmt);

            // Close indented div holding body code
            print_closing_tag("div");

            // Close code block holding task body
            print_html_element("span", "matched ClosingBrace", "}", "cb-" + std::to_string(id));

            // Close div holding this fork task
            print_closing_tag("div");
        }
    }
};


/************** HTMLVisualizationPrinter Class **************/

class HTMLVisualizationPrinter {
public:
    HTMLVisualizationPrinter(std::ofstream &os)
        : stream(os) {
    }

private:
    // Handle to output file stream
    std::ofstream &stream;
};

/************** IRVisualizer Class **************/

// We use this macro to get the absolute path to resource files
// Can use std::source_location once C++-17 is deprecated
#define __DIR__ \
    std::string_view(__FILE__).substr(0, std::string_view(__FILE__).rfind('/') + 1)

class IRVisualizer {
public:
    // Construct the visualizer and point it to the output file
    IRVisualizer(const string &filename)
        : _popup_id(0), html_code_printer(stream), html_viz_printer(stream) {
        stream.open(filename.c_str());
    }

    // Generate the html visualization of the input module
    void generate_html(const Module &m) {
        stream << "<html>\n";
        generate_head(m);
        generate_body(m);
        stream << "</html>";
    }

private:
    // Handle to output file stream
    std::ofstream stream;

    // Used to generate unique popup ids
    int _popup_id;

    // Used to translate IR to code in HTML
    HTMLCodePrinter html_code_printer;

    // Used to translate IR to visualization in HTML
    HTMLVisualizationPrinter html_viz_printer;

    /* Methods for generating the <head> section of the html file */
    void generate_head(const Module &m) {
        stream << "<head>\n";
        stream << "<title>Visualizing Module: " << m.name() << "</title>\n";
        generate_dependency_links();
        generate_stylesheet();
        stream << "</head>\n";
    }

    // Loads the html code responsible for linking with various js/css libraries from
    // `ir_visualizer/dependencies.html`
    void generate_dependency_links() {
        std::ifstream deps_file(string(__DIR__) + "ir_visualizer/dependencies.html");
        internal_assert(!deps_file.fail()) << "Failed to find `dependencies.html` inside "
                                           << string(__DIR__)
                                           << "ir_visualizer directory.\n ";
        stream << deps_file.rdbuf();
    }

    // Loads the stylesheet code from `ir_visualizer/stylesheet.html`
    void generate_stylesheet() {
        std::ifstream css_file(string(__DIR__) + "ir_visualizer/stylesheet.html");
        internal_assert(!css_file.fail()) << "Failed to find `stylesheet.html` inside "
                                          << string(__DIR__) 
                                          << "ir_visualizer directory.\n ";
        stream << css_file.rdbuf();
    }

    /* Methods for generating the <body> section of the html file */
    void generate_body(const Module &m) {
        stream << "<body>\n";
        stream << "  <div id='page-container'>\n";
        generate_information_bar(m);
        generate_visualization_tabs(m);
        stream << "  </div>\n";
        stream << "</body>";
        
        

        

        // put assembly code in an invisible div
        //stream << get_assembly_info_viz.get_assembly_html();

        // closing parts of the html
        //stream << "<div class='popups'>\n";
        //stream << popups;
        //stream << "</div>\n";

        // Include javascript template.
        generate_javascript();

        
    }

    // Generate the information bar
    void generate_information_bar(const Module &m) {
        //popups += information_popup();

        // We can use std::format once migrated to C++-20
        stream << R"(<div id='info-bar'>
                       <div id='info-bar-title'>
                         <h3>Visualizing Module: )" << m.name() << R"(</h3>
                       </div>
                       <div id='info-bar-spacing'></div>
                       <div id='info-bar-button'>
                         <h3>
                           <button id='show-info'>
                             <i class='bi bi-info-square' data-bs-toggle='modal' 
                                data-bs-target='#info-popup)" << gen_popup_id() << R"('></i>
                           </button>
                         </h3>
                       </div>
                     </div>)";
    }

    // Generate the three visualization tabs
    void generate_visualization_tabs(const Module &m) {
        stream << "<div id='visualization-tabs'>\n";
        generate_ir_tab(m);
        generate_resize_bar_1();
        generate_visualization_tab();
        generate_resize_bar_2();
        generate_assembly_tab();
        stream << "</div>\n";

        // Generatize the resizing bar for the IR Code and Visualization tabs
        //stream << resize_bar();

        // Generate the IR Visualization tab
        //stream << "<div class='IRVisualization' id='IRVisualization'>\n";
        //stream << generate_ir_visualization(m);
        //stream << "</div>\n";

        // Generatize the resizing bar for the Visualization and Assembly Code tabs
        //stream << resize_bar_assembly();

        // Generate the IR Code tab
        //stream << "<div id='assemblyCode'>\n";
        //stream << "</div>\n";
    }

    // Generate tab 1/3: Lowered IR code with syntax highlighting in HTML
    void generate_ir_tab(const Module &m) {
        stream << "<div id='ir-code-tab'>\n";
        html_code_printer.print(m);
        stream << "</div>\n";
    }

    // Generate tab 2/3: Lowered IR code with syntax highlighting in HTML
    void generate_visualization_tab() {
        stream << "<div id='ir-visualization-tab'>\n";
        //stream << generate_ir_visualization(m);
        stream << "</div>\n";
    }

    // Generate tab 3/3: Generated assembly code
    void generate_assembly_tab() {
        stream << "<div id='assembly-tab'>\n";
        stream << "</div>\n";
    }

    // Generate a resizing bar to control the width of code and visualization tabs
    void generate_resize_bar_1() {
        stream << R"(<div class='resize-bar' id='resize-bar-1'>
                       <div class='collapse-btns'>
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapse_code_tab()'>
                             <i class='bi bi-arrow-bar-left'></i>
                           </button>
                         </div>                         
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapseR_visualization_tab()'>
                             <i class='bi bi-arrow-bar-right'></i>
                           </button>
                         </div>
                       </div>
                     </div>)";
    }

    // Generate a resizing bar to control the width of visualization and assembly tabs
    void generate_resize_bar_2() {
        stream << R"(<div class='resize-bar' id='resize-bar-2'>
                       <div class='collapse-btns'>
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapseL_visualization_tab()'>
                             <i class='bi bi-arrow-bar-left'></i>
                           </button>
                         </div>
                         <div>
                           <button class='icon-btn resize-btn' onclick='collapse_assembly_tab()'>
                             <i class='bi bi-arrow-bar-right'></i>
                           </button>
                         </div>
                       </div>
                     </div>)";
    }

    // Loads and initializes the javascript template from `ir_visualizer / javascript_template.html`
    void generate_javascript() {
        // 1. Load the javascript code template
        std::ifstream js_template_file(string(__DIR__) + "ir_visualizer/javascript_template.html");
        internal_assert(!js_template_file.fail()) << "Failed to find `javascript_template.html` inside "
                                                  << string(__DIR__)
                                                  << "ir_visualizer directory.\n ";
        ostringstream js_template;
        js_template << js_template_file.rdbuf();
        js_template_file.close();

        // 2. Initialize template with concrete values for `tooltip_count`,
        // `stmt_hierarchy_tooltip_count` and `ir_viz_tooltip_count`.
        string js = js_template.str();
        std::regex r1("\\{\\{tooltip_count\\}\\}");
        //js = std::regex_replace(js, r1, std::to_string(tooltip_count));
        js = std::regex_replace(js, r1, "0");
        std::regex r2("\\{\\{stmt_hierarchy_tooltip_count\\}\\}");
        //js = std::regex_replace(js, r2, std::to_string(get_stmt_hierarchy.get_tooltip_count()));
        js = std::regex_replace(js, r2, "0");
        std::regex r3("\\{\\{ir_viz_tooltip_count\\}\\}");
        //js = std::regex_replace(js, r3, std::to_string(ir_visualization.get_tooltip_count()));
        js = std::regex_replace(js, r3, "0");

        // 3. Write initialized template to stream
        stream << js;
    }

    /* Misc helper methods */

    // Returns a new unique popup id
    int gen_popup_id() {
        return _popup_id++;
    }
};

/************** The external interface to this module **************/

void print_to_viz(const string &filename, const Stmt &s) {
    internal_assert(false) << "\n\n"
                           << "Exiting early: print_to_viz cannot be called from a Stmt node - it must be "
                              "called from a Module node.\n"
                           << "\n\n\n";
}

void print_to_viz(const string &filename, const Module &m) {

    IRVisualizer visualizer(filename);
    visualizer.generate_html(m);
    debug(1) << "Done generating HTML IR Visualization - printed to: " << filename << "\n";
}

/************** Legacy StmtToViz Class Implementation **************/

/* 
// Commenting out but keeping legacy code around until refactor is complete 

const char *StmtToViz_canIgnoreVariableName_string = "canIgnoreVariableName";

class StmtToViz : public IRVisitor {
    // This allows easier access to individual elements.
    int id_count;

private:
    std::ofstream stream;

    FindStmtCost find_stmt_cost;               // used for finding the cost of statements
    GetStmtHierarchy get_stmt_hierarchy;       // used for getting the hierarchy of
                                               // statements
    IRVisualization ir_visualization;          // used for getting the IR visualization
    GetAssemblyInfoViz get_assembly_info_viz;  // used for getting the assembly line numbers

    int curr_line_num;  // for accessing div of that line

    // used for getting anchor names
    int if_count;
    int producer_consumer_count;
    int for_count;
    int store_count;
    int allocate_count;
    int functionCount;

    // used for tooltip stuff
    int tooltip_count;

    // used for get_stmt_hierarchy popup stuff
    int popup_count;
    string popups;

    int unique_id() {
        return ++id_count;
    }

    // All spans and divs will have an id of the form "x-y", where x
    // is shared among all spans/divs in the same context, and y is unique.
    std::vector<int> context_stack;
    std::vector<string> context_stack_tags;
    string open_tag(const string &tag, const string &cls, int id = -1) {
        ostringstream s;
        s << "<" << tag << " class='" << cls << "' id='";
        if (id == -1) {
            s << context_stack.back() << "-";
            s << unique_id();
        } else {
            s << id;
        }
        s << "'>";
        context_stack.push_back(unique_id());
        context_stack_tags.push_back(tag);
        return s.str();
    }
    string tag(const string &tag, const string &cls, const string &body, int id = -1) {
        ostringstream s;
        s << open_tag(tag, cls, id);
        s << body;
        s << close_tag(tag);
        return s.str();
    }
    string close_tag(const string &tag) {
        internal_assert(!context_stack.empty() && tag == context_stack_tags.back());
        context_stack.pop_back();
        context_stack_tags.pop_back();
        return "</" + tag + ">";
    }

    StmtHierarchyInfo get_stmt_hierarchy_html(const Stmt &op) {
        StmtHierarchyInfo stmt_hierarchy_info = get_stmt_hierarchy.get_hierarchy_html(op);
        string &html = stmt_hierarchy_info.html;
        string popup = generate_stmt_hierarchy_popup(html);
        stmt_hierarchy_info.html = popup;

        return stmt_hierarchy_info;
    }
    StmtHierarchyInfo get_stmt_hierarchy_html(const Expr &op) {
        StmtHierarchyInfo stmt_hierarchy_info = get_stmt_hierarchy.get_hierarchy_html(op);
        string &html = stmt_hierarchy_info.html;
        string popup = generate_stmt_hierarchy_popup(html);
        stmt_hierarchy_info.html = popup;

        return stmt_hierarchy_info;
    }

    string generate_stmt_hierarchy_popup(const string &hierarchy_HTML) {
        ostringstream popup;

        popup_count++;
        popup << "<div class='modal fade' id='stmtHierarchyModal" << popup_count;
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
        popup << hierarchy_HTML;
        popup << "            </div>\n";
        popup << "        </div>\n";
        popup << "    </div>\n";
        popup << "</div>\n";

        return popup.str();
    }

    string open_cost_span(const Stmt &stmt_op) {
        StmtHierarchyInfo stmt_hierarchy_info = get_stmt_hierarchy_html(stmt_op);

        ostringstream s;

        s << cost_colors(stmt_op.get(), stmt_hierarchy_info);

        // popup window - will put them all at the end
        popups += stmt_hierarchy_info.html + "\n";

        s << "<span id='Cost" << id_count << "'>";
        return s.str();
    }
    string open_cost_span(const Expr &stmt_op) {
        StmtHierarchyInfo stmt_hierarchy_info = get_stmt_hierarchy_html(stmt_op);

        ostringstream s;

        s << cost_colors(stmt_op.get(), stmt_hierarchy_info);

        // popup window - will put them all at the end
        popups += stmt_hierarchy_info.html + "\n";

        s << "<span id='Cost" << id_count << "'>";
        return s.str();
    }

    string close_cost_span() {
        return "</span>";
    }
    string open_cost_span_else_case(Stmt else_case) {
        Stmt new_node =
            IfThenElse::make(Variable::make(Int(32), StmtToViz_canIgnoreVariableName_string), std::move(else_case), nullptr);

        StmtHierarchyInfo stmt_hierarchy_info = get_stmt_hierarchy.get_else_hierarchy_html();
        string popup = generate_stmt_hierarchy_popup(stmt_hierarchy_info.html);

        // popup window - will put them all at the end
        popups += popup + "\n";

        ostringstream s;

        curr_line_num += 1;

        s << "<span class='smallColorIndent'>";

        s << computation_button(new_node.get(), stmt_hierarchy_info);
        s << data_movement_button(new_node.get(), stmt_hierarchy_info);

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
                        const StmtHierarchyInfo &stmt_hierarchy_info) {

        int color_range_inclusive, color_range_exclusive;

        if (is_computation) {
            color_range_inclusive = ir_visualization.get_combined_color_range(op, true);
            color_range_exclusive = ir_visualization.get_color_range(op, StmtCostModel::Compute);
        } else {
            color_range_inclusive = ir_visualization.get_combined_color_range(op, false);
            color_range_exclusive = ir_visualization.get_color_range(op, StmtCostModel::DataMovement);
        }
        tooltip_count++;

        ostringstream s;
        s << "<button ";

        // tooltip information
        s << "id='button" << tooltip_count << "' ";
        s << "aria-describedby='tooltip" << tooltip_count << "' ";

        // cost colors
        s << "class='colorButton CostColor" + std::to_string(color_range_exclusive) + "' role='button' ";

        // showing StmtHierarchy popup
        s << "data-bs-toggle='modal' data-bs-target='#stmtHierarchyModal" << popup_count << "' ";

        // for collapsing and expanding StmtHierarchy nodes
        s << "onclick='collapseAllNodes(" << stmt_hierarchy_info.start_node << ", "
          << stmt_hierarchy_info.end_node << "); expandNodesUpToDepth(4, "
          << stmt_hierarchy_info.viz_num << ");'";

        // highlighting selected line in grey
        s << "onmouseover='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"rgba(10,10,10,0.1)\";'";
        s << "onmouseout='document.getElementById(\"Cost" << id_count
          << "\").style.background = \"transparent\";'";

        // for collapsing and expanding and adjusting colors accordingly
        s << "inclusiverange='" << color_range_inclusive << "' ";
        s << "exclusiverange='" << color_range_exclusive << "' ";

        s << ">";
        s << "</button>";

        return s.str();
    }

    string computation_button(const IRNode *op, const StmtHierarchyInfo &stmt_hierarchy_info) {
        ostringstream s;
        s << color_button(op, true, stmt_hierarchy_info);

        string tooltip_text =
            ir_visualization.generate_computation_cost_tooltip(op, "[Click to see full hierarchy]");

        // tooltip span
        s << "<span id='tooltip" << tooltip_count << "' class='tooltip CostTooltip' ";
        s << "role='tooltip" << tooltip_count << "'>";
        s << tooltip_text;
        s << "</span>";

        return s.str();
    }
    string data_movement_button(const IRNode *op, const StmtHierarchyInfo &stmt_hierarchy_info) {
        ostringstream s;
        s << color_button(op, false, stmt_hierarchy_info);

        string tooltip_text = ir_visualization.generate_data_movement_cost_tooltip(
            op, "[Click to see full hierarchy]");

        // tooltip span
        s << "<span id='tooltip" << tooltip_count << "' class='tooltip CostTooltip' ";
        s << "role='tooltip" << tooltip_count << "'>";
        s << tooltip_text;
        s << "</span>";

        return s.str();
    }
    string cost_colors(const IRNode *op, const StmtHierarchyInfo &stmt_hierarchy_info) {
        curr_line_num += 1;

        ostringstream s;

        if (op->node_type == IRNodeType::Allocate || op->node_type == IRNodeType::Evaluate ||
            op->node_type == IRNodeType::IfThenElse || op->node_type == IRNodeType::For ||
            op->node_type == IRNodeType::ProducerConsumer) {
            s << "<span class='smallColorIndent'>";
        } else {
            s << "<span class='bigColorIndent'>";
        }

        s << computation_button(op, stmt_hierarchy_info);
        s << data_movement_button(op, stmt_hierarchy_info);

        s << "</span>";

        return s.str();
    }

    string open_div(const string &cls, int id = -1) {
        return open_tag("div", cls, id) + "\n";
    }
    string close_div() {
        return close_tag("div") + "\n";
    }

    string open_anchor(const string &anchor_name) {
        return "<span class='navigationAnchor' id='" + anchor_name + "'>";
    }
    string close_anchor() {
        return "</span>";
    }

    string see_viz_button(const string &anchor_name) {
        ostringstream s;

        s << "<button class='iconButton dottedIconButton' ";
        s << "style='padding: 0px;' ";
        s << "onclick='scrollToFunctionCodeToViz(\"" + anchor_name + "_viz\")'>";
        s << "<i class='bi bi-arrow-right-square'></i>";
        s << "</button>";

        return s.str();
    }

    string see_assembly_button(const int &assembly_line_num_start,
                               const int &assembly_line_num_end = -1) {
        ostringstream s;

        // Generates the "code-square" icon from Boostrap:
        // https://icons.getbootstrap.com/icons/code-square/
        tooltip_count++;
        s << "<button class='iconButton assemblyIcon' ";
        s << "id='button" << tooltip_count << "' ";
        s << "aria-describedby='tooltip" << tooltip_count << "' ";
        s << "onclick='populateCodeMirror(" << assembly_line_num_start << ", "
          << assembly_line_num_end << ");'>";
        s << "<i class='bi bi-code-square'></i>";
        s << "</button>";

        // tooltip span
        s << "<span id='tooltip" << tooltip_count << "' class='tooltip' ";
        s << "role='tooltip" << tooltip_count << "'>";
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

        ostringstream s;
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
        ostringstream button;
        button << "<a class=ExpandButton onclick='return toggle(" << id << ", " << tooltip_count
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
        for (auto c : op->value) {
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
                    const char *hex_digits = "0123456789ABCDEF";
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
        producer_consumer_count++;
        string anchor_name = "producerConsumer" + std::to_string(producer_consumer_count);

        // for assembly
        int assembly_line_num = get_assembly_info_viz.get_line_number_prod_cons(op);

        int produce_id = unique_id();

        stream << open_cost_span(op);
        stream << open_span("Matched");
        stream << open_expand_button(produce_id);
        stream << open_anchor(anchor_name);
        stream << keyword(op->is_producer ? "produce" : "consume") << " ";
        stream << var(op->name);
        stream << close_expand_button() << " {";
        stream << close_span();
        stream << close_anchor();
        stream << close_cost_span();
        if (assembly_line_num != -1) {
            stream << see_assembly_button(assembly_line_num);
        }
        stream << see_viz_button(anchor_name);

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
        for_count++;
        string anchor_name = "for" + std::to_string(for_count);

        // for assembly
        ForLoopLineNumber assembly_line_info = get_assembly_info_viz.get_line_numbers_for_loops(op);
        int assembly_line_num_start = assembly_line_info.start_line;
        int assembly_line_num_end = assembly_line_info.end_line;

        int id = unique_id();
        stream << open_cost_span(op);
        stream << open_expand_button(id);
        stream << open_anchor(anchor_name);
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
            internal_assert(false) << "\n"
                                   << "Unknown for type: " << ((int)op->for_type) << "\n\n";
        }
        stream << " (";
        stream << close_span();

        print_list({Variable::make(Int(32), op->name), op->min, op->extent});

        stream << matched(")");
        stream << close_expand_button();
        stream << " " << matched("{");
        stream << close_anchor();
        stream << close_cost_span();
        if (assembly_line_num_start != -1) {
            stream << see_assembly_button(assembly_line_num_start, assembly_line_num_end);
        }
        stream << see_viz_button(anchor_name);

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
        store_count++;
        string anchor_name = "store" + std::to_string(store_count);

        stream << open_cost_span(op);
        stream << open_anchor(anchor_name);

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
        stream << see_viz_button(anchor_name);
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
        allocate_count++;
        string anchor_name = "allocate" + std::to_string(allocate_count);
        stream << open_anchor(anchor_name);

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
        stream << see_viz_button(anchor_name);

        stream << open_div("AllocateBody");
        print(op->body);
        stream << close_div();
        stream << close_div();
        scope.pop(op->name);
    }
    void visit(const Free *op) override {
        stream << open_div("Free WrapLine");
        stream << open_cost_span(op);
        stream << keyword("free") << " ";
        stream << var(op->name);
        stream << close_cost_span();
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
        if_count++;
        string anchor_name = "if" + std::to_string(if_count);

        int id = unique_id();
        stream << open_cost_span(op);
        stream << open_expand_button(id);
        stream << open_anchor(anchor_name);
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
            stream << matched("{");
            stream << close_anchor();
            stream << close_cost_span();
            stream << see_viz_button(anchor_name);

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
                if_count++;
                string anchor_name = "if" + std::to_string(if_count);
                stream << open_anchor(anchor_name);

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
                if_count++;
                string anchor_name = "if" + std::to_string(if_count);
                stream << open_anchor(anchor_name);

                stream << keyword("else ");
                stream << close_expand_button() << "{";
                stream << close_anchor();
                stream << close_cost_span();
                stream << see_viz_button(anchor_name);

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
        find_stmt_cost.generate_costs(m);
        return find_stmt_cost;
    }

    string generate_ir_visualization(const Module &m) {
        return ir_visualization.generate_ir_visualization_html(m);
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

        // anchoring
        functionCount++;
        string anchor_name = "loweredFunc" + std::to_string(functionCount);

        int id = unique_id();
        stream << open_expand_button(id);
        stream << open_anchor(anchor_name);
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
        stream << see_viz_button(anchor_name);

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
            internal_assert(false) << "\n\n\nvoid print(const Buffer<> &op): look at this line!!! make "
                                      "sure the closing brace is correct! \n\n\n";
            stream << open_div("ClosingBrace");
            stream << matched("}");
            stream << close_div();
        }
        stream << close_div();
    }

    void print(const Module &m) {
        scope.push(m.name(), unique_id());

        // doesn't currently support submodules - could comment out error, no guarantee it'll work
        // as expected
        for (const auto &s : m.submodules()) {
            internal_assert(false) << "\n\nStmtToViz does not support submodules yet\n\n";
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

    string information_popup() {

        ostringstream popup;

        popup_count++;
        popup << "<div class='modal fade' id='info-popup" << popup_count
              << "' tabindex='-1' aria-labelledby='stmtHierarchyModalLabel' aria-hidden='true'>\n";
        popup << "<div class='modal-dialog modal-dialog-scrollable modal-xl'>\n";
        popup << "<div class='modal-content'>\n";
        popup << "<div class='modal-header'>\n";
        popup << "<h5 class='modal-title' id='stmtHierarchyModalLabel'>How to read this document "
                 "</h5>\n";
        popup << "<button type='button' class='btn-close' data-bs-dismiss='modal' "
                 "aria-label='Close'></button>\n";
        popup << "</div>\n";
        popup << "<div class='modal-body'>\n";
        popup
            << "<p style='font-size: 20px;'><b style='font-weight: bold;'>Three Columns</b> </p>\n";
        popup << "<p>There are 3 columns on this page:</p>\n";
        popup << "<ul>\n";
        popup << "<li><b style='font-weight: bold;'>Left side:</b> Halide Intermediate "
                 "Representation (IR) - the code that Halide generates.</li>\n";
        popup << "<li><b style='font-weight: bold;'>Middle:</b> Visualization - represents, at a "
                 "high level, the structure of the IR.</li>\n";
        popup << "<li><b style='font-weight: bold;'>Right side:</b> Assembly - the code that the "
                 "compiler generates.</li>\n";
        popup << "</ul>\n";
        popup << "<p>You can adjust the size of the columns using the 2 green resize bars in "
                 "between first two and second two columns. The buttons in the middle of this bar "
                 "will also expand either left or right column completely.</p>\n";
        popup << "<p style='font-size: 20px;'><b style='font-weight: bold;'>Left Column "
                 "Functionality</b> </p>\n";
        popup << "<p>Here are the different features of the left column: </p>\n";
        popup << "<ul>\n";

        tooltip_count++;
        popup << "<span id='tooltip" << tooltip_count
              << "' class='tooltip CostTooltip' role='tooltip" << tooltip_count
              << "'>Costs will be shown here. Click to see statement hierarchy.</span>\n";
        popup << "<li><button id='button" << tooltip_count
              << "' style='height: 20px; width: 10px; padding-left: 0px;' aria-describedby='tooltip"
              << tooltip_count
              << "' class='colorButton CostColor0' role='button' inclusiverange='0' "
                 "exclusiverange='0'></button><b\n";
        popup << "style='font-weight: bold;'>Cost Colors:</b>\n";
        popup << "Next to every line, there are 2 buttons that are colored based on the cost of "
                 "the line. Hovering over them will give more information about the cost of that "
                 "line. If you click on the button, a hierarchy of that line will appear, which "
                 "you can explore to see the contents of the line. There are 2 buttons because "
                 "they each represent a different type of color:\n";
        popup << "<ul>\n";
        popup << "<li><b style='font-weight: bold;'>Computation Cost:</b> This is the cost "
                 "associated with how much computation went into that line of code.</li>\n";
        popup << "<li><b style='font-weight: bold;'>Data Movement Cost:</b> This is the cost "
                 "associated with how much data was moved around in that line of code "
                 "(read/written).</li>\n";
        popup << "</ul>\n";
        popup << "</li>\n";
        popup << "<br>\n";
        popup << "<li>\n";
        popup << "<button class='iconButton dottedIconButton' style='padding: 0px; margin: 0px; "
                 "margin-right: 5px;'><i class='bi bi-arrow-right-short'></i></button><b \n";
        popup << "style='font-weight: bold;'>See Visualization:</b> \n";
        popup << "If you click this button, the right column will scroll to the related block of "
                 "that line of code.\n";
        popup << "</li>\n";
        popup << "<li>\n";
        popup << "<button class='iconButton assemblyIcon' style='padding-left: 0px; margin-left: "
                 "0px;'><svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' "
                 "fill='currentColor' class='bi bi-filetype-raw' viewBox='0 0 16 16'> <path "
                 "fill-rule='evenodd'\n";
        popup << "d='M14 4.5V14a2 2 0 0 1-2 2v-1a1 1 0 0 0 1-1V4.5h-2A1.5 1.5 0 0 1 9.5 3V1H4a1 1 "
                 "0 0 0-1 1v9H2V2a2 2 0 0 1 2-2h5.5L14 4.5ZM1.597 11.85H0v3.999h.782v-1.491h.71l.7 "
                 "1.491h1.651l.313-1.028h1.336l.314 1.028h.84L5.31 11.85h-.925l-1.329 "
                 "3.96-.783-1.572A1.18 1.18 0 0 0 3 13.116c0-.256-.056-.479-.167-.668a1.098 1.098 "
                 "0 0 0-.478-.44 1.669 1.669 0 0 0-.758-.158Zm-.815 1.913v-1.292h.7a.74.74 0 0 1 "
                 ".507.17c.13.113.194.276.194.49 0 "
                 ".21-.065.368-.194.474-.127.105-.3.158-.518.158H.782Zm4.063-1.148.489 "
                 "1.617H4.32l.49-1.617h.035Zm4.006.445-.74 2.789h-.73L6.326 11.85h.855l.601 "
                 "2.903h.038l.706-2.903h.683l.706 2.903h.04l.596-2.903h.858l-1.055 "
                 "3.999h-.73l-.74-2.789H8.85Z' />\n";
        popup << "</svg></button><b style='font-weight: bold;'>See Assembly:</b> If you click this "
                 "button, a new tab will open with the assembly scrolled to the related assembly "
                 "instruction of that line of code.\n";
        popup << "</li>\n";
        popup << "</ul>\n";
        popup << "<p style='font-size: 20px;'><b style='font-weight: bold;'>Middle Column "
                 "Functionality</b></p>\n";
        popup << "<p>Here are the different features of the middle column: </p>\n";
        popup << "<ul>\n";

        tooltip_count++;
        popup << "<span id='tooltip" << tooltip_count << "' class='tooltip' role='tooltip"
              << tooltip_count << "'>Costs will be shown here.</span>\n";
        popup << "<li><button id='button" << tooltip_count
              << "' style='height: 20px; width: 10px; padding-left: 0px;' aria-describedby='tooltip"
              << tooltip_count
              << "' class='colorButton CostColor0' role='button' inclusiverange='0' "
                 "exclusiverange='0'></button><b\n";
        popup << "style='font-weight: bold;'>Cost Colors:</b> Cost colors on the right work the "
                 "same way as they do on the left - hovering over them will give information about "
                 "the cost.</li>\n";
        popup << "<li> <button class='iconButton dottedIconButton' style='padding: 0px; margin: "
                 "0px; margin-right: 5px;'><i class='bi bi-arrow-left-short'></i></button><b "
                 "style='font-weight: bold;'>See Code:</b>\n";
        popup << "If you click this button, the left column will scroll to the related line of "
                 "code of that block in the visualization. </li>\n";

        tooltip_count++;
        popup << "<li> <span id='tooltip" << tooltip_count << "' class='tooltip' role='tooltip"
              << tooltip_count << "'>More information about the node will appear here.</span>\n";
        popup << "<button class='infoButton' id='button" << tooltip_count
              << "' style='padding: 0; margin: 0; margin-right: 5px;' aria-describedby='tooltip"
              << tooltip_count << "'><i class='bi bi-info'></i></button><b\n";
        popup << "style='font-weight: bold;'>Info Button:</b>\n";
        popup << "If you hover over these buttons, they will offer more information about that "
                 "block (eg. load/store size, allocation type, etc.) </li>\n";
        popup << "</ul>\n";
        popup << "<p style='font-size: 20px;'><b style='font-weight: bold;'>Right Column "
                 "Functionality</b> </p>\n";
        popup << "<p>Here are the different features of the right column: </p>\n";
        popup << "<ul>\n";
        popup << "<li> <b style='font-weight: bold;'>Search:</b> You can search the Assembly, but "
                 "you have to do it using CodeMirror specific key bindings: <ul>\n";
        popup << "<li><i>Start Searching:</i> Ctrl-F / Cmd-F </li>\n";
        popup << "<li><i>Find Next:</i> Ctrl-G / Cmd-G</li>\n";
        popup << "</ul>\n";
        popup << "</li>\n";
        popup << "</ul>\n";
        popup << "</div>\n";
        popup << "</div>\n";
        popup << "</div>\n";
        popup << "</div>\n";
        popup << "\n";

        return popup.str();
    }

    string information_bar(const Module &m) {
        popups += information_popup();

        ostringstream info_bar_ss;

        info_bar_ss << "<div class='informationBar'>\n"
                    << "<div class='title'>\n"
                    << "<h3>" << m.name() << "</h3>\n"
                    << "</div>\n"
                    << "<div class='spacing' style='flex-grow: 1;'></div>\n"
                    << "<div class='button'>\n"
                    << "<h3><button class='informationBarButton'><i\n"
                    << "class='bi bi-info-square' data-bs-toggle='modal'\n"
                    << "data-bs-target='#stmtHierarchyModal" << popup_count << "'></i></button>\n"
                    << "</h3>\n"
                    << "</div>\n"
                    << "</div>\n";

        return info_bar_ss.str();
    }

    string resize_bar() {
        ostringstream resize_bar_ss;

        resize_bar_ss << "<div class='ResizeBar' id='ResizeBar'>\n"
                      << "<div class='collapseButtons'>\n"
                      << "<div>\n"
                      << "<button class='iconButton resizeButton' onclick='collapseViz()'>"
                      << "<i class='bi bi-arrow-bar-right'></i></button>"
                      << "</div>\n"
                      << "<div>\n"
                      << "<button class='iconButton resizeButton' onclick='collapseCode()'>"
                      << "<i class='bi bi-arrow-bar-left'></i></button>"
                      << "</div>\n"
                      << "</div>\n"
                      << "</div>\n";

        return resize_bar_ss.str();
    }

    string resize_bar_assembly() {
        ostringstream resize_bar_ss;

        resize_bar_ss << "<div class='ResizeBar' id='ResizeBarAssembly'>\n"
                      << "<div class='collapseButtons'>\n"
                      << "<div>\n"
                      << "<button class='iconButton resizeButton' onclick='collapseAssembly()'>"
                      << "<i class='bi bi-arrow-bar-right'></i></button>"
                      << "</div>\n"
                      << "<div>\n"
                      << "<button class='iconButton resizeButton' onclick='collapseVizAssembly()'>"
                      << "<i class='bi bi-arrow-bar-left'></i></button>"
                      << "</div>\n"
                      << "</div>\n"
                      << "</div>\n";

        return resize_bar_ss.str();
    }

    // Loads the html code responsible for linking with various js/css libraries from
    // `ir_visualizer/dependencies.html`
    void generate_dependency_links() {
        std::ifstream deps_file(string(__DIR__) + "ir_visualizer/dependencies.html");
        internal_assert(!deps_file.fail()) << "Failed to find `dependencies.html` inside "
                                           << string(__DIR__)
                                           << "ir_visualizer directory.\n ";
        stream << deps_file.rdbuf();
    }

    // Loads the stylesheet code from `ir_visualizer/stylesheet.html`
    void generate_stylesheet() {
        std::ifstream css_file(string(__DIR__) + "ir_visualizer/stylesheet.html");
        internal_assert(!css_file.fail()) << "Failed to find `stylesheet.html` inside "
                                          << string(__DIR__)
                                          << "ir_visualizer directory.\n ";
        stream << css_file.rdbuf();
    }

    // Loads and initializes the javascript template from `ir_visualizer / javascript_template.html`
    void generate_javascript() {
        // 1. Load the javascript code template
        std::ifstream js_template_file(string(__DIR__) + "ir_visualizer/javascript_template.html");
        internal_assert(!js_template_file.fail()) << "Failed to find `javascript_template.html` inside "
                                                  << string(__DIR__) 
                                                  << "ir_visualizer directory.\n ";
        ostringstream js_template;
        js_template << js_template_file.rdbuf();
        js_template_file.close();

        // 2. Initialize template with concrete values for `tooltip_count`,
        // `stmt_hierarchy_tooltip_count` and `ir_viz_tooltip_count`.
        string js = js_template.str();
        std::regex r1("\\{\\{tooltip_count\\}\\}");
        js = std::regex_replace(js, r1, std::to_string(tooltip_count));
        std::regex r2("\\{\\{stmt_hierarchy_tooltip_count\\}\\}");
        js = std::regex_replace(js, r2, std::to_string(get_stmt_hierarchy.get_tooltip_count()));
        std::regex r3("\\{\\{ir_viz_tooltip_count\\}\\}");
        js = std::regex_replace(js, r3, std::to_string(ir_visualization.get_tooltip_count()));

        // 3. Write initialized template to stream
        stream << js;
    }

    void generate_head() {
        stream << "<head>";
        // TODO: Generate title, author and other metadata
        generate_dependency_links();
        generate_stylesheet();
        stream << "</head>\n";
    }

    void generate_body(const Module &m) {
        stream << "<body>\n";

        stream << "<div class='outerDiv'>\n";

        stream << information_bar(m);

        stream << "<div class='mainContent'>\n";

        // print main html page
        stream << "<div class='IRCode-code' id='IRCode-code'>\n";
        print(m);
        stream << "</div>\n";

        // for resizing the code and visualization divs
        stream << resize_bar();

        stream << "<div class='IRVisualization' id='IRVisualization'>\n";
        stream << generate_ir_visualization(m);
        stream << "</div>\n";

        // for resizing the visualization and assembly code divs
        stream << resize_bar_assembly();

        // assembly content
        stream << "<div id='assemblyCode'>\n";
        stream << "</div>\n";

        stream << "</div>\n";  // close mainContent div
        stream << "</div>\n";  // close outerDiv div

        // put assembly code in an invisible div
        stream << get_assembly_info_viz.get_assembly_html();

        // closing parts of the html
        stream << "<div class='popups'>\n";
        stream << popups;
        stream << "</div>\n";

        // Include javascript template.
        generate_javascript();

        stream << "</body>";
    }

    void generate_html(const string &filename, const Module &m) {
        get_assembly_info_viz.generate_assembly_information(m, filename);

        // Create the output file
        stream.open(filename.c_str());

        generate_head();

        generate_body(m);
    }

    StmtToViz(const string &filename, const Module &m)
        : id_count(0), get_stmt_hierarchy(generate_costs(m)), ir_visualization(find_stmt_cost),
          if_count(0), producer_consumer_count(0), for_count(0), store_count(0), allocate_count(0),
          functionCount(0), tooltip_count(0), popup_count(0), context_stack(1, 0) {
    }
};

*/

}  // namespace Internal
}  // namespace Halide
