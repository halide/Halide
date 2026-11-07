#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

// The text of each line of IR in an html output, in order of their line
// numbers, which must be consecutive from 1.
bool html_lines(const std::string &html, std::vector<std::string> &lines) {
    // A line may have more classes than 'l', e.g. when it is shaded.
    const std::string line_open = "<div class='l";
    const std::string number_attribute = "data-line='";
    const std::string code_open = "<span class='c'>";
    for (size_t at = html.find(line_open); at != std::string::npos; at = html.find(line_open, at + 1)) {
        const char next = html[at + line_open.size()];
        if (next != '\'' && next != ' ') {
            continue;
        }
        int number = std::atoi(html.c_str() + html.find(number_attribute, at) + number_attribute.size());
        if (number != (int)lines.size() + 1) {
            printf("html line %d follows line %d\n", number, (int)lines.size());
            return false;
        }
        size_t c = html.find(code_open, at) + code_open.size();
        std::string text;
        int depth = 1;
        while (depth > 0) {
            if (html.compare(c, 5, "<span") == 0) {
                depth++;
                c = html.find('>', c) + 1;
            } else if (html.compare(c, 7, "</span>") == 0) {
                depth--;
                c += 7;
            } else if (html.compare(c, 5, "&amp;") == 0) {
                text += '&';
                c += 5;
            } else if (html.compare(c, 4, "&lt;") == 0) {
                text += '<';
                c += 4;
            } else if (html.compare(c, 4, "&gt;") == 0) {
                text += '>';
                c += 4;
            } else {
                text += html[c++];
            }
        }
        lines.push_back(text);
    }
    return true;
}

bool check_html(const std::string &html_path, const std::string &stmt_path) {
    const std::string html = read_file(html_path);
    const std::vector<std::string> stmt_lines = split_lines(read_file(stmt_path));
    std::vector<std::string> lines;
    if (!html_lines(html, lines)) {
        return false;
    }
    if (lines != stmt_lines) {
        for (size_t i = 0; i < std::max(lines.size(), stmt_lines.size()); i++) {
            std::string a = i < lines.size() ? lines[i] : "(none)";
            std::string b = i < stmt_lines.size() ? stmt_lines[i] : "(none)";
            if (a != b) {
                printf("%s line %d is\n  %s\ninstead of\n  %s\n", html_path.c_str(), (int)i + 1, a.c_str(), b.c_str());
                break;
            }
        }
        return false;
    }
    // Some loops are linked to their assembly.
    const std::string asm_lines = "const asmIRLines = [";
    size_t at = html.find(asm_lines);
    if (html.find("data-asm='1'") == std::string::npos || at == std::string::npos) {
        printf("%s links no IR to assembly\n", html_path.c_str());
        return false;
    }
    // ... and the assembly is linked to lines that exist.
    std::istringstream in(html.substr(at + asm_lines.size(), html.find(']', at) - at - asm_lines.size()));
    int linked = 0;
    for (std::string n; std::getline(in, n, ',');) {
        int line = std::atoi(n.c_str());
        if (line < 0 || line > (int)lines.size()) {
            printf("%s links assembly to line %d of %d\n", html_path.c_str(), line, (int)lines.size());
            return false;
        }
        linked += line > 0;
    }
    if (linked == 0) {
        printf("%s links no assembly to IR\n", html_path.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main() {
    Var x, y;

    // The gradient function and schedule from tutorial lesson 5.
    Func gradient_fast("gradient_fast");
    gradient_fast(x, y) = x + y;

    Var x_outer, y_outer, x_inner, y_inner, tile_index;
    gradient_fast
        .tile(x, y, x_outer, y_outer, x_inner, y_inner, 256, 256)
        .fuse(x_outer, y_outer, tile_index)
        .parallel(tile_index);

    Var x_inner_outer, y_inner_outer, x_vectors, y_pairs;
    gradient_fast
        .tile(x_inner, y_inner, x_inner_outer, y_inner_outer, x_vectors, y_pairs, 4, 2)
        .vectorize(x_vectors)
        .unroll(y_pairs);

    std::string result_file_1 = Internal::get_test_tmp_dir() + "stmt_to_html_dump_1.html";
    Internal::ensure_no_file_exists(result_file_1);
    gradient_fast.compile_to_lowered_stmt(result_file_1, {}, Halide::HTML);
    Internal::assert_file_exists(result_file_1);

    // Also check using an image.
    std::string result_file_2 = Internal::get_test_tmp_dir() + "stmt_to_html_dump_2.html";
    Internal::ensure_no_file_exists(result_file_2);
    Buffer<int> im(800, 600);
    gradient_fast.compile_to_lowered_stmt(result_file_2, {im}, Halide::HTML);
    Internal::assert_file_exists(result_file_2);

    // Check a multi-output pipeline.
    std::string result_file_3 = Internal::get_test_tmp_dir() + "stmt_to_html_dump_3.html";
    Internal::ensure_no_file_exists(result_file_3);
    Func tuple_func;
    tuple_func(x, y) = Tuple(x, y);
    tuple_func.compile_to_lowered_stmt(result_file_3, {}, Halide::HTML);
    Internal::assert_file_exists(result_file_3);

    // The html outputs show the IR exactly as the stmt outputs print it, and
    // link its loops to the assembly.
    {
        const std::string dir = Internal::get_test_tmp_dir();
        const std::string stmt = dir + "stmt_to_html.stmt";
        const std::string html = dir + "stmt_to_html.stmt.html";
        const std::string conceptual = dir + "stmt_to_html.conceptual.stmt";
        const std::string conceptual_html = dir + "stmt_to_html.conceptual.stmt.html";
        for (const auto &p : {stmt, html, conceptual, conceptual_html}) {
            Internal::ensure_no_file_exists(p);
        }
        gradient_fast.compile_to({{OutputFileType::stmt, stmt},
                                  {OutputFileType::stmt_html, html},
                                  {OutputFileType::conceptual_stmt, conceptual},
                                  {OutputFileType::conceptual_stmt_html, conceptual_html}},
                                 {}, "stmt_to_html", get_host_target());
        if (!check_html(html, stmt) || !check_html(conceptual_html, conceptual)) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
