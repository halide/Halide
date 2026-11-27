#include "StmtToHTML.h"
#include "Debug.h"
#include "Error.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "Module.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>
#include <string_view>
#include <utility>

// Setting this to 0 is meant to speed up the development iteration cycle.
// Not inlining these template files, but just linking them by an absolute path
// causes you to be able to just edit the files without having to recompile Halide
// and then rerun your generator.
// For distribution purposes, they should be inlined, and this define should be on 1.
#define INLINE_TEMPLATES 1

#if !INLINE_TEMPLATES
#include <filesystem>
#endif

extern "C" unsigned char halide_html_template_StmtToHTML_css[];
extern "C" unsigned char halide_html_template_StmtToHTML_js[];

namespace Halide {
namespace Internal {

namespace {

std::string escape_html(std::string src) {
    src = replace_all(std::move(src), "&", "&amp;");
    src = replace_all(std::move(src), "<", "&lt;");
    src = replace_all(std::move(src), ">", "&gt;");
    src = replace_all(std::move(src), "\"", "&quot;");
    src = replace_all(std::move(src), "/", "&#x2F;");
    src = replace_all(std::move(src), "'", "&#39;");
    return src;
}

// Writes a line of text into a <script type='text/plain'>. Such a script
// ends at "</script", and "<!--" changes how that end is found, so they are
// left out.
void write_script_text(std::ostream &stream, std::string_view line) {
    if (line.find('<') == std::string_view::npos) {
        stream << line;
        return;
    }
    const auto unsafe_length = [](std::string_view at) -> size_t {
        const std::string_view close = "</script";
        if (at.substr(0, 4) == "<!--") {
            return 4;
        }
        if (at.size() >= close.size() &&
            std::equal(close.begin(), close.end(), at.begin(),
                       [](char a, char b) { return a == std::tolower((unsigned char)b); })) {
            return close.size();
        }
        return 0;
    };
    std::string text(line);
    for (size_t i = 0; i < text.size();) {
        if (const size_t n = unsafe_length(std::string_view(text).substr(i))) {
            text.erase(i, n);
            // What is around the erased text may be unsafe together.
            i -= std::min(i, (size_t)8);
        } else {
            i++;
        }
    }
    stream << text;
}

// The basic blocks that code generation makes for loops and produce/consume
// blocks are named after them, e.g. "3_for_f.s0.x": the IR node of each such
// name in the functions of m, numbered the same way.
std::map<std::string, const IRNode *> block_nodes(const Module &m) {
    std::map<std::string, const IRNode *> result;
    int for_loop_id = 0, producer_consumer_id = 0;
    for (const auto &f : m.functions()) {
        visit_with(
            f.body,
            [&](auto *self, const For *op) {
                if (op->for_type == ForType::Serial) {
                    result[std::to_string(++for_loop_id) + "_for_" + op->name] = op;
                }
                self->visit_base(op);
            },
            [&](auto *self, const ProducerConsumer *op) {
                result[std::to_string(++producer_consumer_id) + (op->is_producer ? "_produce_" : "_consume_") + op->name] = op;
                self->visit_base(op);
            });
    }
    return result;
}

// Writes the assembly to the stream, as the text of a <script
// type='text/plain'>, and returns for each line of it the 1-based line of the
// stmt it was generated for, or 0.
//
// The assembly names the basic block that each label starts in a comment, as
// %"name". The instructions of a block made for a node of block_nodes are
// mapped to the line to_line gives for that node.
std::vector<uint32_t> map_assembly(std::ostream &stream, std::string_view code,
                                   const std::map<std::string, const IRNode *> &block_nodes,
                                   const std::function<uint32_t(const IRNode *)> &to_line) {
    std::vector<uint32_t> stmt_lines;
    uint32_t current = 0;
    size_t start = 0;
    while (start < code.size()) {
        size_t end = code.find('\n', start);
        if (end == std::string_view::npos) {
            end = code.size();
        }
        std::string_view line = code.substr(start, end - start);
        start = end + 1;

        size_t first = line.find_first_not_of(" \t");
        std::string_view trimmed = first == std::string_view::npos ? std::string_view{} : line.substr(first);

        // A label, or the comment that stands in for the label of a block
        // that is only fallen into (e.g. "# %bb.3:"), starts a block.
        const size_t space = trimmed.find_first_of(" \t");
        const std::string_view head = trimmed.substr(0, space);
        const bool starts_block = (!head.empty() && head.back() == ':') ||
                                  trimmed.find("%bb.") != std::string_view::npos;
        if (starts_block) {
            current = 0;
            const size_t quote = line.find("%\"");
            if (quote != std::string_view::npos) {
                const size_t close = line.find('"', quote + 2);
                const std::string name(line.substr(quote + 2, close == std::string_view::npos ? std::string_view::npos : close - quote - 2));
                auto it = block_nodes.find(name);
                if (it != block_nodes.end()) {
                    current = to_line(it->second);
                }
            }
        }

        // Only instructions are code; labels and directives are not.
        const bool is_instruction = first != 0 && !trimmed.empty() &&
                                    trimmed[0] != '.' && trimmed[0] != '#' && trimmed[0] != '/' &&
                                    trimmed[0] != ';' && trimmed.back() != ':';
        if (!stmt_lines.empty()) {
            stream << "\n";
        }
        if (line.length() > 500) {
            // Very long lines in the assembly are typically the
            // _gpu_kernel_sources or other buffers (such as static LUTs) as a
            // raw ASCII block in the assembly. Let's chop that off to make
            // browsers faster when dealing with this.
            write_script_text(stream, line.substr(0, 200));
            stream << "\" # omitted the remainder of the buffer";
        } else {
            write_script_text(stream, line);
        }
        stmt_lines.push_back(is_instruction ? current : 0);
    }
    return stmt_lines;
}

/** The text of IR, line by line, with its HTML: written to by an
 * HTMLStmtPrinter, which marks it up. */
class HTMLLines : public std::streambuf {
public:
    struct Line {
        /** The text of the line, exactly as in the stmt file. */
        std::string text;
        /** The text, escaped, with the markup in it. */
        std::string html;
        /** The Stmt the line is about: the first one that began on it, apart
         * from those that just hold others. */
        const IRNode *stmt = nullptr;
    };
    std::vector<Line> lines = std::vector<Line>(1);

    // Opens a span, which continues on the next lines until it is closed.
    void open(std::string tag) {
        lines.back().html += tag;
        open_tags.push_back(std::move(tag));
    }

    void close() {
        lines.back().html += "</span>";
        open_tags.pop_back();
    }

protected:
    std::vector<std::string> open_tags;

    void put(char c) {
        if (c == '\n') {
            // Close the spans at the end of the line, and reopen them on
            // the next one.
            for (size_t i = 0; i < open_tags.size(); i++) {
                lines.back().html += "</span>";
            }
            lines.emplace_back();
            for (const std::string &tag : open_tags) {
                lines.back().html += tag;
            }
            return;
        }
        Line &line = lines.back();
        line.text += c;
        switch (c) {
        case '&':
            line.html += "&amp;";
            break;
        case '<':
            line.html += "&lt;";
            break;
        case '>':
            line.html += "&gt;";
            break;
        default:
            if ((unsigned char)c < ' ' && c != '\t') {
                // Show control characters (e.g. the terminator of an
                // embedded string) as the symbols for them.
                line.html += "&#" + std::to_string(0x2400 + (int)c) + ";";
            } else {
                line.html += c;
            }
        }
    }

    int overflow(int c) override {
        if (c != EOF) {
            put((char)c);
        }
        return c;
    }

    std::streamsize xsputn(const char *str, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; i++) {
            put(str[i]);
        }
        return n;
    }
};

// What the assembly calls the basic block of a loop or a produce/consume
// block, without the number that code generation gives it.
std::string block_name(const IRNode *node) {
    if (node->node_type == IRNodeType::For) {
        return "for " + ((const For *)node)->name;
    }
    const ProducerConsumer *pc = (const ProducerConsumer *)node;
    return (pc->is_producer ? "produce " : "consume ") + pc->name;
}

/** Prints IR exactly as IRPrinter does, into HTMLLines, with each Expr and
 * each styled piece of text in a span. */
class HTMLStmtPrinter : public IRPrinter {
public:
    HTMLStmtPrinter(std::ostream &stream, HTMLLines &lines)
        : IRPrinter(stream), lines(lines) {
    }

    /** The 1-based line of each loop and produce/consume block printed, and
     * of the first of them with each block_name. */
    std::map<const IRNode *, uint32_t> node_lines;
    std::map<std::string, uint32_t> name_lines;

    void begin_style(IRPrinterStyle style) const override {
        if (style == IRPrinterStyle::Paren || style == IRPrinterStyle::BoldParen) {
            // The punctuation of a node is highlighted together.
            lines.open("<span class='op d" + std::to_string(paren_depth % 6) +
                       (style == IRPrinterStyle::BoldParen ? " bold" : "") +
                       "' data-g='" + std::to_string(groups.empty() ? 0 : groups.back()) + "'>");
        } else {
            // Indexed by IRPrinterStyle.
            static const char *const classes[] = {"hl", "dim", "kw", "num", "num", "str", "var", "buf", "fn", "type",
                                                  "var gpu-x", "var gpu-y", "var gpu-z"};
            lines.open(std::string("<span class='") + classes[(int)style] + "'>");
        }
    }

    void end_style(IRPrinterStyle) const override {
        lines.close();
    }

protected:
    HTMLLines &lines;

    // A number for each node being printed, innermost last.
    std::vector<int> groups;
    int num_groups = 0;

    void begin_node(const IRNode *node) override {
        groups.push_back(++num_groups);
        if (node->node_type <= StrongestExprNodeType) {
            std::ostringstream type;
            type << ((const BaseExprNode *)node)->type;
            lines.open("<span class='e' title='" + escape_html(type.str()) + "'>");
            return;
        }
        HTMLLines::Line &line = lines.lines.back();
        if (!line.stmt && node->node_type != IRNodeType::Block && node->node_type != IRNodeType::Fork) {
            line.stmt = node;
        }
        if (node->node_type == IRNodeType::For || node->node_type == IRNodeType::ProducerConsumer) {
            node_lines[node] = (uint32_t)lines.lines.size();
            name_lines.emplace(block_name(node), (uint32_t)lines.lines.size());
        }
    }

    void end_node(const IRNode *node) override {
        groups.pop_back();
        if (node->node_type <= StrongestExprNodeType) {
            lines.close();
        }
    }
};

/** Prints the lines of IR in HTML, one div per line, after a gutter with the
 * line number and a toggle to collapse the block that the line starts. */
void print_lines(std::ostream &stream, const std::vector<HTMLLines::Line> &lines, const std::vector<bool> &has_asm) {
    // The text ends with a newline, which starts a line with nothing on it.
    const uint32_t num_lines = (uint32_t)lines.size() - (lines.size() > 1 && lines.back().text.empty());
    auto line_text = [&](uint32_t line) -> const std::string & {
        return lines[line - 1].text;
    };

    // Blocks of code: from a line that ends in an opening brace, up to the
    // line that starts with the matching closing brace.
    std::vector<uint32_t> block_end(num_lines + 1, 0);
    {
        std::vector<uint32_t> open;
        for (uint32_t l = 1; l <= num_lines; l++) {
            const std::string &t = line_text(l);
            size_t first = t.find_first_not_of(' ');
            size_t last = t.find_last_not_of(' ');
            if (first == std::string::npos) {
                continue;
            }
            if (t[first] == '}' && !open.empty()) {
                block_end[open.back()] = l;
                open.pop_back();
            }
            if (t[last] == '{') {
                open.push_back(l);
            }
        }
    }

    // Parallel loops and loops over GPU blocks are shaded, and so are loops
    // over GPU threads, in another color. A shade starts at the column of the
    // outermost such loop around the line, or is -1 if there is none.
    std::vector<int> parallel_column(num_lines + 1, -1), thread_column(num_lines + 1, -1);
    for (uint32_t line = 1; line <= num_lines; line++) {
        const IRNode *s = lines[line - 1].stmt;
        if (!s || s->node_type != IRNodeType::For) {
            continue;
        }
        const ForType for_type = ((const For *)s)->for_type;
        std::vector<int> *column = nullptr;
        if (for_type == ForType::Parallel || for_type == ForType::GPUBlock) {
            column = &parallel_column;
        } else if (for_type == ForType::GPUThread || for_type == ForType::GPULane) {
            column = &thread_column;
        } else {
            continue;
        }
        const int indent = (int)line_text(line).find_first_not_of(' ');
        for (uint32_t l2 = line; l2 <= std::max(line, block_end[line]); l2++) {
            if ((*column)[l2] < 0) {
                (*column)[l2] = indent;
            }
        }
    }

    stream << "<div class='ir-code'>\n";
    for (uint32_t line = 1; line <= num_lines; line++) {
        stream << "<div class='l";
        if (parallel_column[line] >= 0) {
            stream << " in-par";
        }
        if (thread_column[line] >= 0) {
            stream << " in-thr";
        }
        stream << "' data-line='" << line << "'";
        // The indentation, so that a wrapped line continues indented too.
        const size_t indent = std::min(line_text(line).find_first_not_of(' '), line_text(line).size());
        if (indent > 0 || parallel_column[line] >= 0 || thread_column[line] >= 0) {
            stream << " style='";
            if (indent > 0) {
                stream << "--ind:" << indent << ";";
            }
            if (parallel_column[line] >= 0) {
                stream << "--par:" << parallel_column[line] << ";";
            }
            if (thread_column[line] >= 0) {
                stream << "--thr:" << thread_column[line] << ";";
            }
            stream << "'";
        }
        if (block_end[line]) {
            stream << " data-block-end='" << block_end[line] << "'";
        }
        if (line < has_asm.size() && has_asm[line]) {
            stream << " data-asm='1'";
        }
        stream << ">";

        // The gutter: line number, collapse toggle.
        stream << "<span class='g'><span class='n'>" << line << "</span>";
        stream << (block_end[line] ? "<span class='t'></span>" : "<span class='t none'></span>");
        stream << "</span>";

        stream << "<span class='c'>" << lines[line - 1].html << "</span>";
        stream << "</div>\n";
    }
    stream << "</div>\n";
}

/** PipelineHTMLInspector Class
 * Generates the output html page. Currently the html page has
 * three key tabs: IR code, Visualized pipeline and the generated
 * assembly.
 */
class PipelineHTMLInspector {
public:
    // Construct the visualizer and point it to the output file
    // Shows the conceptual stmt if conceptual is set.
    explicit PipelineHTMLInspector(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename,
                                   bool conceptual)
        : conceptual(conceptual) {
        // Open output file
        stream.open(html_output_filename.c_str());

        // Load assembly code -- if not explicit specified, assume it will have matching pathname
        // as our output file, with a different extension.
        if (assembly_input_filename.empty()) {
            // get_output_info() is the One True Source Of Truth for expected file suffixes,
            // so use that rather than hardcoding it here.
            const auto info = get_output_info(m.target());
            const std::string stmt_html_extension = info.at(OutputFileType::stmt_html).extension;
            const size_t pos = html_output_filename.rfind(stmt_html_extension);
            user_assert(pos != std::string::npos)
                << "Unable to find expected extension (" << stmt_html_extension
                << ") in filename (" << html_output_filename << ")\n";

            const std::string asm_extension = info.at(OutputFileType::assembly).extension;
            const std::string asm_file = html_output_filename.substr(0, pos) + asm_extension;
            load_asm_code(asm_file);
        } else {
            load_asm_code(assembly_input_filename);
        }
    }

    // Generate the html visualization of the input module
    void generate_html(const Module &m) {
        // The IR, exactly as the stmt files print it.
        std::ostream ir_stream(&ir_lines);
        HTMLStmtPrinter printer(ir_stream, ir_lines);
        if (conceptual) {
            printer.print(m.get_conceptual_stmt());
        } else {
            printer.print(m);
        }

        // Generate html page
        stream << "<!DOCTYPE html>\n";
        stream << "<html lang='en'>\n";
        generate_head(m);
        stream << "<body>\n";

        // The host assembly, which the page renders as it comes into view.
        // The assembly of the loops and produce/consume blocks is linked to
        // their lines of IR. The conceptual stmt was lowered further, so
        // there they are found by name.
        stream << "<script type='text/plain' id='asm-text'>";
        asm_ir_lines = map_assembly(stream, asm_buffer, block_nodes(m), [&](const IRNode *node) -> uint32_t {
            if (conceptual) {
                auto it = printer.name_lines.find(block_name(node));
                return it == printer.name_lines.end() ? 0 : it->second;
            }
            auto it = printer.node_lines.find(node);
            return it == printer.node_lines.end() ? 0 : it->second;
        });
        stream << "</script>\n";

        generate_body(m);
        stream << "</html>";
    }

private:
    // Handle to output file stream
    std::ofstream stream;

    // Whether the conceptual stmt is shown, rather than the stmt.
    bool conceptual;

    // The lines of IR shown.
    HTMLLines ir_lines;

    /* Methods for generating the <head> section of the html file */
    void generate_head(const Module &m) {
        stream << "<head>\n";
        stream << "<title>Halide Module: " << m.name() << "</title>\n";
#if INLINE_TEMPLATES
        stream << "<style type='text/css'>\n"
               << halide_html_template_StmtToHTML_css
               << "\n</style>\n";
#else
        std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path() / "irvisualizer";
        debug(1) << "Will link CSS in directory: " << dir << "\n";
        internal_assert(std::filesystem::exists(dir));
        stream << "<link rel='stylesheet' href='file://" << (dir / "html_template_StmtToHTML.css").string() << "'>\n";
#endif
        stream << "</head>\n";
    }

    /* Methods for generating the <body> section of the html file */
    void generate_body(const Module &m) {
        stream << "  <div id='page-container'>\n";
        generate_toolbar(m);
        stream << "     <div id='content'>\n";
        generate_visualization_panes(m);
        stream << "     </div>\n";
        stream << "  </div>\n";

        // The line of IR each line of the host assembly was generated for.
        stream << "<script>\nconst asmIRLines = [";
        for (size_t i = 0; i < asm_ir_lines.size(); i++) {
            stream << (i ? "," : "") << asm_ir_lines[i];
        }
        // How comments and immediates look in the assembly of this target.
        const Target &t = m.target();
        const bool arm32 = t.arch == Target::ARM && t.bits == 32;
        const bool apple = t.os == Target::MacOS || t.os == Target::IOS;
        const char *comment = t.arch == Target::ARM && !arm32 && apple                       ? ";" :
                              (t.arch == Target::ARM && !arm32) || t.arch == Target::Hexagon ? "//" :
                              arm32                                                          ? "@" :
                                                                                               "#";
        stream << "];\nconst asmSyntax = {comment: \"" << comment << "\", immediate: \""
               << (t.arch == Target::X86 ? "$" : "#") << "\", x86: " << (t.arch == Target::X86 ? "true" : "false")
               << "};\n</script>\n";

#if INLINE_TEMPLATES
        stream << "<script>\n"
               << halide_html_template_StmtToHTML_js
               << "</script>";
#else
        std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path() / "irvisualizer";
        debug(1) << "Will link Javascript in directory: " << dir << "\n";
        internal_assert(std::filesystem::exists(dir));
        stream << "<script src='file://" << (dir / "html_template_StmtToHTML.js").string() << "'></script>\n";
#endif
        stream << "</body>";
    }

    void generate_toolbar(const Module &m) {
        stream << "<div id='toolbar'>\n";

        // IR settings
        stream << "<form id='form-ir-settings' name='form-ir-settings'>IR:\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir-wrap' checked />Wrap</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir-line-nums' checked />Line numbers</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-rainbow-parens' />Rainbow Parentheses</label>\n";
        stream << "</form>\n";

        // Which panes to show
        stream << "<form id='form-panes' name='form-panes'>Panes:\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-ir' checked />Show IR</label>\n";
        stream << "   <label><input type='checkbox' name='checkbox-show-assembly' checked />Show Assembly</label>\n";
        Buffer<> device_code_buf = m.get_device_code_buffer();
        if (device_code_buf.defined()) {
            stream << "   <label><input type='checkbox' name='checkbox-show-device-code' checked />Show Device Code</label>\n";
        }
        stream << "</form>\n";

        // Color theme
        stream << "<form id='form-theme' name='form-theme'>Theme:\n";
        stream << "    <label><input type='radio' name='theme' value='auto' checked />Auto</label>\n";
        stream << "    <label><input type='radio' name='theme' value='classic-dark' />Classic Dark</label>\n";
        stream << "    <label><input type='radio' name='theme' value='classic-light' />Classic Light</label>\n";
        stream << "    <label><input type='radio' name='theme' value='gruvbox-dark' />Gruvbox Dark</label>\n";
        stream << "<label><input type='radio' name='theme' value='gruvbox-light' />Gruvbox Light</label>\n";

        stream << "</form>\n";
        stream << "</div>\n";
    }

    // Generate the three visualization panes
    void generate_visualization_panes(const Module &m) {
        int pane_count = 0;
        stream << "<div id='visualization-panes'>\n";
        stream << "<div id='resizer-preview' style='display:none;'></div>\n";
        generate_ir_pane();
        generate_resize_bar(pane_count++);
        generate_host_assembly_pane();
        Buffer<> device_code_buf = m.get_device_code_buffer();
        if (device_code_buf.defined()) {
            generate_resize_bar(pane_count++);
            generate_device_code_pane(device_code_buf);
        }

        stream << "</div>\n";
    }

    // Generate pane: Lowered IR code with syntax highlighting in HTML
    void generate_ir_pane() {
        stream << "<div id='ir-code-pane' class='pane" << (conceptual ? " conceptual" : "") << "'>\n";
        std::vector<bool> has_asm;
        for (uint32_t line : asm_ir_lines) {
            if (line >= has_asm.size()) {
                has_asm.resize(line + 1, false);
            }
            has_asm[line] = true;
        }
        print_lines(stream, ir_lines.lines, has_asm);
        stream << "</div>\n";
    }

    // Generate pane: Generated host assembly code, rendered by the page
    void generate_host_assembly_pane() {
        stream << "<div id='host-assembly-pane' class='pane'>\n";
        stream << "<div id='asm-code'>\n";
        stream << "<div id='asm-highlights'></div>\n";
        stream << "<div id='assemblyContent'></div>\n";
        stream << "</div>\n";
        stream << "</div>\n";
    }

    // Generate pane: Generated device code
    void generate_device_code_pane(const Buffer<> &buf) {
        stream << "<div id='device-code-pane' class='pane'>\n";
        int length = buf.size_in_bytes();
        while (length > 0 && ((const char *)buf.data())[length - 1] == '\0') {
            length--;
        }
        const std::string code((const char *)buf.data(), length);
        const bool ptx = starts_with(buf.name(), "cuda_");

        // The page highlights the code, PTX in its own way.
        stream << "<div class='code" << (ptx ? " ptx" : "") << "'>\n";
        int number = 0;
        for (size_t start = 0; start < code.size();) {
            const size_t end = std::min(code.find('\n', start), code.size());
            stream << "<span class='line' data-n='" << ++number << "'>"
                   << escape_html(code.substr(start, end - start)) << "</span>\n";
            start = end + 1;
        }
        stream << "</div>\n";
        stream << "</div>\n";
    }

    // Generate a resizing bar to control the width of code and visualization panes
    void generate_resize_bar(int num) {
        stream << "<div class='resize-bar' id='resize-bar-" << num << "'>\n";
        stream << " <div class='collapse-btns'>\n";
        stream << "  <div>\n";
        stream << "   <button class='collapse-left' onclick='collapseTab(" << num << ")' title='Collapse pane on the left'>\n";
        stream << "   </button>\n";
        stream << "  </div>\n";
        stream << "  <div>\n";
        stream << "    <button class='collapse-right' onclick='collapseTab(" << (num + 1) << ")' title='Collapse pane on the right'>\n";
        stream << "    </button>\n";
        stream << "  </div>\n";
        stream << " </div>\n";
        stream << "</div>\n";
    }

    /* Misc helper methods */

    // The assembly code, loaded from file, and for each line of it the line
    // of IR it was generated for, or 0.
    std::string asm_buffer;
    std::vector<uint32_t> asm_ir_lines;

    void load_asm_code(const std::string &asm_file) {
        user_assert(file_exists(asm_file)) << "Unable to open assembly file: " << asm_file << "\n";
        std::ifstream assembly(asm_file.c_str());
        std::stringstream ss;
        ss << assembly.rdbuf();
        asm_buffer = std::move(ss).str();
    }
};

}  // namespace

// The external interface to this module
void print_to_stmt_html(const std::string &html_output_filename,
                        const Module &m,
                        const std::string &assembly_input_filename) {
    PipelineHTMLInspector inspector(html_output_filename, m, assembly_input_filename, false);
    inspector.generate_html(m);
    debug(1) << "Done generating HTML IR Inspector - printed to: " << html_output_filename << "\n";
}

void print_to_conceptual_stmt_html(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename) {
    PipelineHTMLInspector inspector(html_output_filename, m, assembly_input_filename, true);
    inspector.generate_html(m);
    debug(1) << "Done generating HTML Conceptual IR Inspector - printed to: " << html_output_filename << "\n";
}

}  // namespace Internal
}  // namespace Halide
