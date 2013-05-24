
#define WEAK __attribute__((weak))

extern "C" {

extern int halide_printf(const char *, ...);
extern void exit(int);

#ifndef NULL
#define NULL 0
#endif

WEAK void (*halide_error_handler)(char *) = NULL;

WEAK void halide_error(char *msg) {
    if (halide_error_handler) (*halide_error_handler)(msg);
    else {        
        halide_printf("Error: %s\n", msg);
        exit(1);
    }
}

WEAK void halide_set_error_handler(void (*handler)(char *)) {
    halide_error_handler = handler;
}

}
