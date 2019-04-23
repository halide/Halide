#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Usage: augment_sample sample.bin runtime pipeline_id schedule_id\n");
        return -1;
    }

    FILE *f = fopen(argv[1], "ab");
    if (!f) {
      fprintf(stderr, "Unable to open file: %s\n", argv[1]);
      return -1;
    }

    // Input runtime value is presumed to be in seconds,
    // but sample file stores times in milliseconds.
    float r = atof(argv[2]) * 1000.f;
    int pid = atoi(argv[3]);
    int sid = atoi(argv[4]);

    fwrite(&r, 4, 1, f);
    fwrite(&pid, 4, 1, f);
    fwrite(&sid, 4, 1, f);

    fclose(f);

    return 0;

}
