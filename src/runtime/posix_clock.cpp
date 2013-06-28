#include <stdint.h>
#include <ctime>

extern "C" {

WEAK clock_t halide_reference_clock;

WEAK int halide_start_clock() {
	halide_reference_clock = clock();
    return 0;
}

WEAK int halide_current_time() {
	clock_t now = clock();
	return (now - halide_reference_clock) * 1000 / CLOCKS_PER_SEC;
}

}
