#include <assert.h>
#include <cstdlib>
#include <set>
#include <stdio.h>
#include <string.h>
#include <string>

std::set<std::string> done;

void dump_header(std::string header) {
    if (done.find(header) != done.end()) return;
    done.insert(header);

    if (header.find("runtime_internal") != std::string::npos) {
        fprintf(stdout, "#error \"COMPILING_HALIDE_RUNTIME should never be defined for Halide.h\"\n");
        return;
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
                if (sub_header[i] == '"') sub_header[i] = 0;
            }
            size_t slash_pos = header.rfind('/');
            std::string path;
            if (slash_pos != std::string::npos)
                path = header.substr(0, slash_pos + 1);
            dump_header(path + sub_header);
        } else {
            fputs(line, stdout);
        }
    }

    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [--internal] LICENSE.txt [headers...]\n", argv[0]);
        exit(1);
    }

    char** argv_end = argv + argc;

    argv++;
    bool internal = false;
    if (strcmp(*argv, "--internal") == 0) {
        internal = true;
        argv++;
    }

    if (internal) {
        // For internal use only, skip the License stuff
        argv++;
        fprintf(stdout, "#ifndef HALIDE_INTERNAL_H\n");
        fprintf(stdout, "#define HALIDE_INTERNAL_H\n\n");
    } else {
        fprintf(stdout, "/* Halide.h -- interface for the 'Halide' library.\n\n");
        {
            FILE *f = fopen(*argv, "r");
            if (f == nullptr) {
                fprintf(stderr, "Could not open %s.\n", *argv);
                exit(1);
            }

            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                fprintf(stdout, "   %s", line);
            }

            fclose(f);
        }
        fprintf(stdout, "\n*/\n\n");
        fprintf(stdout, "#ifndef HALIDE_H\n");
        fprintf(stdout, "#define HALIDE_H\n\n");
    }

    while (argv < argv_end) {
        dump_header(*argv++);
    }

    fprintf(stdout, "\n");

    if (internal) {
        fprintf(stdout,
                "#endif  // HALIDE_INTERNAL_H\n");
    } else {
        fprintf(stdout,
                "// Clean up macros used inside Halide headers\n"
                "#undef user_assert\n"
                "#undef user_error\n"
                "#undef user_warning\n"
                "#undef internal_error\n"
                "#undef internal_assert\n"
                "#undef halide_runtime_error\n"
                "#undef HALIDE_EXPORT\n\n"
                "#endif  // HALIDE_H\n");
    }

    return 0;
}
