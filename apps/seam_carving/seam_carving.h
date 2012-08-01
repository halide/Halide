#ifndef seam_carving_h
#define seam_carving_h

#ifndef buffer_t_defined
#define buffer_t_defined
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct buffer_t {
  uint8_t* host;
  uint64_t dev;
  bool host_dirty;
  bool dev_dirty;
  size_t dims[4];
  size_t elem_size;
} buffer_t;
#endif

void seam_carving(buffer_t *m0, buffer_t *result);

#ifdef __cplusplus
}
#endif

#endif