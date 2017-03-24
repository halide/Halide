#ifndef HALIDE_HEXAGON_MMAP_DLIB_H
#define HALIDE_HEXAGON_MMAP_DLIB_H

void *mmap_dlopen(const void *code, size_t size);
void *mmap_dlsym(void *dlib, const char *name);
int mmap_dlclose(void *dlib);

#endif
