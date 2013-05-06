/* Work around a Mac (BSD) "feature" where _environ linker symbol is not defined in dynamic libs -- see man environ. */

#include <stdlib.h>
#include <stdio.h>

#ifdef __linux__
#include <unistd.h>
void read_environ() {
}
#else
#include <crt_externs.h>

char **environ = NULL;

void read_environ() {
  environ = *_NSGetEnviron();
}
#endif
