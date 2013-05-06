#include <stdio.h>
#include "mlval.h"

ML_FUNC0(makeFoo1);
ML_FUNC1(makeFoo2);
ML_FUNC1(makeFoo3);
ML_FUNC2(makeFoo4);
ML_FUNC1(eatFoo);

int main(int argc, char **argv) {
    
    eatFoo(makeFoo1());
    eatFoo(makeFoo2(1));
    eatFoo(makeFoo3("Hi!"));
    eatFoo(makeFoo4(17, 18));
    
    return 0;
}
