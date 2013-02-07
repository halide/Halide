#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, const char **argv) {
    assert(argc == 2 && "Requires target name as an argument (e.g. x86)");
    const char *target = argv[1];
    printf("unsigned char halide_internal_initmod_%s[] = {\n", target);
    int count = 0;
    while (1) {
        int c = getchar();
        if (c == EOF) break;
        printf("%d, ", c);
        count++;
    }
    printf("0};\n");
    printf("int halide_internal_initmod_%s_length = %d;\n", target, count);
    return 0;
}
