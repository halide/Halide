#ifndef HALIDE_MALLOC_TRACE_H
#define HALIDE_MALLOC_TRACE_H

//---------------------------------------------------------------------------
// The custom trace allocator can be used in an application by calling:
//
//   halide_enable_malloc_trace();
//
// The trace is off by default, compile with the following to turn it on:
//
//   -DHL_MEMINFO  Produce memory tracing in the custom malloc trace allocator
//
//   halide_malloc => [0x9e400, 0xa27ff], # size:17408, align:1K
//   halide-header => [0x9e390, 0x9e3ff], # size:112, align:16
//   halide_malloc => [0xa2880, 0xa6e9f], # size:17952, align:128
//   halide-header => [0xa2820, 0xa287f], # size:96, align:32
//   halide_free   => [0x9e390, 0x9e3ff], # size:112, align:16
//   halide_free   => [0xa2820, 0xa287f], # size:96, align:32
//
//---------------------------------------------------------------------------
#define HL_MEMBUFLEN  128

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

}

namespace Halide { namespace Tools {

#ifdef HL_MEMINFO
//---------------------------------------------------------------------------
// Lightweight string generation routines that don't perform any additional
// heap allocation so that they can be called from halide_malloc/free

// lw_strcpy
//
// String copy src to the dst string (not going past the end ptr of dst)
//
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

// lw_endl
//
// Guarantee the dst string ends with a newline and NULL terminator even
// if it means overwriting the last characters present at the end of dst
//
static char *lw_endl(char *dst, char *end) {
    if (dst < end) {
        *dst++ = '\n';
        *dst   = '\0';
    } else {
        *(end-1) = '\n';
        *end     = '\0';
    }
    return dst;
}

// lw_val2str
//
// Generate the text representation of val for a given base (2 - 16) in the
// dst string (not going past the end ptr of dst)
//
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

// lw_memalign2str
//
// Describe the memory alignment of the val ptr value in the dst string
// (not going past the end ptr of dst)
//
static char *lw_memalign2str(char *dst, const char *end, intptr_t val) {
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

            dst = lw_strcpy(dst, end, "align:");
            dst = lw_val2str(dst, end, align_chk, 10);
            if (aunit != ' ') {
                if (dst < end) *dst++ = aunit;
            }
            break;
        }
        align_chk >>= 1;
    }
    if (dst <= end) *dst = '\0';
    return dst;
}
//---------------------------------------------------------------------------
#endif // lw_* HL_MEMINFO

void *halide_malloc_trace(void *user_context, size_t x) {
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

#ifdef HL_MEMINFO
    char mem_buf[HL_MEMBUFLEN], *dst, *end = &(mem_buf[HL_MEMBUFLEN-1]);
    void *headend = (orig == ptr) ? orig : (char *)ptr - 1;
    dst = mem_buf;
    dst = lw_strcpy(dst, end, "halide_malloc => [");
    dst = lw_val2str(dst, end, (intptr_t)ptr);
    dst = lw_strcpy(dst, end, ", ");
    dst = lw_val2str(dst, end, (intptr_t)ptr + x-1);
    dst = lw_strcpy(dst, end, "], # size:");
    dst = lw_val2str(dst, end, (intptr_t)x, 10);
    dst = lw_strcpy(dst, end, ", ");
    dst = lw_memalign2str(dst, end, (intptr_t)ptr);
    dst = lw_endl(dst, end);
    halide_print(user_context, mem_buf);
    dst = mem_buf;
    dst = lw_strcpy(dst, end, "halide-header => [");
    dst = lw_val2str(dst, end, (intptr_t)orig);
    dst = lw_strcpy(dst, end, ", ");
    dst = lw_val2str(dst, end, (intptr_t)headend);
    dst = lw_strcpy(dst, end, "], # size:");
    dst = lw_val2str(dst, end, (intptr_t)ptr - (intptr_t)orig, 10);
    dst = lw_strcpy(dst, end, ", ");
    dst = lw_memalign2str(dst, end, (intptr_t)orig);
    dst = lw_endl(dst, end);
    halide_print(user_context, mem_buf);
#endif
    return ptr;
}

void halide_free_trace(void *user_context, void *ptr) {
#ifdef HL_MEMINFO
    char mem_buf[HL_MEMBUFLEN], *dst, *end = &(mem_buf[HL_MEMBUFLEN-1]);
    dst = mem_buf;
    dst = lw_strcpy(dst, end, "halide_free =>   [");
    dst = lw_val2str(dst, end, (intptr_t)((void**)ptr)[-1]);
    dst = lw_strcpy(dst, end, ", ");
    dst = lw_val2str(dst, end, (intptr_t)ptr - 1);
    dst = lw_strcpy(dst, end, "], # size:");
    dst = lw_val2str(dst, end, (intptr_t)ptr - (intptr_t)((void**)ptr)[-1], 10);
    dst = lw_strcpy(dst, end, ", ");
    dst = lw_memalign2str(dst, end, (intptr_t)((void**)ptr)[-1]);
    dst = lw_endl(dst, end);
    halide_print(user_context, mem_buf);
#endif
    free(((void**)ptr)[-1]);
}

void halide_enable_malloc_trace(void) {
    halide_set_custom_malloc(halide_malloc_trace);
    halide_set_custom_free(halide_free_trace);
}

}} // namespace Halide::Tools

#endif // HALIDE_MALLOC_TRACE_H
