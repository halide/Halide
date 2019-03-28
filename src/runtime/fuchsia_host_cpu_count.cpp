#include "HalideRuntime.h"

extern "C" {

uint32_t zx_system_get_num_cpus(void);

WEAK int halide_host_cpu_count() {
  return (int)zx_system_get_num_cpus();
}

}
