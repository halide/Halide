
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>  // O_BINARY
#include <io.h>     // setmode
#endif

// Embeds a binary blob (from stdin) in a C++ source array of unsigned
// chars. Similar to the xxd utility.

static int usage() {
    fprintf(stderr, "Usage: binary2cpp identifier [-header]\n");
    return -1;
}

int main(int argc, const char **argv) {
    const char *target = argv[1];
    if (argc == 3) {
        if (!strcmp(argv[2], "-header")) {
            printf("#ifndef _H_%s_binary2cpp\n", target);
            printf("#define _H_%s_binary2cpp\n", target);
            printf("extern \"C\" {\n");
            printf("extern unsigned char %s[];\n", target);
            printf("extern int %s_length;\n", target);
            printf("}  // extern \"C\"\n");
            printf("#endif  // _H_%s_binary2cpp\n", target);
            return 0;
        } else {
            return usage();
        }
    } else if (argc > 3) {
        return usage();
    }

#ifdef _WIN32
    setmode(fileno(stdin), O_BINARY);  // On windows bad things will happen unless we read stdin in binary mode
#endif
    printf("extern \"C\" {\n");
    printf("unsigned char %s[] = {\n", target);
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
    printf("int %s_length = %d;\n", target, count);
    printf("}  // extern \"C\"\n");
    return 0;
}
