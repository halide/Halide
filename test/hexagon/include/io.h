#include "q6sim_timer.h"
#if defined(__hexagon__)
#define RESET_PMU()     __asm__ __volatile__ (" r0 = #0x48 ; trap0(#0); \n" : : : "r0","r1","r2","r3","r4\
","r5","r6","r7","memory")
#define DUMP_PMU()      __asm__ __volatile__ (" r0 = #0x4a ; trap0(#0); \n" : : : "r0","r1","r2","r3","r4\
","r5","r6","r7","memory")
#define READ_PCYCLES    q6sim_read_pcycles

#else

#define RESET_PMU()
#define DUMP_PMU()
#define READ_PCYCLES()  0
#endif
