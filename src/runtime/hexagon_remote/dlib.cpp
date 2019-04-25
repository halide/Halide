extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
}

#include <HalideRuntime.h>
#include "dlib.h"
#include "log.h"

typedef uint32_t elfaddr_t;

// The standard ELF header. See
// http://man7.org/linux/man-pages/man5/elf.5.html for the meanings of
// these fields.
struct Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    elfaddr_t e_entry;
    elfaddr_t e_phoff;
    elfaddr_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};


enum {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
};

struct Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    elfaddr_t p_vaddr;
    elfaddr_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

enum {
    PF_X = 1,
    PF_W = 2,
    PF_R = 4,
    PF_MASKOS = 0x0ff00000,
    PF_MASKPROC = 0xf0000000,
};

// A symbol table entry
struct Sym {
    uint32_t st_name;
    elfaddr_t st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
};

enum {
    STN_UNDEF = 0,
};

// Hexagon shared object relocation types.
enum {
    R_HEX_COPY = 32,
    R_HEX_GLOB_DAT = 33,
    R_HEX_JMP_SLOT = 34,
    R_HEX_RELATIVE = 35,
};

// A relocation from a relocation section
struct Rela {
    elfaddr_t r_offset;
    uint32_t r_info;
    uint32_t r_type() const {return r_info & 0xff;}
    uint32_t r_sym() const {return r_info >> 8;}
    int32_t r_addend;
};

enum {
    DT_NULL = 0,
    DT_NEEDED = 1,
    DT_PLTRELSZ = 2,
    DT_PLTGOT = 3,
    DT_HASH = 4,
    DT_STRTAB = 5,
    DT_SYMTAB = 6,
    DT_RELA = 7,
    DT_RELASZ = 8,
    DT_RELAENT = 9,
    DT_STRSZ = 10,
    DT_SYMENT = 11,
    DT_INIT = 12,
    DT_FINI = 13,
    DT_SONAME = 14,
    DT_RPATH = 15,
    DT_SYMBOLIC = 16,
    DT_REL = 17,
    DT_RELSZ = 18,
    DT_RELENT = 19,
    DT_PLTREL = 20,
    DT_DEBUG = 21,
    DT_TEXTREL = 22,
    DT_JMPREL = 23,
    DT_LOPROC = 0x70000000,
    DT_HIPROC = 0x7fffffff,
};

struct Dyn {
    uint32_t tag;
    elfaddr_t value;
};

// Wrapper around an ELF hash table. Does not take ownership of the
// table.
struct hash_table {
    const uint32_t *table;

    static unsigned long elf_hash(const char *name) {
        unsigned long h = 0;
        unsigned long g;
        for (char c = *name; c; c = *name++) {
            h = (h << 4) + c;
            g = h & 0xf0000000;
            if (g != 0) {
                h ^= g >> 24;
            }
            h &= ~g;
        }
        return h;
    }

    uint32_t bucket_count() const { return table[0]; }
    uint32_t chain_count() const { return table[1]; }
    const uint32_t *buckets() const { return &table[2]; }
    const uint32_t *chains() const { return buckets() + bucket_count(); }

    uint32_t lookup_chain(const char *name) {
        return buckets()[elf_hash(name) % bucket_count()];
    }

    uint32_t next_in_chain(uint32_t sym) {
        if (sym < chain_count()) {
            return chains()[sym];
        } else {
            return STN_UNDEF;
        }
    }
};

// TODO: This should be made thread safe. Not easy because we can't
// statically initialize a mutex. This should be made thread safe from
// outside the runtime for now...
struct dlib_t;
dlib_t *loaded_libs = NULL;

struct dlib_t {
    char *program;
    size_t program_size;

    // Pointer to virtual address 0.
    char *base_vaddr;

    // Information about the symbols.
    const char *strtab;
    const Sym *symtab;
    typedef void (*init_fini_t)(void);
    init_fini_t fini;
    init_fini_t init;

    hash_table hash;

    // We keep a linked list of these, to implement dlsym's ability to find symbols loaded in other libraries.
    dlib_t *next;

    bool assert_in_bounds(const void *begin, const void *end) {
        if (program <= (char *)begin && (char *)end <= program + program_size) {
            return true;
        } else {
            log_printf("Address range [%x, %x) out of bounds [%x, %x)\n",
                       begin, end, program, program + program_size);
            return false;
        }
    }

    template <typename T>
    bool assert_in_bounds(const T *x, size_t count = 1) {
        return assert_in_bounds(x, x + count);
    }

    bool do_relocations(const Rela *relocs, int count) {
        for (int i = 0; i < count; i++) {
            const Rela &r = relocs[i];
            uint32_t *fixup_addr = (uint32_t *)(base_vaddr + r.r_offset);
            if (!assert_in_bounds(fixup_addr)) return false;
            const char *S = NULL;
            const char *B = program;
            int32_t A = r.r_addend;
            if (r.r_sym() != 0) {
                const Sym *sym = &symtab[r.r_sym()];
                if (!assert_in_bounds(sym)) return false;
                const char *sym_name = &strtab[sym->st_name];
                if (!assert_in_bounds(sym_name)) return false;

                if (sym->st_value == 0) {
                    if (!sym_name) {
                        log_printf("Symbol name not defined");
                        return false;
                    }
                    S = (const char *)mmap_dlsym(RTLD_DEFAULT, sym_name);
                    if (!S) {
                        log_printf("Unresolved external symbol %s\n", sym_name);
                        return false;
                    }
                } else {
                    S = base_vaddr + sym->st_value;
                    if (!assert_in_bounds(S, sym->st_size)) return false;
                }
            }

            switch (r.r_type()) {
            case R_HEX_COPY: *fixup_addr = (uint32_t)S; break;
            case R_HEX_GLOB_DAT: *fixup_addr = (uint32_t)(S + A); break;
            case R_HEX_JMP_SLOT: *fixup_addr = (uint32_t)(S + A); break;
            case R_HEX_RELATIVE: *fixup_addr = (uint32_t)(B + A); break;
            default:
                log_printf("Unsupported relocation type %d\n", r.r_type());
                return false;
            }
        }
        return true;
    }

    bool parse_dynamic(const Dyn *dynamic) {
        strtab = NULL;
        symtab = NULL;
        hash.table = NULL;
        fini = NULL;
        init = NULL;

        const Rela *jmprel = NULL;
        int jmprel_count = 0;
        const Rela *rel = NULL;
        int rel_count = 0;

        for (int i = 0; dynamic[i].tag != DT_NULL; i++) {
            const Dyn &d = dynamic[i];
            switch (d.tag) {
            case DT_HASH:
                hash.table = (const uint32_t *)(base_vaddr + d.value);
                break;
            case DT_SYMTAB:
                symtab = (const Sym *)(base_vaddr + d.value);
                break;
            case DT_SYMENT:
                if (d.value != sizeof(Sym)) {
                    log_printf("Unknown symbol size %d\n", d.value);
                    return false;
                }
                break;
            case DT_STRTAB:
                strtab = (const char *)(base_vaddr + d.value);
                break;
            case DT_STRSZ:
                break;
            case DT_PLTGOT:
                break;
            case DT_JMPREL:
                jmprel = (const Rela *)(base_vaddr + d.value);
                break;
            case DT_PLTREL:
                if (d.value != DT_RELA) {
                    log_printf("DT_JMPREL was not DT_RELA\n");
                    return false;
                }
                break;
            case DT_PLTRELSZ:
                jmprel_count = d.value / sizeof(Rela);
                break;
            case DT_RELA:
                rel = (const Rela *)(base_vaddr + d.value);
                break;
            case DT_RELASZ:
                rel_count = d.value / sizeof(Rela);
                break;
            case DT_INIT:
                init = (init_fini_t) (base_vaddr + d.value);
                break;
            case DT_FINI:
                fini = (init_fini_t) (base_vaddr + d.value);
                break;
            case DT_RELAENT:
                if (d.value != sizeof(Rela)) {
                    log_printf("DT_RELAENT was not 12 bytes.\n");
                    return false;
                }
                break;
            }
        }

        if (!symtab) {
            log_printf("Symbol table not found.\n");
            return false;
        }
        if (!strtab) {
            log_printf("String table not found.\n");
            return false;
        }
        if (!hash.table) {
            log_printf("Hash table not found.\n");
            return false;
        }

        if (jmprel && jmprel_count > 0) {
            if (!do_relocations(jmprel, jmprel_count)) {
                return false;
            }
        }
        if (rel && rel_count > 0) {
            if (!do_relocations(rel, rel_count)) {
                return false;
            }
        }
        return true;
    }

    bool parse(const char *data, size_t size) {
        if (size < sizeof(Ehdr)) {
            log_printf("Buffer is not a valid elf file.\n");
            return false;
        }
        const Ehdr *ehdr = (Ehdr *)data;
        if (ehdr->e_type != 3) {
            log_printf("Buffer is not a shared object (type=%d)\n", ehdr->e_type);
            return false;
        }

        typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
        typedef int (*mprotect_fn)(void *, size_t, int);
        mmap_fn mmap = (mmap_fn)halide_get_symbol("mmap");
        mprotect_fn mprotect = (mprotect_fn)halide_get_symbol("mprotect");
        if (!mmap || !mprotect) {
            log_printf("mmap/mprotect symbol not found");
            return false;
        }
        const int PROT_READ = 0x01;
        const int PROT_WRITE = 0x02;
        const int PROT_EXEC = 0x04;
        const int MAP_PRIVATE = 0x0002;
        const int MAP_ANON = 0x1000;

        const size_t alignment = 4096;
        size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
        program = (char *)mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (!program) {
            log_printf("mmap failed %d\n", aligned_size);
            return false;
        }
        program_size = size;
        base_vaddr = NULL;
        memcpy(program, data, program_size);
        ehdr = (const Ehdr *)program;
        const Phdr *phdrs = (Phdr *)(program + ehdr->e_phoff);
        if (!assert_in_bounds(phdrs, ehdr->e_phnum)) return false;
        const Dyn *dynamic = NULL;
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdrs[i].p_type == PT_NULL) {
                // PT_NULL should be ignored entirely.
                continue;
            }
            size_t size_i = phdrs[i].p_memsz;
            size_t offset_i = phdrs[i].p_offset;
            if (size_i != phdrs[i].p_filesz) {
                log_printf("Program header has mem size %d not equal to file size %d\n", size_i, phdrs[i].p_filesz);
                return false;
            }
            char *program_i = program + offset_i;
            if (!assert_in_bounds(program_i, size_i)) return false;
            if (phdrs[i].p_type == PT_LOAD) {
                if (!base_vaddr) {
                    base_vaddr = program + offset_i - phdrs[i].p_vaddr;
                } else if (base_vaddr != program + offset_i - phdrs[i].p_vaddr) {
                    log_printf("Cannot load program with non-contiguous virtual address space\n");
                    return false;
                }
                if (offset_i % alignment != 0 || size_i % alignment != 0) {
                    log_printf("Cannot load program with unaligned range [%d, %d)\n", offset_i, offset_i + size_i);
                    return false;
                }
                int prot = 0;
                if (phdrs[i].p_flags & PF_R) prot |= PROT_READ;
                if (phdrs[i].p_flags & PF_W) prot |= PROT_WRITE;
                if (phdrs[i].p_flags & PF_X) prot |= PROT_EXEC;
                int err = mprotect(program_i, size_i, prot);
                if (err) {
                    log_printf("mprotect failed %d %p %d\n", err, program_i, size_i);
                    return false;
                }
            } else if (phdrs[i].p_type == PT_DYNAMIC) {
                dynamic = (const Dyn *)(program_i);
            }
        }
        if (!dynamic) {
            log_printf("Did not find PT_DYNAMIC\n");
            return false;
        }

        if (!parse_dynamic(dynamic)) {
            return false;
        }
        return true;
    }
    void run_dtors() {
        if (fini) {
            fini();
        }
    }
    void run_ctors() {
        if (init) {
            init();
        }
    }
    void deinit() {
        typedef int (*munmap_fn)(void *, size_t);
        munmap_fn munmap = (munmap_fn)halide_get_symbol("munmap");
        if (munmap) {
            munmap(program, program_size);
        }
    }

    // Check if a symbol exists in this object file
    bool symbol_is_defined(const Sym *sym) {
        return sym->st_value != 0;
    }

    // Get the address of a symbol
    char *get_symbol_addr(const Sym *sym) {
        char *addr = base_vaddr + sym->st_value;
        if (!assert_in_bounds(addr, sym->st_size)) return NULL;
        return addr;
    }

    // Look up a symbol by name
    const Sym *find_symbol(const char *name) {
        const size_t len = strlen(name);

        uint32_t i = hash.lookup_chain(name);
        while(i != 0) {
            const Sym *sym = &symtab[i];
            if (!assert_in_bounds(sym)) return NULL;
            const char *sym_name = &strtab[sym->st_name];
            if (!assert_in_bounds(sym_name)) return NULL;
            if (strncmp(sym_name, name, len+1) == 0) {
                return sym;
            }
            i = hash.next_in_chain(i);
        }
        return NULL;
    }
};

void *mmap_dlopen(const void *code, size_t size) {
    dlib_t *dlib = (dlib_t *)malloc(sizeof(dlib_t));
    if (!dlib) {
        return NULL;
    }
    if (!dlib->parse((const char *)code, size)) {
        dlib->deinit();
        free(dlib);
        return NULL;
    }
    dlib->run_ctors();
    // Add this library to the list of loaded libs.
    dlib->next = loaded_libs;
    loaded_libs = dlib;

    return dlib;
}

void *mmap_dlsym(void *from, const char *name) {
    if (!from) return NULL;

    if ((from == RTLD_SELF) || (from == RTLD_DEFAULT)) {
        // Check all currently loaded libraries for a symbol
        void *S = halide_get_symbol(name);
        for (dlib_t *i = loaded_libs; i && !S; i = i->next) {
            // TODO: We really should only look in
            // libraries with an soname that is marked
            // DT_NEEDED in this library.
            S = mmap_dlsym(i, name);
        }
        return S;
    }

    dlib_t *dlib = (dlib_t *)from;
    const Sym *sym = dlib->find_symbol(name);
    if (!sym) return NULL;
    if (!dlib->symbol_is_defined(sym)) return NULL;
    return (void *)dlib->get_symbol_addr(sym);
}

int mmap_dlclose(void *dlib) {
    // Remove this library from the list of loaded libs.
    if (loaded_libs == dlib) {
        loaded_libs = loaded_libs->next;
    } else {
        dlib_t *prev = loaded_libs;
        while (prev && prev->next != dlib) {
            prev = prev->next;
        }
        if (prev) {
            dlib_t *new_next = prev->next ? prev->next->next : NULL;
            prev->next = new_next;
        }
    }
    dlib_t *d = (dlib_t *)dlib;
    d->run_dtors();
    d->deinit();
    free(d);
    return 0;
}
