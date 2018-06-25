#ifndef TBL_SIZE
#define TBL_SIZE 1024
#endif

#if (TBL_SIZE <= 256)
#define DTYPE    uint8_t
#elif (TBL_SIZE <= 65536)
#define DTYPE    uint16_t
#else
#define DTYPE    uint32_t
#endif
