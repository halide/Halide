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

  WEAK int halide_error_out_of_memory(void *user_context) {
    // The error message builder uses malloc, so we can't use it here.
    halide_error(user_context, "Out of memory (halide_malloc returned NULL)");
    return halide_error_code_out_of_memory;
  }

struct spawn_thread_task {
    void (*f)(void *);
    void *closure;
};
WEAK void *halide_spawn_thread_helper(void *arg) {
    spawn_thread_task *t = (spawn_thread_task *)arg;
    t->f(t->closure);
    free(t);
    return NULL;
}

    
WEAK void halide_spawn_thread(void *user_context, void (*f)(void *), void *closure) {
#if 0
    // Note that we don't pass the user_context through to the
    // thread. It may begin well after the user context is no longer a
    // valid thing.
    pthread_t thread;
    // For the same reason we use malloc instead of
    // halide_malloc. Custom malloc/free overrides may well not behave
    // well if run at unexpected times (e.g. the matching free may
    // occur at static destructor time if the thread never returns).
    spawn_thread_task *t = (spawn_thread_task *)malloc(sizeof(spawn_thread_task));
    t->f = f;
    t->closure = closure;
    pthread_create(&thread, NULL, halide_spawn_thread_helper, t);
#else
    halide_error(user_context, "Halide spawn thread called");
#endif
}


}
#endif
