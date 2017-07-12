#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <assert.h>

std::set<std::string> done;

void dump_header(std::string header) {
    if (done.find(header) != done.end()) return;
    done.insert(header);

    if (header.find("runtime_internal") != std::string::npos) {
        fprintf(stdout, "#error \"COMPILING_HALIDE_RUNTIME should never be defined for Halide.h\"\n");
        return;
    }

    FILE *f = fopen(header.c_str(), "r");

    if (f == NULL) {
      fprintf(stderr, "Could not open header %s.\n", header.c_str());
      exit(1);
    }

    char line[1024];

    while (fgets(line, 1024, f)) {
        if (strncmp(line, "#include \"", 10) == 0) {
            char *sub_header = line + 10;
            for (int i = 0; i < 1014; i++) {
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

int main(int argc, char **headers) {

    // If we're building on visual studio and Halide_SHARED is defined, we'd better
    // also define it for clients so that dllimport gets used.
    #if defined(_MSC_VER) && defined(Halide_SHARED)
    printf("#define Halide_SHARED\n");
    #endif

    for (int i = 1; i < argc; i++) {
        dump_header(headers[i]);
    }


    return 0;
}
