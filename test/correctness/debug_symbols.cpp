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
    Buffer<int> out = f.realize({37, 5}, get_jit_target_from_environment().with_feature(Target::DebugSymbols));
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int correct = in(x, y) * 3 - 11;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
