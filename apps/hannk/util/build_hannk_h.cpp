#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

std::set<std::string> done;

std::string real_path(const std::string &raw_path) {
    char fullpath_buffer[PATH_MAX];
    if (!realpath(raw_path.c_str(), fullpath_buffer)) {
        fullpath_buffer[0] = 0;
    }
    return std::string(fullpath_buffer);
}

void dump_header(const std::string &base_dir, const std::string &header) {
    static const std::set<std::string> passthru = {
        "HalideBuffer.h",
        "HalideRuntime.h",
    };

    if (passthru.count(header)) {
        fprintf(stdout, "#include \"%s\"\n", header.c_str());
        return;
    }

    std::string full_path = real_path(base_dir + '/' + header);
    if (full_path.empty()) {
        fprintf(stderr, "Could not open header %s + %s.\n", base_dir.c_str(), header.c_str());
        exit(1);
    }

    if (done.find(full_path) != done.end()) {
        return;
    }
    done.insert(full_path);

    FILE *f = fopen(full_path.c_str(), "r");

    if (f == nullptr) {
        fprintf(stderr, "Could not open header %s.\n", full_path.c_str());
        exit(1);
    }

    char line[1024];
    constexpr int line_len = sizeof(line);
    constexpr char include_str[] = "#include \"";
    constexpr int include_str_len = sizeof(include_str) - 1;  // remove null terminator

    while (fgets(line, line_len, f)) {
        if (strncmp(line, include_str, include_str_len) == 0) {
            char *sub_header = line + include_str_len;
            for (int i = 0; i < line_len - include_str_len; i++) {
                if (sub_header[i] == '"') {
                    sub_header[i] = 0;
                }
            }
            dump_header(base_dir, sub_header);
        } else {
            fputs(line, stdout);
        }
    }

    fclose(f);
}

int main(int argc, char **files) {

    if (argc < 4) {
        fprintf(stderr, "Usage: %s LICENSE.txt basedir [headers...]\n", files[0]);
        exit(1);
    }

    fprintf(stdout, "/* hannk.h -- interface for libHannk.\n\n");

    {
        FILE *f = fopen(files[1], "r");
        if (f == nullptr) {
            fprintf(stderr, "Could not open %s.\n", files[1]);
            exit(1);
        }

        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            fprintf(stdout, "   %s", line);
        }

        fclose(f);
    }

    fprintf(stdout, "\n*/\n\n");
    fprintf(stdout, "#ifndef HANNK_H\n");
    fprintf(stdout, "#define HANNK_H\n\n");

    std::string basedir = files[2];
    for (int i = 3; i < argc; i++) {
        dump_header(basedir, files[i]);
    }

    fprintf(stdout, "\n");
    fprintf(stdout, "#endif  // HANNK_H\n");

    return 0;
}
