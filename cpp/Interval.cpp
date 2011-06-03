#include "base.h"
#include "Interval.h"

Interval abs(const Interval &x) {
    if (!x.bounded()) return Interval();
    else if (x.min() < 0 && x.max() > 0) {
        int64_t up = x.max() > -x.min() ? x.max() : -x.min();
        return Interval(0, up);
    } else if (x.min() > 0) {
        return x;
    } else {
        return -x;
    }
}

SteppedInterval abs(const SteppedInterval &x) {
    if (!x.bounded()) {
        return SteppedInterval();
    } else if (x.min() < 0 && x.max() > 0) {
        int64_t up = x.max() > -x.min() ? x.max() : -x.min();
        // it's rare that a reflecting abs would preserve any useful
        // modulus/remainder info. A special case we catch here is
        // when rem == 0
        if (x.remainder() == 0) {
            return SteppedInterval(0, up, 0, x.modulus());
        } else {
            return SteppedInterval(0, up, 0, 1);
        }
    } else if (x.min() > 0) {        
        return x;
    } else {
        return -x;
    }
}
