#ifndef MINI_STDINT_H
#define MINI_STDINT_H

typedef signed char        int8_t;
typedef short int          int16_t;
typedef int                int32_t;
typedef unsigned char      uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int       uint32_t;

#ifdef BITS_64
typedef long long int           int64_t;
typedef unsigned long long int  uint64_t;
typedef uint64_t size_t;
typedef int64_t intptr_t;
typedef int64_t ptrdiff_t;
#define INT64_C(c)	c ## L
#define UINT64_C(c)	c ## UL
#endif

#ifdef BITS_32
__extension__ typedef long long int          int64_t;
__extension__ typedef unsigned long long int uint64_t;
typedef uint32_t size_t;
typedef int32_t intptr_t;
typedef int32_t ptrdiff_t;
#define INT64_C(c)	c ## LL
#define UINT64_C(c)	c ## ULL
#endif

#ifndef NULL
#define NULL 0
#endif

#define WEAK __attribute__((weak))

#endif
