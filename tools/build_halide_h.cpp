#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

std::set<std::string> done, listed;

void dump_header(const std::string &header) {
    if (done.find(header) != done.end()) {
        return;
    }
    done.insert(header);

    if (header.find("runtime_internal") != std::string::npos) {
        fprintf(stdout, "#error \"COMPILING_HALIDE_RUNTIME should never be defined for Halide.h\"\n");
        return;
    }

    if (!listed.count(header)) {
        fprintf(stderr, "Error: %s is transitively included by the files listed, "
                        "but is not one of the files listed. The list of files that go "
                        "into making Halide.h is stale.\n",
                header.c_str());
        abort();
    }

    FILE *f = fopen(header.c_str(), "r");

    if (f == nullptr) {
        fprintf(stderr, "Could not open header %s.\n", header.c_str());
        exit(1);
    }

    char line[1024];
    const int line_len = sizeof(line);
    const char include_str[] = "#include \"";
    const int include_str_len = sizeof(include_str) - 1;  // remove null terminator

    while (fgets(line, line_len, f)) {
        if (strncmp(line, include_str, include_str_len) == 0) {
            char *sub_header = line + include_str_len;
            for (int i = 0; i < line_len - include_str_len; i++) {
                if (sub_header[i] == '"') {
                    sub_header[i] = 0;
                }
            }
            size_t slash_pos = header.rfind('/');
            std::string path;
            if (slash_pos != std::string::npos) {
                path = header.substr(0, slash_pos + 1);
            }
            dump_header(path + sub_header);
        } else {
            fputs(line, stdout);
        }
    }

    fclose(f);
}

int main(int argc, char **argv) {

    if (argc < 3) {
        fprintf(stderr, "Usage: %s LICENSE.txt [headers...]\n", argv[0]);
        exit(1);
    }

    fprintf(stdout, "/* Halide.h -- interface for the 'Halide' library.\n\n");

    {
        FILE *f = fopen(argv[1], "r");
        if (f == nullptr) {
            fprintf(stderr, "Could not open %s.\n", argv[1]);
            exit(1);
        }

        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            fprintf(stdout, "   %s", line);
        }

        fclose(f);
    }

    listed.insert(argv + 2, argv + argc);

    fprintf(stdout, "\n*/\n\n");
    fprintf(stdout, "#ifndef HALIDE_H\n");
    fprintf(stdout, "#define HALIDE_H\n\n");

    for (const auto &f : listed) {
        dump_header(f);
    }

    fprintf(stdout, "\n");
    fprintf(stdout,
            "// Clean up macros used inside Halide headers\n"
            "#ifndef HALIDE_KEEP_MACROS\n"
            "#undef user_assert\n"
            "#undef user_error\n"
            "#undef user_warning\n"
            "#undef internal_error\n"
            "#undef internal_assert\n"
            "#undef halide_runtime_error\n"
            "#undef debug\n"
            "#undef debug_is_active\n"
            "#endif\n"
            "#endif  // HALIDE_H\n");

    return 0;
}
