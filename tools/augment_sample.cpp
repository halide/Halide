#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Usage: record_runtime sample.bin runtime pipeline_id schedule_id\n");
        return -1;
    }

    FILE *f = fopen(argv[1], "ab");

    float r = atof(argv[2]) * 1000;
    int pid = atoi(argv[3]);
    int sid = atoi(argv[4]);

    fwrite(&r, 4, 1, f);
    fwrite(&pid, 4, 1, f);
    fwrite(&sid, 4, 1, f);

    fclose(f);

    return 0;

}
