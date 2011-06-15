#include <stdio.h>
#include <stdint.h>

#define CHAR_BIT 8
#define FMT_BUF_SIZE (CHAR_BIT*sizeof(uintmax_t)+1)
static char *binary_fmt(uintmax_t x, char buf[static FMT_BUF_SIZE])
{
    char *s = buf + FMT_BUF_SIZE;
    *--s = 0;
    if (!x) *--s = '0';
    for(; x; x/=2) *--s = '0' + x%2;
    return s;
}

/*
 * Data_bigarray_val(val) = ((void**)val)[1] -- dereference base ptr + 1, for
 * base of `data` field.
 */

void ptr_test(void** val) {
    unsigned char* arr = (unsigned char*)val[1];
    char tmp[FMT_BUF_SIZE];
    for (size_t c = 0; c < sizeof(int)*10; c++) {
        printf("%s ", binary_fmt(arr[c], tmp));
        //printf("0x%x ", (unsigned)arr[c]);
    }
}
