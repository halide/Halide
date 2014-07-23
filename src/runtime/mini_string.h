#ifndef MINI_STRING_H
#define MINI_STRING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcmp(const char* s, const char* t);
int strncmp(const char* s, const char* t, size_t n);
size_t strlen(const char* s);
char *strstr(const char* s, const char* t);
char *strchr(const char* s, char c);

void* memcpy(void* s1, const void* s2, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

#ifdef __cplusplus
}  // extern "C"
#endif


#endif
