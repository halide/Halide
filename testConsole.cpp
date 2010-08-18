#include <stdio.h>
#include <windows.h> // for VirtualProtect
#include "X64.h"

int main(int argc, char **argv) {
    
    // Generate some X64 machine code representing a function that returns the argument plus one
    AsmX64 a;
    a.mov(a.rax, a.rcx); 
    a.sub(a.rdx, a.rdx);
    a.add(a.rdx, 10);
    a.label("loop");

    a.add(a.rax, a.rax);

    a.sub(a.rdx, 1);
    a.jne("loop");
    a.ret();

    for (int i = 0; i < a.buffer().size(); i++) {
        printf("%02x ", a.buffer()[i]);
    }
    printf("\n");

    // Convince windows that the buffer is safe to execute (normally
    // it refuses to do so for security reasons)
    DWORD out;
    VirtualProtect(&(a.buffer()[0]), a.buffer().size(), PAGE_EXECUTE_READWRITE, &out);

    // Cast the buffer to a function pointer of the appropriate type
    int64_t (*func)(int64_t) = (int64_t (*)(int64_t))(&(a.buffer()[0]));
    
    // Call the function
    printf("This should be 1024: %d\n", func(1));

    return 0;
}
