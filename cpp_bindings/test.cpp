#include "FImage.h"

using namespace FImage;

void pgm_static(void *args) {
    unsigned *arg = ((unsigned **)args)[0];
    arg[0] += 17;
}

int main(int argc, char **argv) {
    Var x;

    unsigned int memory[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    void *buf1 = &(memory[0]);

    void *args = &buf1;

    Expr pgm = Load(1, 0);
    for (int i = 1; i < 10; i++) {
        pgm = pgm + Load(1, i);
    }
    for (int i = 1; i < 10; i++) {
        pgm = pgm + Load(1, i);
    }
    pgm = Store(pgm, 1, 0);

    printf("Running...\n");

    printf("memory before = %u\n", memory[0]);

    run(pgm, args);

    printf("memory after = %u\n", memory[0]);

    printf("Running again...\n");

    run(pgm, args);

    printf("memory after = %u\n", memory[0]);

    printf("Running it a bunch of times\n");
    printf("memory before = %u\n", memory[0]);    
    for (int i = 0; i < 10000000; i++) {
        run(pgm, args);
        //pgm_static(args);
    } 
    printf("memory after = %u\n", memory[0]);    

    return 0;
}
