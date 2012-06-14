/* Work around a Mac (BSD) "feature" where _environ linker symbol is not defined in dynamic libs -- see man environ. */

#include <stdlib.h>
#include <stdio.h>
#include <crt_externs.h>

char **environ = NULL;

void read_environ() {
  environ = *_NSGetEnviron();
}
