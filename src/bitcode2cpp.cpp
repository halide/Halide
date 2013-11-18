#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#ifdef _WIN32
#include <fcntl.h> // O_BINARY
#include <io.h> // setmode
#endif


int main(int argc, const char **argv) {
    assert(argc == 2 && "Requires target name as an argument (e.g. x86_64)");
#ifdef _WIN32
    setmode(fileno(stdin), O_BINARY); // On windows bad things will happen unless we read stdin in binary mode
#endif
    const char *target = argv[1];
    printf("extern \"C\" {\n");
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
    printf("}\n"); // extern "C"
    return 0;
}
