#include <stdio.h>
#include <stdlib.h>
#include "X64.h"

int main(int argc, char **argv) {
    const char *regname[] = {"rax", "rcx", "rdx", "rbx",
                             "rsp", "rbp", "rsi", "rdi",
                             "r8", "r9", "r10", "r11",
                             "r12", "r13", "r14", "r15"};
    AsmX64 a;
    const AsmX64::Reg reg[] = {a.rax, a.rcx, a.rdx, a.rbx,
                               a.rsp, a.rbp, a.rsi, a.rdi,
                               a.r8, a.r9, a.r10, a.r11,
                               a.r12, a.r13, a.r14, a.r15};

    FILE *f = fopen("test.s", "w");
    fprintf(f, ".CODE\n");

    srand(157);

    { // add
        // reg + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "add %s, %s\n", regname[i], regname[j]);
                a.add(reg[i], reg[j]);
            }
        }
        
        // reg + mem
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "add %s, [%s]\n", regname[i], regname[j]);
                a.add(reg[i], AsmX64::Mem(reg[j]));
            }
        }
        
        // reg + mem+off
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;            
                if (off > 0) {
                    fprintf(f, "add %s, [%s+%d]\n", regname[i], regname[j], off);
                } else {
                    fprintf(f, "add %s, [%s-%d]\n", regname[i], regname[j], abs(off));
                }
                a.add(reg[i], AsmX64::Mem(reg[j], off));
            }
        }
        
        // mem + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "add [%s], %s\n", regname[i], regname[j]);
                a.add(AsmX64::Mem(reg[i]), reg[j]);
            }
        }
        
        // mem+off + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;
                if (off > 0) {
                    fprintf(f, "add [%s+%d], %s\n", regname[i], off, regname[j]);
                } else {
                    fprintf(f, "add [%s-%d], %s\n", regname[i], abs(off), regname[j]);
                }
                a.add(AsmX64::Mem(reg[i], off), reg[j]);
            }
        }  
        
        // reg + const
        for (int i = 0; i < 16; i++) {
            int off = (int)rand() - RAND_MAX/2;
            if (rand() & 1) off = (rand() & 0xff) - 128;
            fprintf(f, "add %s, %d\n", regname[i], off);
            a.add(reg[i], off);
        }
    }


    { // sub
        // reg + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "sub %s, %s\n", regname[i], regname[j]);
                a.sub(reg[i], reg[j]);
            }
        }
        
        // reg + mem
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "sub %s, [%s]\n", regname[i], regname[j]);
                a.sub(reg[i], AsmX64::Mem(reg[j]));
            }
        }
        
        // reg + mem+off
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;            
                if (off > 0) {
                    fprintf(f, "sub %s, [%s+%d]\n", regname[i], regname[j], off);
                } else {
                    fprintf(f, "sub %s, [%s-%d]\n", regname[i], regname[j], abs(off));
                }
                a.sub(reg[i], AsmX64::Mem(reg[j], off));
            }
        }
        
        // mem + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "sub [%s], %s\n", regname[i], regname[j]);
                a.sub(AsmX64::Mem(reg[i]), reg[j]);
            }
        }
        
        // mem+off + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;
                if (off > 0) {
                    fprintf(f, "sub [%s+%d], %s\n", regname[i], off, regname[j]);
                } else {
                    fprintf(f, "sub [%s-%d], %s\n", regname[i], abs(off), regname[j]);
                }
                a.sub(AsmX64::Mem(reg[i], off), reg[j]);
            }
        }  
        
        // reg + const
        for (int i = 0; i < 16; i++) {
            int off = (int)rand() - RAND_MAX/2;
            if (rand() & 1) off = (rand() & 0xff) - 128;
            fprintf(f, "sub %s, %d\n", regname[i], off);
            a.sub(reg[i], off);
        }
    }



    { // cmp
        // reg + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "cmp %s, %s\n", regname[i], regname[j]);
                a.cmp(reg[i], reg[j]);
            }
        }
        
        // reg + mem
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "cmp %s, [%s]\n", regname[i], regname[j]);
                a.cmp(reg[i], AsmX64::Mem(reg[j]));
            }
        }
        
        // reg + mem+off
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;            
                if (off > 0) {
                    fprintf(f, "cmp %s, [%s+%d]\n", regname[i], regname[j], off);
                } else {
                    fprintf(f, "cmp %s, [%s-%d]\n", regname[i], regname[j], abs(off));
                }
                a.cmp(reg[i], AsmX64::Mem(reg[j], off));
            }
        }
        
        // mem + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "cmp [%s], %s\n", regname[i], regname[j]);
                a.cmp(AsmX64::Mem(reg[i]), reg[j]);
            }
        }
        
        // mem+off + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;
                if (off > 0) {
                    fprintf(f, "cmp [%s+%d], %s\n", regname[i], off, regname[j]);
                } else {
                    fprintf(f, "cmp [%s-%d], %s\n", regname[i], abs(off), regname[j]);
                }
                a.cmp(AsmX64::Mem(reg[i], off), reg[j]);
            }
        }  
        
        // reg + const
        for (int i = 0; i < 16; i++) {
            int off = (int)rand() - RAND_MAX/2;
            if (rand() & 1) off = (rand() & 0xff) - 128;
            fprintf(f, "cmp %s, %d\n", regname[i], off);
            a.cmp(reg[i], off);
        }
    }

    { // call and ret
        for (int i = 0; i < 16; i++) {
            fprintf(f, "call %s\n", regname[i]);
            a.call(reg[i]);

            fprintf(f, "call qword ptr [%s]\n", regname[i]);
            a.call(AsmX64::Mem(reg[i]));

            int off = (int)rand() - RAND_MAX/2;
            if (off > 0) {
                fprintf(f, "call qword ptr [%s+%d]\n", regname[i], off);
            } else {
                fprintf(f, "call qword ptr [%s-%d]\n", regname[i], -off);
            }
            a.call(AsmX64::Mem(reg[i], off));

            off = (rand() & 0xff) - 128;            
            if (off > 0) {
                fprintf(f, "call qword ptr [%s+%d]\n", regname[i], off);
            } else {
                fprintf(f, "call qword ptr [%s-%d]\n", regname[i], -off);
            }
            a.call(AsmX64::Mem(reg[i], off));
        }
        fprintf(f, "ret\n");
        a.ret();
    }

    { // mov
        
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "mov %s, %s\n", regname[i], regname[j]);
                a.mov(reg[i], reg[j]);
            }
        }
        /*
        // reg + mem
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "mov %s, [%s]\n", regname[i], regname[j]);
                a.mov(reg[i], AsmX64::Mem(reg[j]));
            }
        }
        
        // reg + mem+off
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;            
                if (off > 0) {
                    fprintf(f, "mov %s, [%s+%d]\n", regname[i], regname[j], off);
                } else {
                    fprintf(f, "mov %s, [%s-%d]\n", regname[i], regname[j], abs(off));
                }
                a.mov(reg[i], AsmX64::Mem(reg[j], off));
            }
        }
        
        // mem + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(f, "mov [%s], %s\n", regname[i], regname[j]);
                a.mov(AsmX64::Mem(reg[i]), reg[j]);
            }
        }
        
        // mem+off + reg
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int off = (int)rand() - RAND_MAX/2;
                if (off > 0) {
                    fprintf(f, "mov [%s+%d], %s\n", regname[i], off, regname[j]);
                } else {
                    fprintf(f, "mov [%s-%d], %s\n", regname[i], abs(off), regname[j]);
                }
                a.mov(AsmX64::Mem(reg[i], off), reg[j]);
            }
        }  
        
        // reg + const
        for (int i = 0; i < 16; i++) {
            int off = (int)rand() - RAND_MAX/2;
            if (rand() & 1) off = (rand() & 0xff) - 128;
            fprintf(f, "mov %s, %d\n", regname[i], off);
            a.mov(reg[i], off);
        }
        */
    }

    fprintf(f, "END\n");
    fclose(f);


    // write a coff file to disassemble

    f = fopen("generated.obj", "w");
    unsigned short coffHeader[10] = {0x8664,  // machine
                                     1,     // sections
                                     0, 0,  // date stamp
                                     20, 0, // pointer to symbol table
                                     0, 0,  // entries in symbol table
                                     0,     // optional header size
                                     0};    // characteristics
    
    unsigned char sectionName[8] = {'.', 't', 'e', 'x', 't', 0, 0, 0};
    
    unsigned int sectionHeader[8] = {0, // physical address
                                     0, // virtual address
                                     a.buffer().size(), // size of data
                                     10*2 + 8 + 8*4, // pointer to raw data
                                     0, // relocation table
                                     0, // line numbers
                                     0, // relocation entries and line number entries
                                     0x60500020}; // flags

    fwrite(coffHeader, 2, 10, f);
    fwrite(sectionName, 1, 8, f);
    fwrite(sectionHeader, 4, 8, f);
    fwrite(&a.buffer()[0], 1, a.buffer().size(), f);
    fclose(f);

    // now compile test.s, and compare it with generated.obj using a disassembler

    // now actually generate some functions and call them on the fly
    a.buffer().clear();
    // move the first argument to the output
    a.mov(a.rax, a.rcx);
    // add one 
    //a.add(a.rax, 1);
    a.ret();

    // cast the buffer to a function pointer
    long long (*func)(long long) = (long long (*)(long long))(&a.buffer()[0]);
    printf("%lld == 17\n", func(16));
    fflush(stdout);
    return 0;
}
