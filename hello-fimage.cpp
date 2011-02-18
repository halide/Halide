#include "FImage.h"

int main(int argc, char **argv) {
    
    FImage im(2);
    Var x(0, im.size[0]);
    im(x) = x; ///im.size[0];
    im.evaluate();
    
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
