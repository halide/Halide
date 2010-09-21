#include <stdio.h>
#include "X64.h"

int main(int argc, char **argv) {
    
    // Generate some X64 machine code representing a function that
    // loops from 10->0 multiplying the argument by 2 (by adding it
    // into itself) -- computing arg*2^10.
    AsmX64 a;
#ifdef _MSC_VER
    a.mov(a.rax, a.rcx); // Microsoft x86-64 calling convention
#else //!_MSC_VER
    a.mov(a.rax, a.rdi); // AMD64 calling convention for non-Windows platforms
#endif
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

    // Convince the OS that the buffer is safe to execute (normally
    // it refuses to do so for security reasons)
    AsmX64::makePagesExecutable((uintptr_t)&(a.buffer()[0]), a.buffer().size());

    // Cast the buffer to a function pointer of the appropriate type
    int64_t (*func)(int64_t) = (int64_t (*)(int64_t))(&(a.buffer()[0]));
    
    // Call the function
    printf("This should be 1024: %d\n", func(1));

    return 0;
}
