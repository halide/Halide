#include "FImage.h"

int main(int argc, char **argv) {
    
    int integers[] = { 1, 2, 3, 4 };
    
    FImage im(4,4);
    Var x(0, im.size[0]);
    Var y(0, im.size[1]);
    //x.unroll(4);
    //x.vectorize(4);
    printf("In:\n");
    for (int j = 0; j < im.size[1]; j++) {
        for (int i = 0; i < im.size[0]; i++) {
            if (j % 2) im(i,j) = -integers[i % (sizeof(integers)/sizeof(integers[0]))];
            else im(i,j) = integers[i % (sizeof(integers)/sizeof(integers[0]))];
            printf("%f ", im(i,j));
        }
        printf("\n");
    }
    im(x,y) = im(x,y) * 2;
    im.evaluate();
    
    printf("Out:\n");
    for (int j = 0; j < im.size[1]; j++) {
        for (int i = 0; i < im.size[0]; i++) {
            printf("%f ", im(i,j));
        }
        printf("\n");
    }
    
    // Slightly more complex example
    #if 0
    FImage im(4, 4, 3);
    Var x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    im(x,y,c) = 0.5f*x;
    im.evaluate();
    
    for (int i = 0; i < im.size[0]; i++) {
        for (int j = 0; j < im.size[1]; j++) {
            for (int k = 0; k < im.size[2]; k++) {
                printf("im(%d,%d,%d) = %f\n", i, j, k, im(i,j,k));
            }
        }
    }
    #endif
    
    return 0;
}
