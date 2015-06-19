#include "HalideRuntime.h"
#ifdef __cplusplus
extern "C" {
  WEAK int halide_device_free(void *user_context, struct buffer_t *buf) {
    return 0;
  }
  WEAK void halide_mutex_lock(struct halide_mutex *mutex) {
  }
  WEAK void halide_mutex_unlock(struct halide_mutex *mutex) {}
  WEAK void halide_mutex_cleanup(struct halide_mutex *mutex_arg) {}
  WEAK void halide_error(void *user_context, const char *s) {
  }
  WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                             int min, int size, uint8_t *closure) {
   return 666;
  } 

  namespace Halide { namespace Runtime { namespace Internal {
        WEAK void *default_malloc(void *user_context, size_t x) {
          void *orig = malloc(x+40);
          if (orig == NULL) {
            // Will result in a failed assertion and a call to halide_error
            return NULL;
          }
          // Round up to next multiple of 32. Should add at least 8 bytes so we can fit the original pointer.
          void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
          ((void **)ptr)[-1] = orig;
          return ptr;
        }

        WEAK void default_free(void *user_context, void *ptr) {
          free(((void**)ptr)[-1]);
        }

        WEAK void *(*custom_malloc)(void *, size_t) = default_malloc;
        WEAK void (*custom_free)(void *, void *) = default_free;
      }}}

  WEAK void *(*halide_set_custom_malloc(void *(*user_malloc)(void *, size_t)))(void *, size_t) {
    void *(*result)(void *, size_t) = custom_malloc;
    custom_malloc = user_malloc;
    return result;
  }

  WEAK void (*halide_set_custom_free(void (*user_free)(void *, void *)))(void *, void *) {
    void (*result)(void *, void *) = custom_free;
    custom_free = user_free;
    return result;
  }

  WEAK void *halide_malloc(void *user_context, size_t x) {
    return custom_malloc(user_context, x);
  }

  WEAK void halide_free(void *user_context, void *ptr) {
    custom_free(user_context, ptr);
  }

}
#endif
