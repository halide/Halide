#ifdef HL_HEXAGON_DEVICE
//#define IMG_SIZE        (256*1024)
#else
//#define IMG_SIZE        250
#endif

// Overall size of histogram (max pixel bits)
#ifndef HIST_SIZE_BITS
#define HIST_SIZE_BITS 10
#endif
#define HIST_SIZE       (1<<HIST_SIZE_BITS)

// Depth of one histogram bucket
#ifndef HIST_TYPE_BITS
#define HIST_TYPE_BITS 32
#endif

#if (HIST_TYPE_BITS <= 8)
#define HIST_TYPE       uint8_t
#elif (HIST_TYPE_BITS <= 16)
#define HIST_TYPE       uint16_t
#else
#define HIST_TYPE       uint32_t
#endif

