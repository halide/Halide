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
            int off = (int)rand();
            fprintf(f, "add %s, [%s+%08xh]\n", regname[i], regname[j], off);
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
            int off = (int)rand();
            fprintf(f, "add [%s+%08xh], %s\n", regname[i], off, regname[j]);
            a.add(AsmX64::Mem(reg[i], off), reg[j]);
        }
    }  
    

    // reg + const
    for (int i = 0; i < 16; i++) {
        int off = (int)rand();
        fprintf(f, "add %s, %08xh\n", regname[i], off);
        a.add(reg[i], off);
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

    return 0;
}
