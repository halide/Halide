#include "base.h"
#include "Interval.h"

int main() {
    printf("a = 6x + 3 from -100 to 100\n"
           "b = 10x + 5 from -100 to 100\n");
    SteppedInterval a(-100, 100, 3, 6);
    SteppedInterval b(-100, 100, 5, 10);

    SteppedInterval c = a*b;
    printf("a*b = %ldx + %ld from %ld to %ld\n",
           c.modulus(), c.remainder(), c.min(), c.max());

    c = a+b;
    printf("a+b = %ldx + %ld from %ld to %ld\n",
           c.modulus(), c.remainder(), c.min(), c.max());

    c = a-b;
    printf("a-b = %ldx + %ld from %ld to %ld\n",
           c.modulus(), c.remainder(), c.min(), c.max());

    c = a*4;
    printf("a*4 = %ldx + %ld from %ld to %ld\n",
           c.modulus(), c.remainder(), c.min(), c.max());

    c = b*4;
    printf("b*4 = %ldx + %ld from %ld to %ld\n",
           c.modulus(), c.remainder(), c.min(), c.max());

    return 0;


}
