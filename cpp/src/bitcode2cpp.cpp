#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, const char **argv) {
    assert(argc == 2 && "Requires target name as an argument (e.g. x86)");
    const char *target = argv[1];
    printf("namespace Halide { namespace Internal {");
    printf("unsigned char builtins_bitcode_%s[] = {\n", target);
    int count = 0;
    while (1) {
        int c = getchar();
        if (c == EOF) break;
        printf("%d, ", c);
        count++;
    }
    printf("0};\n");
    printf("int builtins_bitcode_%s_length = %d;\n", target, count);
    printf("}}\n");
    return 0;
}
