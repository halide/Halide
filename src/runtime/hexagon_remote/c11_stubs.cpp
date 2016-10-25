extern "C" {

// Hexagon-tools 8.0.06 and later are dependent on 2 additional symbols:
//    __cxa_finalize and __cxa_atexit
// for backward compatability with hexagon_remote runtimes, we are
// providing weak symbol definitions of the these functions.

#include "HAP_farf.h"
#include "HAP_power.h"

#define FARF_LOW 1    // Enable debug output

void __attribute__ ((weak)) __cxa_finalize() {
  FARF(LOW, "Finalizing\n");
  return;
}

void __attribute__ ((weak)) __cxa_atexit() {
  FARF(LOW, "Atexiting\n");
  return;
}

}  // extern "C"
