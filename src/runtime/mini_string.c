#include "mini_string.h"

int strncmp(const char* s, const char* t, size_t n) {
    size_t i;
    for (i = 0; i < n && s[i] && (s[i] == t[i]); i++)
        ;
    if (i == n)
        return 0;
    else {
        unsigned char a = s[i], b = t[i];
        return a - b;
    }
}

int strcmp(const char* s, const char* t) {
    size_t i;
    for (i = 0; s[i] && (s[i] == t[i]); i++)
        ;
    unsigned char a = s[i], b = t[i];
    return a - b;
}

size_t strlen(const char* s) {
    size_t i;
    for (i = 0; s[i] != 0; i++)
        ;
    return i;
}

char* strstr(const char* s, const char* t) {
    while (*s) {
        size_t i = 0;
        for (i = 0; s[i] && (s[i] == t[i]); i++)
            ;
        if (!t[i])
            return (char *)s;
        ++s;
    }
    return (char *)0;
}

char *strchr(const char* s, char c) {
    size_t i;
    for (i = 0; s[i]; i++)
        if (s[i] == c)
            return (char*)(s + i);
    return (char*)0;
}

void* memcpy(void* s, const void* t, size_t n) {
    size_t i;
    char* p = (char*)s;
    const char* q = (char*)t;
    for (i = 0; i < n; i++)
        p[i] = q[i];
    return s;
}
