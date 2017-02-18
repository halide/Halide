#ifndef HALIDE_HEXAGON_ELF_H
#define HALIDE_HEXAGON_ELF_H

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
}

#include "log.h"

// ELF comes in 32 and 64-bit variants. Define ELF64 to use the 64-bit
// variant.
// #define ELF64

#ifdef ELF64
typedef uint64_t elfaddr_t;
#else
typedef uint32_t elfaddr_t;
#endif

static const int alignment = 4096;

// The standard ELF header. See
// http://man7.org/linux/man-pages/man5/elf.5.html for the meanings of
// these fields.
struct elf_header_t {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    elfaddr_t     e_entry;
    elfaddr_t     e_phoff;
    elfaddr_t     e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
};

// An elf section header
struct section_header_t {
    uint32_t   sh_name;
    uint32_t   sh_type;
    elfaddr_t  sh_flags;
    elfaddr_t  sh_addr;
    elfaddr_t  sh_offset;
    elfaddr_t  sh_size;
    uint32_t   sh_link;
    uint32_t   sh_info;
    elfaddr_t   sh_addralign;
    elfaddr_t   sh_entsize;
};

// A symbol table entry
struct symbol_t {
    uint32_t      st_name;

#ifdef ELF64
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
    elfaddr_t     st_value;
    uint64_t      st_size;
#else
    elfaddr_t     st_value;
    uint32_t      st_size;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
#endif
};

// A relocation from a relocation section
struct rela_t {
    elfaddr_t  r_offset;
#ifdef ELF64
    uint64_t   r_info;
    uint32_t   r_type() {return r_info & 0xffffffff;}
    uint32_t   r_sym()  {return r_info >> 32;}
    int64_t    r_addend;
#else
    uint32_t   r_info;
    uint32_t   r_type() {return r_info & 0xff;}
    uint32_t   r_sym()  {return r_info >> 8;}
    int32_t    r_addend;
#endif
};

struct elf_t {
    // The object file in memory
    char *buf;
    size_t size;

    // Set to true to spew debug info
    bool debug;

    // If it fails, this records the line number
    int failed;

    // Pointer to the header
    elf_header_t *header;

    // Sections of interest
    section_header_t
        *sec_symtab,   // The symbol table
        *sec_secnames, // The name of each section, i.e. the table of contents
        *sec_text,     // The .text section where the functions live
        *sec_strtab;   // The string table, for looking up symbol names

    // The writeable portions of the object file in memory
    char *writeable_buf;
    size_t writeable_size;

    // The global offset table for PIC code
    elfaddr_t *global_offset_table;
    int global_offset_table_entries;
    int max_global_offset_table_entries;

    // Load an object file in memory. Does not take
    // ownership of the memory. Memory should be page-aligned.
    void parse_object_file(const unsigned char *b, size_t s, bool d = false) {
        failed = 0;
        buf = NULL;
        writeable_buf = NULL;
        writeable_size = 0;
        size = (s + alignment - 1) & ~(alignment - 1);
        debug = d;

        int global_offset_table_size = 4096;

        // Make a mapping of the appropriate size and type. We
        // allocate the size of the object file for the executable
        // stuff, the same size again to make a writeable copy, and
        // then an extra page for the global offset table for PIC
        // code.
        typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
        mmap_fn mmap = (mmap_fn)halide_get_symbol("mmap");
        if (!mmap) {
            log_printf("mmap symbol not found");
            fail(__LINE__);
            return;
        }
        const int PROT_READ = 0x01;
        const int PROT_WRITE = 0x02;
        const int MAP_PRIVATE = 0x0002;
        const int MAP_ANON = 0x1000;
        buf = (char *)mmap(NULL, size * 2 + global_offset_table_size,
                           PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

        // Copy over the data
        memcpy(buf, b, s);

        // Set up the global offset table
        global_offset_table = (elfaddr_t *)(buf + size*2);
        global_offset_table_entries = 0;
        max_global_offset_table_entries = global_offset_table_size / sizeof(elfaddr_t);
        memset(global_offset_table, 0, global_offset_table_size);

        // Grab the ELF header
        if (size < sizeof(elf_header_t)) {
            fail(__LINE__);
            return;
        }
        header = (elf_header_t *)buf;

        // Get the section names section first
        sec_secnames = get_section(header->e_shstrndx);
        if (failed) return;

        // Walk over the other sections
        for (int i = 0; i < num_sections(); i++) {
            section_header_t *sec = get_section(i);
            const char *sec_name = get_section_name(sec);
            if (failed) return;
            if (debug) log_printf("\nSection %s at %p:\n",
                              sec_name,
                              get_addr(get_section_offset(sec)));

            // The text, symbol table, and string table sections have
            // types 1, 2, and 3 respectively in the ELF spec.
            if (sec->sh_type == 1 && strncmp(sec_name, ".text", 5) == 0) {
                sec_text = sec;
            } else if (sec->sh_type == 2) {
                sec_symtab = sec;
            } else if (sec->sh_type == 3) {
                sec_strtab = sec;
            }
        }
    }

    void fail(int line) {
        log_printf("Failure at line %d\n", line);
        failed = line;
    }

    void move_writeable_sections() {
        // Move the writeable sections to their own mapping. First
        // determine their span.
        char *min_addr = NULL, *max_addr = NULL;
        for (int i = 0; i < num_sections(); i++) {
            section_header_t *sec = get_section(i);
            if (is_section_writeable(sec)) {
                char *start = get_section_start(sec);
                char *end = start + get_section_size(sec);
                if (min_addr == NULL || start < min_addr) {
                    min_addr = start;
                }
                if (max_addr == NULL || end > max_addr) {
                    max_addr = end;
                }
            }
        }
        size_t size_to_copy = (max_addr - min_addr);

        if (size_to_copy == 0) {
            if (debug) log_printf("No writeable sections\n");
            return;
        }

        // Copying over the span may also copy a bunch of intermediate
        // junk, e.g. if the first and last sections of the object
        // file are writeable, but in practice we've found that the
        // writeable sections are in one tight cluster. We don't want
        // to copy over the sections individually, because some of
        // them alias.

        // Align up the size for the mapping
        writeable_size = (size_to_copy + alignment - 1) & ~(alignment - 1);

        // Make the mapping
        writeable_buf = buf + size;

        if (!writeable_buf) {
            fail(__LINE__);
            return;
        }

        if (debug) {
            log_printf("Copying %d bytes of writeable data from %p to %p to a separate mapping of size %d at %p\n",
                       (int)size_to_copy, min_addr, max_addr, (int)writeable_size, writeable_buf);
        }

        // Copy over the sections
        memcpy(writeable_buf, min_addr, size_to_copy);

        // How far did the sections move?
        int64_t delta = writeable_buf - min_addr;

        // Adjust the section offsets in the section table so that
        // whenever we go looking for one of these sections we find it
        // in the writeable mapping.
        for (int i = 0; i < num_sections(); i++) {
            section_header_t *sec = get_section(i);
            const char *sec_name = get_section_name(sec);
            if (failed) return;
            if (is_section_writeable(sec)) {
                if (debug) log_printf("Section %s is writeable. Moving it\n", sec_name);
                // Make the section table point to the writeable copy instead
                sec->sh_offset += delta;
            }
        }
    }

    void deinit() {
        typedef int (*munmap_fn)(void *, size_t);
        munmap_fn munmap = (munmap_fn)halide_get_symbol("munmap");
        if (munmap) {
            munmap(buf, size * 2);
        }
    }

    // Get the address given an offset into the buffer. Asserts that
    // it's in-range.
    char *get_addr(elfaddr_t off) {
        int64_t o = (int64_t)off;
        char *addr = buf + o;
        if ((addr < buf || addr >= buf + size) &&
            (addr < writeable_buf || addr >= writeable_buf + writeable_size)) {
            log_printf("Offset out of bounds: %p\n", addr);
            fail(__LINE__);
            return NULL;
        }
        return addr;
    }

    // Get the number of sections
    int num_sections() {
        return header->e_shnum;
    }

    // Get a section by index
    section_header_t *get_section(int i) {
        if (!header) {
            fail(__LINE__);
            return NULL;
        }
        elfaddr_t off = header->e_shoff + i * header->e_shentsize;
        if (off + sizeof(section_header_t) > size) {
            fail(__LINE__);
            return NULL;
        }
        return (section_header_t *)get_addr(off);
    }

    // Get the starting address of a section
    char *get_section_start(section_header_t *sec) {
        return get_addr(sec->sh_offset);
    }

    // Get the offset of a section
    elfaddr_t get_section_offset(section_header_t *sec) {
        return sec->sh_offset;
    }

    // Get the size of a section in bytes
    int get_section_size(section_header_t *sec) {
        return sec->sh_size;
    }

    bool is_section_writeable(section_header_t *sec) {
        // Writeable sections have the SHF_WRITE bit set, which is bit
        // 1.
        return (sec->sh_flags & 1);
    }

    // Get the name of a section
    const char *get_section_name(section_header_t *sec) {
        if (!sec_secnames) {
            fail(__LINE__);
            return NULL;
        }
        return get_addr(get_section_offset(sec_secnames) + sec->sh_name);
    }

    // Look up a section by name
    section_header_t *find_section(const char *name) {
        for (int i = 0; i < num_sections(); i++) {
            section_header_t *sec = get_section(i);
            const char *sec_name = get_section_name(sec);
            if (strncmp(sec_name, name, strlen(name)+1) == 0) {
                return sec;
            }
        }
        return NULL;
    }

    // The number of symbols in the symbol table
    int num_symbols() {
        if (!sec_symtab) {
            fail(__LINE__);
            return 0;
        }
        return get_section_size(sec_symtab) / sizeof(symbol_t);
    }

    // Get a symbol from the symbol table by index
    symbol_t *get_symbol(int i) {
        if (!sec_symtab) {
            fail(__LINE__);
            return 0;
        }
        return (symbol_t *)(get_addr(get_section_offset(sec_symtab) + i * sizeof(symbol_t)));
    }

    // Get the name of a symbol
    const char *get_symbol_name(symbol_t *sym) {
        if (!sec_strtab) {
            fail(__LINE__);
            return NULL;
        }
        return (const char *)(get_addr(get_section_offset(sec_strtab) + sym->st_name));
    }

    // Get the section a symbol exists in. NULL for extern symbols.
    section_header_t *get_symbol_section(symbol_t *sym) {
        if (sym->st_shndx == 0) return NULL;
        return get_section(sym->st_shndx);
    }

    // Check if a symbol exists in this object file
    bool symbol_is_defined(symbol_t *sym) {
        return get_symbol_section(sym) != NULL;
    }

    // Get the address of a symbol
    char *get_symbol_addr(symbol_t *sym) {
        return get_addr(get_section_offset(get_symbol_section(sym)) + sym->st_value);
    }

    // Look up a symbol by name
    symbol_t *find_symbol(const char *name) {
        if (debug) log_printf("find_symbol(%s)\n", name);

        const size_t len = strlen(name);

        for (int i = 0; i < num_symbols(); i++) {
            symbol_t *sym = get_symbol(i);
            const char *sym_name = get_symbol_name(sym);
            if (strncmp(sym_name, name, len+1) == 0) {
                if (debug) log_printf("-> %p\n", sym);
                return sym;
            }
        }

        return NULL;
    }

    // Get the number of relocations in a relocation section
    int num_relas(section_header_t *sec_rela) {
        if (!sec_rela) {
            fail(__LINE__);
            return 0;
        }
        return get_section_size(sec_rela) / sizeof(rela_t);
    }

    // Get a relocation from a relocation section by index
    rela_t *get_rela(section_header_t *sec_rela, int i) {
        if (!sec_rela) {
            fail(__LINE__);
            return NULL;
        }
        return (rela_t *)(get_addr(get_section_offset(sec_rela) + i * sizeof(rela_t)));
    }

    void do_reloc(char *addr, uint32_t mask, uintptr_t val, bool is_signed, bool verify) {
        uint32_t inst = *((uint32_t *)addr);
        if (debug) {
            log_printf("Fixup inside instruction at %lx:\n  %08lx\n",
                   (uint32_t)(addr - get_addr(get_section_offset(sec_text))), inst);
            log_printf("val: 0x%08lx\n", (unsigned long)val);
            log_printf("mask: 0x%08lx\n", mask);
        }

        if (!mask) {

            // The mask depends on the instruction. To implement
            // relocations for new instructions see
            // instruction_encodings.txt
            if (debug) {
                // First print the bits so I can search for it in the
                // instruction encodings.
                log_printf("Instruction bits: ");
                for (int i = 31; i >=0; i--) {
                    log_printf("%d", (int)((inst >> i) & 1));
                }
                log_printf("\n");
            }

            if ((inst & (3 << 14)) == 0) {
                // Some instructions are actually pairs of 16-bit
                // subinstructions. See section 3.7 in the
                // programmer's reference.
                if (debug) log_printf("Duplex!\n");

                int iclass = ((inst >> 29) << 1) | ((inst >> 13) & 1);
                if (debug) {
                    log_printf("Class: %x\n", iclass);
                    log_printf("Hi: ");
                    for (int i = 28; i >= 16; i--) {
                        log_printf("%d", (int)((inst >> i) & 1));
                    }
                    log_printf("\n");
                    log_printf("Lo: ");
                    for (int i = 12; i >= 0; i--) {
                        log_printf("%d", (int)((inst >> i) & 1));
                    }
                    log_printf("\n");
                }

                // We only know how to do the ones where the high
                // subinstruction is an immediate assignment. (marked
                // as A in table 9-4 in the programmer's reference
                // manual).
                if (iclass < 3 || iclass > 7) {
                    fail(__LINE__);
                    return;
                }

                // Pull out the subinstructions. They're the low 13
                // bits of each half-word.
                uint32_t hi = (inst >> 16) & ((1 << 13) - 1);
                uint32_t lo = inst & ((1 << 13) - 1);

                // We only understand the ones where hi starts with 010
                if ((hi >> 10) != 2) {
                    fail(__LINE__);
                    return;
                }

                // Low 6 bits of val go in the following bits.
                mask = 63 << 20;

            } else if ((inst >> 24) == 72) {
                // Example instruction encoding that has this high byte (ignoring bits 1 and 2):
                // 0100 1ii0  000i iiii  PPit tttt  iiii iiii
                if (debug) log_printf("Instruction-specific case A\n");
                mask = 0x061f20ff;
            } else if ((inst >> 24) == 73) {
                // 0100 1ii1  000i iiii  PPii iiii  iiid dddd
                if (debug) log_printf("Instruction-specific case B\n");
                mask = 0x061f3fe0;
            } else if ((inst >> 24) == 120) {
                // 0111 1000  ii-i iiii  PPii iiii  iiid dddd
                if (debug) log_printf("Instruction-specific case C\n");
                mask = 0x00df3fe0;
            } else if ((inst >> 16) == 27209) {
                // 0110 1010  0100 1001  PP-i iiii  i--d dddd
                mask = 0x00001f80;
            } else if ((inst >> 25) == 72) {
                // 1001 0ii0  101s ssss  PPii iiii  iiid dddd
                // 1001 0ii1  000s ssss  PPii iiii  iiid dddd
                mask = 0x06003fe0;
            } else if ((inst >> 24) == 115 || (inst >> 24) == 124) {
                // 0111 0011 -10sssss PP1iiiii iiiddddd
                // 0111 0011 -11sssss PP1iiiii iiiddddd
                // 0111 0011 0uusssss PP0iiiii iiiddddd
                // 0111 0011 1uusssss PP0iiiii iiiddddd
                // 0111 0011 -00sssss PP1iiiii iiiddddd
                // 0111 0011 -01sssss PP1iiiii iiiddddd
                // 0111 1100 0IIIIIII PPIiiiii iiiddddd
                // 0111 0011 -11sssss PP1iiiii iiiddddd
                mask = 0x00001fe0;

            } else {
                log_printf("Unhandled!\n");
                fail(__LINE__);
                return;
            }
        }

        uintptr_t old_val = val;
        bool consumed_every_bit = false;
        for (int i = 0; i < 32; i++) {
            if (mask & (1 << i)) {
                if (inst & (1 << i)) {
                    // This bit should be zero in the unrelocated instruction
                    fail(__LINE__);
                    return;
                }
                // Consume a bit of val
                int next_bit = val & 1;
                if (is_signed) {
                    consumed_every_bit |= ((intptr_t)val) == -1;
                    val = ((intptr_t)val) >> 1;
                } else {
                    val = ((uintptr_t)val) >> 1;
                }
                consumed_every_bit |= (val == 0);
                inst |= (next_bit << i);
            }
        }

        if (verify && !consumed_every_bit) {
            log_printf("Relocation overflow inst=%08lx mask=%08lx val=%08lx\n",
                       inst, mask, (unsigned long)old_val);
            fail(__LINE__);
            return;
        }

        if (debug) log_printf("Relocated instruction:\n  %08lx\n", inst);
        *((uint32_t *)addr) = inst;
    }

    // Do all the relocations for sec (e.g. .text), using the list of
    // relocations in sec_rela (e.g. .rela.text)
    void do_relocations_for_section(section_header_t *sec, section_header_t *sec_rela) {
        if (!sec_rela || !sec) {
            fail(__LINE__);
            return;
        }

        // Read from the GP register for GP-relative relocations. We
        // need to do this with some inline assembly.
        char *GP = NULL;
        asm ("{%0 = gp}\n" : "=r"(GP) : : );
        if (debug) log_printf("GP = %p\n", GP);

        for (int i = 0; i < num_relas(sec_rela); i++) {
            rela_t *rela = get_rela(sec_rela, i);
            if (!rela) {
                fail(__LINE__);
                return;
            }
            if (debug) log_printf("\nRelocation %d of type %lu:\n", i, rela->r_type());

            // The location to make a change
            char *fixup_addr = get_addr(get_section_offset(sec) + rela->r_offset);
            if (debug) log_printf("Fixup address %p\n", fixup_addr);

            // We're fixing up a reference to the following symbol
            symbol_t *sym = get_symbol(rela->r_sym());

            const char *sym_name = get_symbol_name(sym);
            if (debug) log_printf("Applies to symbol %s\n", sym_name);

            char *sym_addr = NULL;
            if (!symbol_is_defined(sym)) {
                if (strncmp(sym_name, "_GLOBAL_OFFSET_TABLE_", 22) == 0) {
                    sym_addr = (char *)global_offset_table;
                } else {
                    sym_addr = (char *)halide_get_symbol(sym_name);
                }
                if (!sym_addr) {
                    log_printf("Failed to resolve external symbol: %s\n", sym_name);
                    fail(__LINE__);
                    return;
                }
            } else {
                section_header_t *sym_sec = get_symbol_section(sym);
                const char *sym_sec_name = get_section_name(sym_sec);
                if (debug) log_printf("Symbol is in section: %s\n", sym_sec_name);

                sym_addr = get_symbol_addr(sym);
                if (debug) log_printf("Symbol is at address: %p\n", sym_addr);
            }

            // Hexagon relocations are specified in section 11.5 in
            // the Hexagon Application Binary Interface spec.

            // Find the symbol's index in the global_offset_table
            int global_offset_table_idx = global_offset_table_entries;
            for (int i = 0; i < global_offset_table_entries; i++) {
                if ((elfaddr_t)sym_addr == global_offset_table[i]) {
                    global_offset_table_idx = i;
                    break;
                }
            }

            // Now we can define the variables from Table 11-5.
            char *S = sym_addr;
            char *P = fixup_addr;
            intptr_t A = rela->r_addend;
            elfaddr_t G = global_offset_table_idx * sizeof(elfaddr_t);

            // Define some constants from table 11-3
            const uint32_t Word32     = 0xffffffff;
            const uint32_t Word16     = 0xffff;
            const uint32_t Word8      = 0xff;
            const uint32_t Word32_B22 = 0x01ff3ffe;
            const uint32_t Word32_B15 = 0x00df20fe;
            const uint32_t Word32_B13 = 0x00202ffe;
            const uint32_t Word32_B9  = 0x003000fe;
            const uint32_t Word32_B7  = 0x00001f18;
            const uint32_t Word32_GP  = 0; // The mask is instruction-specific
            const uint32_t Word32_X26 = 0x0fff3fff;
            const uint32_t Word32_U6  = 0; // The mask is instruction-specific
            const uint32_t Word32_R6  = 0x000007e0;
            const uint32_t Word32_LO  = 0x00c03fff;
            const bool truncate = false, verify = true;
            const bool _unsigned = false, _signed = true;

            bool needs_global_offset_table_entry = false;

            switch (rela->r_type()) {
            case 1:
                // Address to fix up, mask, value, signed, verify
                do_reloc(fixup_addr, Word32_B22, intptr_t(S + A - P) >> 2, _signed, verify);
                break;
            case 2:
                // Untested
                do_reloc(fixup_addr, Word32_B15, intptr_t(S + A - P) >> 2, _signed, verify);
                break;
            case 3:
                // Untested
                do_reloc(fixup_addr, Word32_B7, intptr_t(S + A - P) >> 2, _signed, verify);
                break;
            case 4:
                // Untested
                do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 5:
                // Untested
                do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A) >> 16, _unsigned, truncate);
                break;
            case 6:
                do_reloc(fixup_addr, Word32, intptr_t(S + A) >> 2, _signed, truncate);
                break;
            case 7:
                // Untested
                do_reloc(fixup_addr, Word16, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 8:
                // Untested
                do_reloc(fixup_addr, Word8, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 9:
                do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP), _unsigned, verify);
                break;
            case 10:
                do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 1, _unsigned, verify);
                break;
            case 11:
                do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 2, _unsigned, verify);
                break;
            case 12:
                do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 3, _unsigned, verify);
                break;
            case 13:
                // Untested
                do_reloc(fixup_addr,   Word32_LO, uintptr_t(S + A) >> 16, _unsigned, truncate);
                do_reloc(fixup_addr+4, Word32_LO, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 14:
                // Untested
                do_reloc(fixup_addr, Word32_B13, intptr_t(S + A - P) >> 2, _signed, verify);
                break;
            case 15:
                // Untested
                do_reloc(fixup_addr, Word32_B9, intptr_t(S + A - P) >> 2, _signed, verify);
                break;
            case 16:
                do_reloc(fixup_addr, Word32_X26, intptr_t(S + A - P) >> 6, _signed, truncate);
                break;
            case 17:
                do_reloc(fixup_addr, Word32_X26, uintptr_t(S + A) >> 6, _unsigned, verify);
                break;
            case 18:
                // Untested
                do_reloc(fixup_addr, Word32_B22, intptr_t(S + A - P) & 0x3f, _signed, verify);
                break;
            case 19:
                // Untested
                do_reloc(fixup_addr, Word32_B15, intptr_t(S + A - P) & 0x3f, _signed, verify);
                break;
            case 20:
                // Untested
                do_reloc(fixup_addr, Word32_B13, intptr_t(S + A - P) & 0x3f, _signed, verify);
                break;
            case 21:
                // Untested
                do_reloc(fixup_addr, Word32_B9, intptr_t(S + A - P) & 0x3f, _signed, verify);
                break;
            case 22:
                // Untested
                do_reloc(fixup_addr, Word32_B7, intptr_t(S + A - P) & 0x3f, _signed, verify);
                break;
            case 23:
                do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 24:
                do_reloc(fixup_addr, Word32_R6, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 25: // These ones all seem to mean the same thing. Only 30 is tested.
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
                do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A), _unsigned, truncate);
                break;
            case 31:
                // Untested
                do_reloc(fixup_addr, Word32, intptr_t(S + A - P), _signed, verify);
                break;
            case 65:
                do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A - P), _unsigned, truncate);
                break;
            case 69:
                do_reloc(fixup_addr, Word32_X26, intptr_t(G) >> 6, _signed, truncate);
                needs_global_offset_table_entry = true;
                break;
            case 71:
                do_reloc(fixup_addr, Word32_U6, uintptr_t(G), _unsigned, truncate);
                needs_global_offset_table_entry = true;
                break;

            default:
                // The remaining types are all for things like thread-locals.
                log_printf("Unhandled relocation type %lu.\n", rela->r_type());
                fail(__LINE__);
                return;
            }

            if (needs_global_offset_table_entry &&
                global_offset_table_idx == global_offset_table_entries) {
                // This symbol needs a slot in the global offset table
                if (global_offset_table_entries == max_global_offset_table_entries) {
                    log_printf("Out of space in the global offset table\n");
                    fail(__LINE__);
                    return;
                } else {
                    global_offset_table[global_offset_table_idx] = (elfaddr_t)S;
                    global_offset_table_entries++;
                }
            }
        }

    }

    // Do relocations for all relocation sections in the object file
    void do_relocations() {
        for (int i = 0; i < num_sections(); i++) {
            section_header_t *sec = get_section(i);
            const char *sec_name = get_section_name(sec);
            if (strncmp(sec_name, ".rela.", 6) == 0) {
                // It's a relocations section for something
                section_header_t *sec_to_relocate = find_section(sec_name + 5);
                if (!sec_to_relocate) {
                    fail(__LINE__);
                    return;
                }
                if (debug) log_printf("Relocating: %s\n", sec_name);
                do_relocations_for_section(sec_to_relocate, sec);
                if (failed) return;
                if (debug) log_printf("Done relocating: %s\n", sec_name);
            }
        }

        // Dump the global offset table
        if (debug) {
            log_printf("global offset table:");
            for (int i = 0; i < global_offset_table_entries; i++) {
                log_printf(" %08lx\n", global_offset_table[i]);
            }
        }
    }

    // Mark the executable pages of the object file executable
    void make_executable() {
        typedef int (*mprotect_fn)(void *, size_t, int);
        mprotect_fn mprotect = (mprotect_fn)halide_get_symbol("mprotect");
        if (!mprotect) {
            log_printf("mprotect symbol not found");
            fail(__LINE__);
            return;
        }
        const int PROT_READ = 0x01;
        const int PROT_EXEC = 0x04;
        int err = mprotect(buf, size, PROT_EXEC | PROT_READ);
        if (err) {
            log_printf("mprotect %d %p %d", err, buf, size);
            fail(__LINE__);
        }
    }

    // Dump the object file to stdout base-64 encoded. This is useful
    // for getting the relocation object file back over channels where
    // all you have is a logging mechanism.
    void dump_as_base64() {
        // For base-64 encoding
        static const char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                              'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                              'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                              'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                              'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                              'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                              'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                              '4', '5', '6', '7', '8', '9', '+', '/'};
        // Dump the object in base 64
        log_printf("BEGIN BASE64\n");
        for (int i = 0; i < size; i += 3) {
            // every group of 3 bytes becomes 4 output bytes
            uint32_t a = buf[i];
            uint32_t b = buf[i+1];
            uint32_t c = buf[i+2];
            uint32_t triple = (a << 16) | (b << 8) | c;
            log_printf("%c%c%c%c",
                   encoding_table[(triple >> (3*6)) & 0x3f],
                   encoding_table[(triple >> (2*6)) & 0x3f],
                   encoding_table[(triple >> (1*6)) & 0x3f],
                   encoding_table[(triple >> (0*6)) & 0x3f]);
        }
        log_printf("\nEND BASE64\n");
    }
};

// dlopen a relocatable (but not yet relocated) object file in
// memory. The object should be compiled with -fno-pic.
inline elf_t *obj_dlopen_mem(const unsigned char *code, int code_size) {
    elf_t *elf = (elf_t *)malloc(sizeof(elf_t));
    if (!elf) {
        return NULL;
    }
    elf->parse_object_file(code, code_size, false);
    elf->move_writeable_sections();
    elf->do_relocations();
    //elf->dump_as_base64();
    elf->make_executable();

    // TODO: Should we run .ctors?
    return elf;
}

// Find a symbol in a handle returned by obj_dlopen_mem
inline void *obj_dlsym(elf_t *elf, const char *name) {
    if (!elf) return NULL;
    symbol_t *sym = elf->find_symbol(name);
    if (!sym) return NULL;
    if (!elf->symbol_is_defined(sym)) return NULL;
    return (void *)elf->get_symbol_addr(sym);
}

// Release an object opened by obj_dlopen_mem
inline int obj_dlclose(elf_t *elf) {
    // TODO: Should we run .dtors?
    elf->deinit();
    return 0;
}

#endif
