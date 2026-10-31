#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
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
    // Some IR lines are linked to their assembly.
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

size_t count(const std::string &text, const std::string &needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        n++;
    }
    return n;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_host_target();
    if (target.os == Target::Windows) {
        printf("[SKIP] This test checks the DWARF of the LLVM assembly.\n");
        return 0;
    }
    target = target.with_feature(Target::DebugSymbols);

    ImageParam input(Int(32), 2, "input");
    Param<int> scale("scale"), offset("offset");
    Var x("x"), y("y");
    Func f("f");
    f(x, y) = input(x, y) * scale + offset;
    f.parallel(y).vectorize(x, 8);

    const std::string dir = Internal::get_test_tmp_dir();
    const std::string stmt_path = dir + "debug_symbols.stmt";
    const std::string ll_path = dir + "debug_symbols.ll";
    const std::string object_path = dir + "debug_symbols.o";
    for (const auto &p : {stmt_path, ll_path, object_path}) {
        Internal::ensure_no_file_exists(p);
    }
    f.compile_to({{OutputFileType::stmt, stmt_path},
                  {OutputFileType::llvm_assembly, ll_path},
                  {OutputFileType::object, object_path}},
                 {input, scale, offset}, "debug_symbols", target);
    Internal::assert_file_exists(object_path);

    const std::string stmt = read_file(stmt_path);
    const std::string ll = read_file(ll_path);
    const std::vector<std::string> stmt_lines = split_lines(stmt);

    // The debug info refers to the stmt file.
    if (ll.find("!DIFile(filename: \"debug_symbols.stmt\"") == std::string::npos ||
        ll.find("checksumkind: CSK_MD5") == std::string::npos) {
        printf("Missing or incomplete DIFile for the stmt\n");
        return 1;
    }

    // One subprogram per function in the stmt: the pipeline, and the closure
    // of its parallel loop.
    const size_t functions = count(stmt, " func ");
    const size_t subprograms = count(ll, "distinct !DISubprogram(");
    if (functions < 2 || subprograms != functions) {
        printf("Expected one subprogram for each of the %d functions, got %d\n",
               (int)functions, (int)subprograms);
        return 1;
    }

    // Every location points at something printed in the stmt.
    std::map<std::string, std::pair<int, int>> locations;
    std::regex location_re(R"((!\d+) = (?:distinct )?!DILocation\(line: (\d+), column: (\d+))");
    for (std::sregex_iterator it(ll.begin(), ll.end(), location_re), end; it != end; ++it) {
        int line = std::stoi((*it)[2]), column = std::stoi((*it)[3]);
        locations[(*it)[1]] = {line, column};
        if (line == 0) {
            // LLVM merged instructions from different lines.
            continue;
        }
        if (line > (int)stmt_lines.size() || column < 1 ||
            column > (int)stmt_lines[line - 1].size() ||
            stmt_lines[line - 1][column - 1] == ' ') {
            printf("Location %d:%d does not point at the stmt's text\n", line, column);
            return 1;
        }
    }
    if (locations.empty()) {
        printf("No locations\n");
        return 1;
    }

    // Multiplies point at the operator that made them.
    std::regex mul_re(R"(= mul [^\n]*!dbg (!\d+))");
    int muls = 0;
    for (std::sregex_iterator it(ll.begin(), ll.end(), mul_re), end; it != end; ++it) {
        auto loc = locations.find((*it)[1]);
        if (loc != locations.end() && loc->second.first > 0 &&
            stmt_lines[loc->second.first - 1][loc->second.second - 1] == '*') {
            muls++;
        }
    }
    if (muls == 0) {
        printf("No multiply points at a '*' in the stmt\n");
        return 1;
    }

    // Every loop variable is visible to the debugger.
    std::regex for_re(R"(\bfor \(([^,]+),)");
    int loops = 0;
    for (std::sregex_iterator it(stmt.begin(), stmt.end(), for_re), end; it != end; ++it) {
        std::string var = "!DILocalVariable(name: \"" + (*it)[1].str() + "\"";
        if (ll.find(var) == std::string::npos) {
            printf("No variable for loop over %s\n", (*it)[1].str().c_str());
            return 1;
        }
        loops++;
    }
    if (loops == 0) {
        printf("No loops in the stmt\n");
        return 1;
    }

    // So are the buffers, as halide_buffer_t.
    if (ll.find("!DILocalVariable(name: \"input.buffer\", arg: ") == std::string::npos ||
        ll.find("!DICompositeType(tag: DW_TAG_structure_type, name: \"halide_buffer_t\"") == std::string::npos) {
        printf("No description of the input buffer\n");
        return 1;
    }

    // JIT compilation with debug info still computes the right thing.
    Buffer<int> in(37, 5);
    in.for_each_element([&](int x, int y) { in(x, y) = x * 7 - y; });
    input.set(in);
    scale.set(3);
    offset.set(-11);
    Buffer<int> out = f.realize({37, 5}, get_jit_target_from_environment().without_feature(Target::DebugSymbolsConceptual).with_feature(Target::DebugSymbols));
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int correct = in(x, y) * 3 - 11;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    // The source maps hold the text of the stmt files.
    {
        Module m = f.compile_to_module({input, scale, offset}, "debug_symbols", target);
        std::ostringstream module_text, conceptual_text;
        module_text << m;
        conceptual_text << m.get_conceptual_stmt();
        if (Internal::print_with_source_map(m).text != module_text.str() ||
            Internal::print_with_source_map(m.get_conceptual_stmt()).text != conceptual_text.str()) {
            printf("Source map text differs from the printed IR\n");
            return 1;
        }
    }

    // The html outputs show the IR exactly as the stmt files do, and link it
    // to the assembly. Without debug_symbols, only the html has debug info.
    {
        Func g("g"), h("h");
        RDom r(0, 10);
        g(x, y) = x + y;
        g(r, y) += select(g(r, y) > 3, g(r, y) / 2, offset);
        h(x, y) = g(x, y) * scale;
        g.compute_at(h, y);
        h.specialize(scale > 2).vectorize(x, 4);
        h.parallel(y);

        const std::string html_path = dir + "debug_symbols_html.stmt.html";
        const std::string conceptual_html_path = dir + "debug_symbols_html.conceptual.stmt.html";
        const std::string html_stmt_path = dir + "debug_symbols_html.stmt";
        const std::string conceptual_stmt_path = dir + "debug_symbols_html.conceptual.stmt";
        const std::string html_object_path = dir + "debug_symbols_html.o";
        for (const auto &p : {html_path, conceptual_html_path, html_stmt_path, conceptual_stmt_path, html_object_path}) {
            Internal::ensure_no_file_exists(p);
        }
        h.compile_to({{OutputFileType::stmt_html, html_path},
                      {OutputFileType::conceptual_stmt_html, conceptual_html_path},
                      {OutputFileType::stmt, html_stmt_path},
                      {OutputFileType::conceptual_stmt, conceptual_stmt_path},
                      {OutputFileType::object, html_object_path}},
                     {scale, offset}, "debug_symbols_html", get_host_target());
        if (!check_html(html_path, html_stmt_path) ||
            !check_html(conceptual_html_path, conceptual_stmt_path)) {
            return 1;
        }
        // The object did not ask for debug info.
        const std::string object = read_file(html_object_path);
        if (object.find(".debug_line") != std::string::npos ||
            object.find("debug_symbols_html.stmt") != std::string::npos) {
            printf("The object has debug info without debug_symbols\n");
            return 1;
        }
    }

    // The html links the IR to the assembly for other platforms too, whose
    // assembly carries the debug info differently (e.g. CodeView on Windows).
    for (const char *other : {"x86-64-windows", "arm-64-macos", "arm-64-linux"}) {
        Func k("k");
        k(x, y) = x * scale + offset;
        k.vectorize(x, 4).parallel(y);
        const std::string other_html_path = dir + "debug_symbols_" + other + ".stmt.html";
        const std::string other_stmt_path = dir + "debug_symbols_" + other + ".stmt";
        for (const auto &p : {other_html_path, other_stmt_path}) {
            Internal::ensure_no_file_exists(p);
        }
        try {
            k.compile_to({{OutputFileType::stmt_html, other_html_path},
                          {OutputFileType::stmt, other_stmt_path}},
                         {scale, offset}, "debug_symbols_other", Target(other));
        } catch (const Halide::CompileError &e) {
            printf("[SKIP] Can't compile for %s: %s\n", other, e.what());
            continue;
        }
        if (!check_html(other_html_path, other_stmt_path)) {
            return 1;
        }
    }

    // With debug_symbols_conceptual, the debug info points into the
    // conceptual stmt instead.
    {
        const Target conceptual_target = get_host_target().with_feature(Target::DebugSymbolsConceptual);
        const std::string c_stmt_path = dir + "debug_symbols_c.stmt";
        const std::string c_conceptual_path = dir + "debug_symbols_c.conceptual.stmt";
        const std::string c_ll_path = dir + "debug_symbols_c.ll";
        const std::string c_html_path = dir + "debug_symbols_c.stmt.html";
        const std::string c_conceptual_html_path = dir + "debug_symbols_c.conceptual.stmt.html";
        for (const auto &p : {c_stmt_path, c_conceptual_path, c_ll_path, c_html_path, c_conceptual_html_path}) {
            Internal::ensure_no_file_exists(p);
        }
        f.compile_to({{OutputFileType::stmt, c_stmt_path},
                      {OutputFileType::conceptual_stmt, c_conceptual_path},
                      {OutputFileType::llvm_assembly, c_ll_path},
                      {OutputFileType::stmt_html, c_html_path},
                      {OutputFileType::conceptual_stmt_html, c_conceptual_html_path}},
                     {input, scale, offset}, "debug_symbols_c", conceptual_target);

        const std::string c_ll = read_file(c_ll_path);
        const std::vector<std::string> c_lines = split_lines(read_file(c_conceptual_path));
        if (c_ll.find("!DIFile(filename: \"debug_symbols_c.conceptual.stmt\"") == std::string::npos) {
            printf("The debug info does not refer to the conceptual stmt\n");
            return 1;
        }
        std::map<std::string, std::pair<int, int>> c_locations;
        std::regex location_re(R"((!\d+) = (?:distinct )?!DILocation\(line: (\d+), column: (\d+))");
        for (std::sregex_iterator it(c_ll.begin(), c_ll.end(), location_re), end; it != end; ++it) {
            int line = std::stoi((*it)[2]), column = std::stoi((*it)[3]);
            c_locations[(*it)[1]] = {line, column};
            if (line == 0) {
                continue;
            }
            if (line > (int)c_lines.size() || column < 1 ||
                column > (int)c_lines[line - 1].size() ||
                c_lines[line - 1][column - 1] == ' ') {
                printf("Location %d:%d does not point at the conceptual stmt's text\n", line, column);
                return 1;
            }
        }
        std::regex mul_re(R"(= mul [^\n]*!dbg (!\d+))");
        int c_muls = 0;
        for (std::sregex_iterator it(c_ll.begin(), c_ll.end(), mul_re), end; it != end; ++it) {
            auto loc = c_locations.find((*it)[1]);
            if (loc != c_locations.end() && loc->second.first > 0 &&
                c_lines[loc->second.first - 1][loc->second.second - 1] == '*') {
                c_muls++;
            }
        }
        if (c_muls == 0) {
            printf("No multiply points at a '*' in the conceptual stmt\n");
            return 1;
        }
        if (!check_html(c_html_path, c_stmt_path) ||
            !check_html(c_conceptual_html_path, c_conceptual_path)) {
            return 1;
        }

        // The debug info can't point into both.
        bool refused = false;
        try {
            f.compile_to({{OutputFileType::llvm_assembly, c_ll_path}},
                         {input, scale, offset}, "debug_symbols_c",
                         conceptual_target.with_feature(Target::DebugSymbols));
        } catch (const Halide::CompileError &) {
            refused = true;
        }
        if (!refused) {
            printf("Asking for both kinds of debug symbols was not an error\n");
            return 1;
        }

        // JIT compilation with it still computes the right thing.
        Buffer<int> c_out = f.realize({37, 5}, get_jit_target_from_environment().without_feature(Target::DebugSymbols).with_feature(Target::DebugSymbolsConceptual));
        for (int y = 0; y < c_out.height(); y++) {
            for (int x = 0; x < c_out.width(); x++) {
                if (c_out(x, y) != in(x, y) * 3 - 11) {
                    printf("c_out(%d, %d) = %d instead of %d\n", x, y, c_out(x, y), in(x, y) * 3 - 11);
                    return 1;
                }
            }
        }
    }

    // GPU device code compiled with LLVM (PTX) gets line information that
    // points into the conceptual stmt, which is where the code of its kernels
    // is, with gpu_debug_symbols. The html links it to the conceptual stmt
    // either way.
    for (bool with_gpu_debug_symbols : {false, true}) {
        ImageParam gpu_input(Float(32), 2, "gpu_input");
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        Func blur("blur"), gpu_out("gpu_out");
        blur(x, y) = (gpu_input(x, y) + gpu_input(x + 1, y)) / 2;
        gpu_out(x, y) = select(blur(x, y) > 0.5f, blur(x, y) * 3.0f, blur(x, y + 1));
        gpu_out.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        blur.compute_at(gpu_out, xo).gpu_threads(x, y);

        const std::string ptx_path = dir + "debug_symbols_gpu.ptx";
        const std::string conceptual_stmt_path = dir + "debug_symbols_gpu.conceptual.stmt";
        const std::string conceptual_html_path = dir + "debug_symbols_gpu.conceptual.stmt.html";
        const std::string gpu_object_path = dir + "debug_symbols_gpu.o";
        for (const auto &p : {ptx_path, conceptual_stmt_path, conceptual_html_path, gpu_object_path}) {
            Internal::ensure_no_file_exists(p);
        }
        bool compiled = true;
        Target gpu_target = get_host_target().with_feature(Target::CUDA);
        if (with_gpu_debug_symbols) {
            gpu_target = gpu_target.with_feature(Target::GPUDebugSymbols);
        }
        try {
            gpu_out.compile_to({{OutputFileType::device_code, ptx_path},
                                {OutputFileType::conceptual_stmt, conceptual_stmt_path},
                                {OutputFileType::conceptual_stmt_html, conceptual_html_path},
                                {OutputFileType::object, gpu_object_path}},
                               {gpu_input}, "debug_symbols_gpu", gpu_target);
        } catch (const Halide::CompileError &e) {
            printf("[SKIP] No CUDA backend: %s\n", e.what());
            compiled = false;
        }
        if (compiled) {
            const std::string ptx = read_file(ptx_path);
            const std::vector<std::string> conceptual = split_lines(read_file(conceptual_stmt_path));
            if (ptx.find(", debug") != std::string::npos) {
                printf("The PTX asks for debug compilation\n");
                return 1;
            }
            if (!with_gpu_debug_symbols &&
                (ptx.find(".loc") != std::string::npos || ptx.find("$L__tmp") != std::string::npos ||
                 ptx.find(".debug_") != std::string::npos)) {
                printf("The PTX has line information without gpu_debug_symbols\n");
                return 1;
            }
            std::regex loc_re(R"(\.loc\s+\d+ (\d+) (\d+))");
            int locs = 0;
            for (std::sregex_iterator it(ptx.begin(), ptx.end(), loc_re), end; it != end; ++it) {
                int line = std::stoi((*it)[1]), column = std::stoi((*it)[2]);
                if (line == 0 || column == 0) {
                    continue;
                }
                if (line > (int)conceptual.size() || column > (int)conceptual[line - 1].size() ||
                    conceptual[line - 1][column - 1] == ' ') {
                    printf("PTX location %d:%d does not point at the conceptual stmt's text\n", line, column);
                    return 1;
                }
                locs++;
            }
            if ((locs > 0) != with_gpu_debug_symbols) {
                printf("The PTX has %d locations %s gpu_debug_symbols\n", locs, with_gpu_debug_symbols ? "with" : "without");
                return 1;
            }
            const std::string html = read_file(conceptual_html_path);
            if (html.find("data-ir='") == std::string::npos || html.find("data-device='1'") == std::string::npos) {
                printf("The html does not link device code to the IR\n");
                return 1;
            }
        }
    }

    // C-like GPU source, e.g. OpenCL C, gets #line directives that say which
    // line of the conceptual stmt each line was made from, with
    // gpu_debug_symbols. The html links it to the conceptual stmt either way.
    {
        ImageParam cl_input(Float(32), 2, "cl_input");
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        Func cl_blur("cl_blur"), cl_out("cl_out");
        cl_blur(x, y) = (cl_input(x, y) + cl_input(x + 1, y)) / 2;
        cl_out(x, y) = select(cl_blur(x, y) > 0.5f, cl_blur(x, y) * 3.0f, cl_blur(x, y + 1));
        cl_out.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        cl_blur.compute_at(cl_out, xo).gpu_threads(x, y);

        const std::string cl_source_path = dir + "debug_symbols_cl.cl";
        const std::string cl_conceptual_path = dir + "debug_symbols_cl.conceptual.stmt";
        const std::string cl_html_path = dir + "debug_symbols_cl.conceptual.stmt.html";
        for (bool with_gpu_debug_symbols : {false, true}) {
            for (const auto &p : {cl_source_path, cl_conceptual_path, cl_html_path}) {
                Internal::ensure_no_file_exists(p);
            }
            Target cl_target = get_host_target().with_feature(Target::OpenCL);
            if (with_gpu_debug_symbols) {
                cl_target = cl_target.with_feature(Target::GPUDebugSymbols);
            }
            cl_out.compile_to({{OutputFileType::device_code, cl_source_path},
                               {OutputFileType::conceptual_stmt, cl_conceptual_path},
                               {OutputFileType::conceptual_stmt_html, cl_html_path}},
                              {cl_input}, "debug_symbols_cl", cl_target);
            const std::string source = read_file(cl_source_path);
            const std::vector<std::string> conceptual = split_lines(read_file(cl_conceptual_path));
            std::regex line_re(R"re(#line (\d+) "([^"]*)")re");
            int directives = 0;
            for (std::sregex_iterator it(source.begin(), source.end(), line_re), end; it != end; ++it) {
                directives++;
                const std::string file = (*it)[2];
                if (file != "debug_symbols_cl.conceptual.stmt") {
                    continue;
                }
                const int line = std::stoi((*it)[1]);
                if (line < 1 || line > (int)conceptual.size() ||
                    conceptual[line - 1].find_first_not_of(' ') == std::string::npos) {
                    printf("#line %d does not point at a line of the conceptual stmt\n", line);
                    return 1;
                }
            }
            if ((directives > 0) != with_gpu_debug_symbols) {
                printf("%d #line directives %s gpu_debug_symbols\n", directives, with_gpu_debug_symbols ? "with" : "without");
                return 1;
            }
            const std::string html = read_file(cl_html_path);
            const bool linked = html.find("data-ir='") != std::string::npos &&
                                html.find("data-device='1'") != std::string::npos;
            if (!linked) {
                printf("The html does not link the OpenCL C to the IR %s gpu_debug_symbols\n",
                       with_gpu_debug_symbols ? "with" : "without");
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
