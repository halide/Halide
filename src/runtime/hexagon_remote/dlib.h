#ifndef HALIDE_HEXAGON_MMAP_DLIB_H
#define HALIDE_HEXAGON_MMAP_DLIB_H

// This is a custom implementation of dlopen/dlsym/dlclose for loading
// a shared object in memory, based on using mmap/mprotect to load and
// make data executable. The arguments are the same as their standard
// counterparts, except mmap_dlopen takes a pointer/size instead of a
// file, and does not take a flags option. The exported symbols are
// not actually loaded into the process for use by other
// dlopen/mmap_dlopen calls.
void *mmap_dlopen(const void *code, size_t size);
void *mmap_dlsym(void *dlib, const char *name);
void *mmap_dlsym_libs(const char *name);
int mmap_dlclose(void *dlib);

#endif
