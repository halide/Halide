#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

//---------------------------------------------------------------------------
//#define MEMINFO   1
//
// Define MEMINFO to make default_malloc/free produce memory tracing info:
//
//   default_malloc => [0x9e400, 0xa27ff] # size:17408, 1Kbyte aligned
//   default_header => [0x9e390, 0x9e3ff] # size:112, 16 byte aligned
//   default_malloc => [0xa2880, 0xa6e9f] # size:17952, 128 byte aligned
//   default_header => [0xa2820, 0xa287f] # size:96, 32 byte aligned
//   default_free   => [0x9e390, 0x9e3ff] # size:112, 16 byte aligned
//   default_free   => [0xa2820, 0xa287f] # size:96, 32 byte aligned
//

#ifdef MEMINFO
#define MEMBUFLEN  128

// lightweight string generation routines that can be called from malloc/free
static char *lw_val2str(char *dst, const char *end, intptr_t val, int base = 16) {
    const char *dig2char = "0123456789abcdef";
    int maxdigits = sizeof(intptr_t)*8;
    char numbuf[maxdigits], *numptr = numbuf;
    if ((base < 2) || (base > 16)) {
        base = 16;
    }
    // Collect the digits (least to most significant digit)
    if (base == 16) {
        do { *numptr++ = dig2char[val & 0xf];  val >>= 4;   } while(val);
    } else if (base == 8) {
        do { *numptr++ = dig2char[val & 0x7];  val >>= 3;   } while(val);
    } else if (base == 4) {
        do { *numptr++ = dig2char[val & 0x3];  val >>= 2;   } while(val);
    } else if (base == 2) {
        do { *numptr++ = dig2char[val & 0x1];  val >>= 1;   } while(val);
    } else if (base < 10) {
        do { *numptr++ = (val % base) + '0';   val /= base; } while(val);
    } else {
        do { *numptr++ = dig2char[val % base]; val /= base; } while(val);
    }
    int numdigits = numptr - numbuf;
    --numptr;   // Point to the most significant digit

    // Add a prefix to identify the base
    switch (base) {
        case 16: if (dst < end) *dst++='0'; if (dst < end) *dst++='x'; break;
        case 10: break;
        case 8:  if (dst < end) *dst++='0'; break;
        case 2:  if (dst < end) *dst++='0'; if (dst < end) *dst++='b'; break;
        default:
            if (dst < end) *dst++ = 'B';
            if (dst < end) *dst++ = dig2char[base & 0xf];
            if (dst < end) *dst++ = '_';
            break;
    }

    // Skip leading zeros (all but the least significant digit)
    int i = numdigits;
    while ((i > 1) && (*numptr == '0')) {
        numptr--; i--;
    }
    // Copy the digits to dst
    while (i > 0) {
        if (dst < end) {
            *dst++ = *numptr--; i--;
        } else {
            break;
        }
    }
    if (dst <= end) *dst = '\0';
    return dst;
}

static char *lw_strcpy(char *dst, const char *end, const char *src) {
    while (*src) {
        if (dst < end)
           *dst++ = *src++;
        else
           break;
    }
    if (dst <= end) *dst = '\0';
    return dst;
}

static char *lw_align2str(char *dst, const char *end, intptr_t val) {
    intptr_t align_chk = 1024*1024;
    while (align_chk > 0) {
        if ((val & (align_chk-1)) == 0) {
            char aunit = ' ';
            if (align_chk >= 1024) {
                align_chk >>= 10;
                aunit = 'K';
            }
            if (align_chk >= 1024) {
                align_chk >>= 10;
                aunit = 'M';
            }

            dst = lw_val2str(dst, end, align_chk, 10);
            if (dst < end) *dst++ = aunit;
            dst = lw_strcpy(dst, end, "byte aligned\n");
            break;
        }
        align_chk >>= 1;
    }
    if (dst <= end) *dst = '\0';
    return dst;
}
#endif // MEMINFO
//---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
  WEAK void halide_mutex_lock(struct halide_mutex *mutex) {
  }
  WEAK void halide_mutex_unlock(struct halide_mutex *mutex) {}
  WEAK void halide_mutex_cleanup(struct halide_mutex *mutex_arg) {}
  WEAK void halide_error(void *user_context, const char *s) {
        write(STDERR_FILENO, s, strlen(s));
  }
  WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                             int min, int size, uint8_t *closure) {
    return -1;
  }
  WEAK int halide_device_free(void *user_context, struct buffer_t *buf) {
    return -1;
  }
  WEAK int halide_copy_to_host(void *user_context, struct buffer_t *buf) {
    return 0;
  }

  namespace Halide { namespace Runtime { namespace Internal {
        WEAK void *default_malloc(void *user_context, size_t x) {
          // Halide requires halide_malloc to allocate memory that can be
          // read 8 bytes before the start and 8 bytes beyond the end.
          // Additionally, we also need to align it to the natural vector
          // width.
          void *orig = malloc(x+(128+8));
          if (orig == NULL) {
            // Will result in a failed assertion and a call to halide_error
            return NULL;
          }
          // Round up to next multiple of 128. Should add at least 8 bytes so we
          // can fit the original pointer.
          void *ptr = (void *)((((size_t)orig + 128) >> 7) << 7);
          ((void **)ptr)[-1] = orig;

#ifdef MEMINFO
          char mem_buf[MEMBUFLEN], *dst, *end = &(mem_buf[MEMBUFLEN-1]);
          dst = mem_buf;
          dst = lw_strcpy(dst, end, "default_malloc => [");
          dst = lw_val2str(dst, end, (intptr_t)ptr);
          dst = lw_strcpy(dst, end, ", ");
          dst = lw_val2str(dst, end, (intptr_t)ptr + x-1);
          dst = lw_strcpy(dst, end, "] # size:");
          dst = lw_val2str(dst, end, (intptr_t)x, 10);
          dst = lw_strcpy(dst, end, ", ");
          dst = lw_align2str(dst, end, (intptr_t)ptr);
          halide_print(user_context, mem_buf);
          dst = mem_buf;
          dst = lw_strcpy(dst, end, "default_header => [");
          dst = lw_val2str(dst, end, (intptr_t)orig);
          dst = lw_strcpy(dst, end, ", ");
          dst = lw_val2str(dst, end, (intptr_t)ptr - 1);
          dst = lw_strcpy(dst, end, "] # size:");
          dst = lw_val2str(dst, end, (intptr_t)ptr - (intptr_t)orig, 10);
          dst = lw_strcpy(dst, end, ", ");
          dst = lw_align2str(dst, end, (intptr_t)orig);
          halide_print(user_context, mem_buf);
#endif
          return ptr;
        }

        WEAK void default_free(void *user_context, void *ptr) {
#ifdef MEMINFO
          char mem_buf[MEMBUFLEN], *dst, *end = &(mem_buf[MEMBUFLEN-1]);
          dst = mem_buf;
          dst = lw_strcpy(dst, end, "default_free =>   [");
          dst = lw_val2str(dst, end, (intptr_t)((void**)ptr)[-1]);
          dst = lw_strcpy(dst, end, ", ");
          dst = lw_val2str(dst, end, (intptr_t)ptr - 1);
          dst = lw_strcpy(dst, end, "] # size:");
          dst = lw_val2str(dst, end, (intptr_t)ptr - (intptr_t)((void**)ptr)[-1], 10);
          dst = lw_strcpy(dst, end, ", ");
          dst = lw_align2str(dst, end, (intptr_t)((void**)ptr)[-1]);
          halide_print(user_context, mem_buf);
#endif
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


typedef int32_t (*trace_fn)(void *, const halide_trace_event *);

namespace Halide { namespace Runtime { namespace Internal {

      WEAK void halide_print_impl(void *user_context, const char * str) {
        write(STDERR_FILENO, str, strlen(str));
      }

      WEAK int32_t default_trace(void *user_context, const halide_trace_event *e) {
        stringstream ss(user_context);

        // Round up bits to 8, 16, 32, or 64
        int print_bits = 8;
        while (print_bits < e->bits) print_bits <<= 1;
        halide_assert(user_context, print_bits <= 64 && "Tracing bad type");

        // Otherwise, use halide_printf and a plain-text format
        const char *event_types[] = {"Load",
                                     "Store",
                                     "Begin realization",
                                     "End realization",
                                     "Produce",
                                     "Update",
                                     "Consume",
                                     "End consume"};

        // Only print out the value on stores and loads.
        bool print_value = (e->event < 2);

        ss << event_types[e->event] << " " << e->func << "." << e->value_index << "(";
        if (e->vector_width > 1) {
            ss << "<";
        }
        for (int i = 0; i < e->dimensions; i++) {
            if (i > 0) {
                if ((e->vector_width > 1) && (i % e->vector_width) == 0) {
                    ss << ">, <";
                } else {
                    ss << ", ";
                }
            }
            ss << e->coordinates[i];
        }
        if (e->vector_width > 1) {
            ss << ">)";
        } else {
            ss << ")";
        }

        if (print_value) {
            if (e->vector_width > 1) {
                ss << " = <";
            } else {
                ss << " = ";
            }
            for (int i = 0; i < e->vector_width; i++) {
                if (i > 0) {
                    ss << ", ";
                }
                if (e->type_code == 0) {
                    if (print_bits == 8) {
                        ss << ((int8_t *)(e->value))[i];
                    } else if (print_bits == 16) {
                        ss << ((int16_t *)(e->value))[i];
                    } else if (print_bits == 32) {
                        ss << ((int32_t *)(e->value))[i];
                    } else {
                        ss << ((int64_t *)(e->value))[i];
                    }
                } else if (e->type_code == 1) {
                    if (print_bits == 8) {
                        ss << ((uint8_t *)(e->value))[i];
                    } else if (print_bits == 16) {
                        ss << ((uint16_t *)(e->value))[i];
                    } else if (print_bits == 32) {
                        ss << ((uint32_t *)(e->value))[i];
                    } else {
                        ss << ((uint64_t *)(e->value))[i];
                    }
                } else if (e->type_code == 2) {
                    halide_assert(user_context, print_bits >= 32 && "Tracing a bad type");
                    if (print_bits == 32) {
                        ss << ((float *)(e->value))[i];
                    } else {
                        ss << ((double *)(e->value))[i];
                    }
                } else if (e->type_code == 3) {
                    ss << ((void **)(e->value))[i];
                }
            }
            if (e->vector_width > 1) {
                ss << ">";
            }
        }
        ss << "\n";

        halide_print(user_context, ss.str());
        return 0;
      }
      WEAK trace_fn halide_custom_trace = default_trace;
      WEAK void (*halide_custom_print)(void *, const char *) = halide_print_impl;
    }}} // namespace Halide::Runtime::Internal

  WEAK trace_fn halide_set_custom_trace(trace_fn t) {
    trace_fn result = halide_custom_trace;
    halide_custom_trace = t;
    return result;
  }
  WEAK int32_t halide_trace(void *user_context, const halide_trace_event *e) {
    return (*halide_custom_trace)(user_context, e);
  }
  WEAK int halide_shutdown_trace() {
        return 0;
  }
  WEAK void halide_print(void *user_context, const char *msg) {
    (*halide_custom_print)(user_context, msg);
  }

}
#endif
