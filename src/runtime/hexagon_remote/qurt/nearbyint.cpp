#include <math.h>

extern "C" {

// Hexagon doesn't have an implementation of nearbyint/nearbyintf, so
// we provide one here. This implementation is not great, nearbyint is
// supposed to round to nearest even in the case of a tie.

float nearbyintf(float x) {
    return floorf(x + 0.5f);
}

double nearbyint(double x) {
    return floor(x + 0.5);
}

}  // extern "C"
