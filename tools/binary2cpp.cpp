
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <fcntl.h> // O_BINARY
#include <io.h> // setmode
#endif

// Embeds a binary blob (from stdin) in a C++ source array of unsigned
// chars. Similar to the xxd utility.

static int usage() {
    fprintf(stderr, "Usage: binary2cpp identifier [-header] [-hidden]\n");
    return -1;
}

void emit_hidden_macro(const char *target) {
    // Note that visibility=hidden isn't supported under MinGW, regardless of compiler
    printf("#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)\n");
    printf("#define HALIDE_BINARY2CPP_%s_HIDDEN_DATA\n", target);
    printf("#else\n");
    printf("#define HALIDE_BINARY2CPP_%s_HIDDEN_DATA __attribute__((visibility(\"hidden\")))\n", target);
    printf("#endif\n");
}

int main(int argc, const char **argv) {
    if (argc < 2 || argc > 4) {
        return usage();
    }

    const char *target = "";
    bool header = false;
    bool hidden = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-header")) header = true;
        else if (!strcmp(argv[i], "-hidden")) hidden = true;
        else if (argv[i][0] == '-') { return usage(); }
        else target = argv[i];
    }
    if (!target[0]) {
        return usage();
    }

    char vis[1024] = "";
    if (hidden) {
        sprintf(vis, "HALIDE_BINARY2CPP_%s_HIDDEN_DATA", target);
    }

    if (header) {
        printf("#ifndef _H_%s_binary2cpp\n", target);
        printf("#define _H_%s_binary2cpp\n", target);
        if (hidden) {
            emit_hidden_macro(target);
        }
        printf("extern \"C\" {\n");
        printf("extern %s unsigned char %s[];\n", vis, target);
        printf("extern %s int %s_length;\n", vis, target);
        printf("}  // extern \"C\"\n");
        printf("#endif  // _H_%s_binary2cpp\n", target);
    } else {
    #ifdef _WIN32
        setmode(fileno(stdin), O_BINARY); // On windows bad things will happen unless we read stdin in binary mode
    #endif
        printf("extern \"C\" {\n");
        if (hidden) {
            emit_hidden_macro(target);
        }
        printf("%s unsigned char %s[] = {\n", vis, target);
        int count = 0;
        int line_break = 0;
        while (1) {
            int c = getchar();
            if (c == EOF) break;
            printf("0x%02x, ", c);
            // Not necessary, but makes a bit easier to read
            if (++line_break > 12) {
                printf("\n");
                line_break = 0;
            }
            count++;
        }
        printf("0};\n");
        printf("%s int %s_length = %d;\n", vis, target, count);
        printf("}  // extern \"C\"\n");
    }
    return 0;
}
