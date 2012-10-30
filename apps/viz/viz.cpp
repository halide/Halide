#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

typedef enum {Load, Store, Compute, Realize} event_type;

struct event {
    int location[4];
    int size[4];
    const std::string *name;
    event_type type;
};

int main(int argc, char **argv) {

    // Parse an event log
    FILE *f = fopen(argv[1], "r");
    
    std::vector<event> log;    

    event e;
    std::vector<std::string> names;

    char buf[1024];
    while (fgets(buf, 1023, f)) {
        if (strncmp(buf, "Loading", 7) == 0) {
            printf("Load\n");            
            
        } else if (strncmp(buf, "Storing", 7) == 0) {
            printf("Store\n");      
        } else if (strncmp(buf, "Realizing", 9) == 0) {
            printf("Realize\n");            
        } else if (strncmp(buf, "Computing", 9) == 0) {
            printf("Compute\n");            
        } else {
            printf("Other\n");
        }
    }

    fclose(f);
}
