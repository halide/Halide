#include <math.h>

extern "C" {

float nearbyintf(float x) {
    return floorf(x + 0.5f);
}

double nearbyint(double x) {
    return floor(x + 0.5);
}

}  // extern "C"
