#include "HalideRuntime.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
  void *halide_malloc(void *user_context, size_t x) {
    return NULL;
  }
  void halide_free(void *user_context, void *ptr) {
  }
  int halide_device_free(void *user_context, struct buffer_t *buf) {
    return 0;
  }
  void halide_mutex_lock(struct halide_mutex *mutex) {
  }
  void halide_mutex_unlock(struct halide_mutex *mutex) {}
  void halide_mutex_cleanup(struct halide_mutex *mutex_arg) {}
  void halide_error(void *user_context, const char *s) {
    printf("%x %s\n",(unsigned)user_context, s);
  }
}
#endif
