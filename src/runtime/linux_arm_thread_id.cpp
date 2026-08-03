#ifdef BITS_64
#define SYS_GETTID 178
#else
#define SYS_GETTID 224
#endif

#include "linux_thread_id_common.h"
