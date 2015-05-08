#include "HalideRuntime.h"
#ifdef __cplusplus
extern "C" {
  WEAK void *halide_malloc(void *user_context, size_t x) {
    return NULL;
  }
  WEAK void halide_free(void *user_context, void *ptr) {
  }
  WEAK int halide_device_free(void *user_context, struct buffer_t *buf) {
    return 0;
  }
  WEAK void halide_mutex_lock(struct halide_mutex *mutex) {
  }
  WEAK void halide_mutex_unlock(struct halide_mutex *mutex) {}
  WEAK void halide_mutex_cleanup(struct halide_mutex *mutex_arg) {}
  WEAK void halide_error(void *user_context, const char *s) {
  }
}
#endif
