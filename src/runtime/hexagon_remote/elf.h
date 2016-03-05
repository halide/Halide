#ifndef HALIDE_RUNTIME_HEXAGON_ELF
#define HALIDE_RUNTIME_HEXAGON_ELF

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(x)
#endif

// An elf parser that does not allocate any non-stack memory :)

namespace Elf {

template <typename T>
T read(void *&ptr) {
    T result;
    memcpy(&result, ptr, sizeof(T));
    ptr = (char *)ptr + sizeof(T);
    return result;
}

template <typename T>
void read(void *&ptr, T &to) {
    to = read<T>(ptr);
}

void read_padding(void *& ptr, size_t padding) {
    ptr = (char *)ptr + padding;
}

template <typename AddrType>
class Object {
    struct HeaderTable {
        void *header;
        uint16_t count;
        uint16_t entry_size;

        HeaderTable() : header(0), count(0), entry_size(0) {}
    };

    enum {
        SHT_NULL = 0,
        SHT_SYMTAB = 2,
        SHT_STRTAB = 3,
        SHT_RELA = 4,
        SHT_REL = 9,
        SHT_DYNSYM = 11,
    };

    enum {
        R_HEX_B22_PCREL = 1,
        R_HEX_32 = 6,
        R_HEX_B32_PCREL_X = 16,
        R_HEX_B15_PCREL_X = 19,
        R_HEX_6_PCREL_X = 65,
        R_HEX_GOT_32_6_X = 69,
        R_HEX_GOT_11_X = 71,
    };

    enum {
        Word8      = 0xff,
        Word16     = 0xffff,
        Word32     = 0xffffffff,
        Word32_LO  = 0x00c03fff,
        Word32_HL  = 0x00c03fff,
        Word32_GP  = 0,
        Word32_B7  = 0x00001f18,
        Word32_B9  = 0x003000fe,
        Word32_B13 = 0x00202ffe,
        Word32_B15 = 0x00df20fe,
        Word32_B22 = 0x01ff3ffe,
        Word32_R6  = 0x000007e0,
        Word32_U6  = 0xffffffff,
        Word32_U16 = 0,
        Word32_X26 = 0x0fff3fff,
    };

    struct Shdr {
        uint32_t name;
        uint32_t type;
        uint32_t flags;
        AddrType addr;
        AddrType offset;
        uint32_t size;
        uint32_t link;
        uint32_t info;
        uint32_t addralign;
        uint32_t entsize;

        Shdr() {}
        Shdr(void *entry) {
            read(entry, name);
            read(entry, type);
            read(entry, flags);
            read(entry, addr);
            read(entry, offset);
            read(entry, size);
            read(entry, link);
            read(entry, info);
            read(entry, addralign);
            read(entry, entsize);
        }
    };

    struct Sym {
        uint32_t name;
        AddrType value;
        uint32_t size;
        uint8_t info;
        uint8_t other;
        uint16_t shndx;

        Sym(void *sym) {
            read(sym, name);
            read(sym, value);
            read(sym, size);
            read(sym, info);
            read(sym, other);
            read(sym, shndx);
        };
    };

    struct Rel {
        uint32_t offset;
        uint32_t info;

        Rel(void *rel) {
            read(rel, offset);
            read(rel, info);
        }

        uint32_t sym() { return info >> 8; }
        uint8_t type() { return static_cast<uint8_t>(info & 0xFF); }
    };

    struct Rela : public Rel {
        int32_t addend;

        using Rel::offset;
        using Rel::info;
        using Rel::sym;
        using Rel::type;

        Rela(void *rel) {
            read(rel, offset);
            read(rel, info);
            read(rel, addend);
        }
    };

    void *base;

    HeaderTable section_headers;
    HeaderTable program_headers;

    Shdr names_shdr;
    Shdr symtab_shdr;
    Shdr strtab_shdr;
    Shdr dynsym_shdr;

public:
    uint16_t section_count() const { return section_headers.count; }
    uint16_t program_count() const { return program_headers.count; }
    uint16_t symbol_count(const Shdr* symtab = 0) const {
        if (symtab == 0) symtab = &symtab_shdr;
        return symtab->size / symtab->entsize;
    }

    Shdr section_header(uint16_t i) {
        return Shdr((char *)section_headers.header + i*section_headers.entry_size);
    }

    void *section_ptr(const Shdr& shdr) {
        return (char *)base + shdr.offset;
    }

    const char *string(uint32_t index, const Shdr* strtab = 0) {
        if (strtab == 0) strtab = &strtab_shdr;
        return (const char *)section_ptr(*strtab) + index;
    }

    const char *section_name(const Shdr& shdr) {
        return (const char *)section_ptr(names_shdr) + shdr.name;
    }
    const char *section_name(uint16_t i) {
        return section_name(section_header(i));
    }

    Sym symbol(uint32_t i, const Shdr* symtab = 0) {
        if (symtab == 0) symtab = &symtab_shdr;
        return Sym((char *)base + symtab->offset + i*symtab->entsize);
    }

    int find_section(const char *name) {
        for (uint16_t i = 0; i < section_count(); i++) {
            if (strcmp(section_name(i), name) == 0) {
                return i;
            }
        }
        DEBUG_PRINT("Failed to find section:");
        DEBUG_PRINT(name);
        return -1;
    }

    int find_symbol(const char *name, const Shdr* symtab = 0) {
        if (symtab == 0) symtab = &symtab_shdr;
        Shdr strtab = section_header(symtab->link);
        for (uint32_t i = 0; i < symbol_count(symtab); i++) {
            Sym sym(symbol(i, symtab));
            if (strcmp(string(sym.name, &strtab), name) == 0) {
                return i;
            }
        }
        DEBUG_PRINT("Failed to find symbol:");
        DEBUG_PRINT(name);
        return -1;
    }

    AddrType symbol_offset(uint32_t i, const Shdr* symtab = 0) {
        Sym sym = symbol(i, symtab);
        return sym.value + section_header(sym.shndx).offset;
    }
    void *symbol_address(uint32_t i, const Shdr* symtab = 0) {
        return (char *)base + symbol_offset(i, symtab);
    }

    void *symbol_address(const char *name, const Shdr* symtab = 0) {
        int sym = find_symbol(name, symtab);
        if (sym == -1) {
            return 0;
        }
        return symbol_address(sym, symtab);
    }

    int init(void *obj) {
        const uint32_t elf_magic = 0x464c457f;
        const uint8_t elf_32bit = 1;
        const uint8_t elf_little_endian = 1;
        const uint8_t elf_version_ident = 1;

        base = obj;

        // Read the header.
        void *header = obj;
        if (read<uint32_t>(header) != elf_magic) {
            DEBUG_PRINT("Not an ELF object");
            return -1;
        }
        if (read<uint8_t>(header) != elf_32bit) {
            DEBUG_PRINT("Not a 32-bit ELF object");
            return -1;
        }
        if (read<uint8_t>(header) != elf_little_endian) {
            DEBUG_PRINT("Not a little endian ELF object");
            return -1;
        }
        if (read<uint8_t>(header) != elf_version_ident) {
            DEBUG_PRINT("Not a version 1 ELF object");
            return -1;
        }
        read<uint8_t>(header);  // abi
        read<uint8_t>(header);  // abi version
        read_padding(header, 7);

        read<uint16_t>(header);  // type, {1, 2, 3, 4} -> {relocatable, executable, shared, core}
        read<uint16_t>(header);  // machine
        read<uint32_t>(header);  // version
        read<AddrType>(header); // entry point
        program_headers.header = (char *)base + read<AddrType>(header);
        section_headers.header = (char *)base + read<AddrType>(header);
        read<uint32_t>(header);  // flags
        read<uint16_t>(header);  // header size
        program_headers.entry_size = read<uint16_t>(header);
        program_headers.count = read<uint16_t>(header);
        section_headers.entry_size = read<uint16_t>(header);
        section_headers.count = read<uint16_t>(header);
        names_shdr = section_header(read<uint16_t>(header));

        for (uint16_t i = 0; i < section_count(); i++) {
            Shdr shdr = section_header(i);
            switch (shdr.type) {
            case SHT_SYMTAB: symtab_shdr = shdr; break;
            case SHT_STRTAB: strtab_shdr = shdr; break;
            case SHT_DYNSYM: dynsym_shdr = shdr; break;
            }
        }
        return 0;
    }

    int do_relocations() {
        AddrType B = (AddrType)(uintptr_t)base;
        //uint32_t GOT = (AddrType)(uintptr_t)symbol_address("_GLOBAL_OFFSET_TABLE_");
        //uint32_t GP = 0;
        //uint32_t L = 0;

        // Since we are not doing any real dynamic linking, we just
        // store a list of the global offsets allocated on the stack
        // here.
        uint32_t *global_offsets = (uint32_t *)__builtin_alloca(symbol_count() * sizeof(uint32_t));
        for (uint32_t i = 0; i < symbol_count(); i++) {
            global_offsets[i] = -1;
        }
        uint32_t global_offset_allocator = 0;

        for (uint16_t i = 0; i < section_count(); i++) {
            Shdr rel_shdr = section_header(i);
            if (rel_shdr.type == SHT_REL) {
            } else if (rel_shdr.type == SHT_RELA) {
            } else {
                continue;
            }
            Shdr target_shdr = section_header(rel_shdr.info);
            Shdr symtab = section_header(rel_shdr.link);

            for (uint32_t i = 0; i < rel_shdr.size / rel_shdr.entsize; i++) {
                Rel *rel = (Rel *)((char *)base + rel_shdr.offset + i * rel_shdr.entsize);
                AddrType P = B + target_shdr.offset + rel->offset;
                uint32_t S = symbol_offset(rel->sym(), &symtab);
                int32_t A = rel_shdr.type == SHT_RELA ? ((Rela *)rel)->addend : 0;
                uint32_t& G = global_offsets[rel->sym()];
                if (G == -1) {
                    G = global_offset_allocator;
                    global_offset_allocator += sizeof(uint32_t);
                }

                uint32_t mask;
                uint32_t field;
                uint32_t result;
                switch (rel->type()) {
                case R_HEX_B22_PCREL:
                    mask = field = Word32_B22;
                    result = static_cast<uint32_t>((static_cast<int32_t>(S) + A - static_cast<int32_t>(P)) >> 2);
                    break;
                case R_HEX_32:
                    mask = field = Word32;
                    result = S + A;
                    break;
                case R_HEX_B32_PCREL_X:
                    mask = field = Word32_X26;
                    result = static_cast<uint32_t>((static_cast<int32_t>(S) + A - static_cast<int32_t>(P)) >> 6);
                    break;
                case R_HEX_B15_PCREL_X:
                    mask = field = Word32_B15;
                    result = static_cast<uint32_t>((static_cast<int32_t>(S) + A - static_cast<int32_t>(P)) & 0x3f);
                    break;
                case R_HEX_6_PCREL_X:
                    mask = field = Word32_U6;
                    result = (S + A - P);
                    break;
                case R_HEX_GOT_32_6_X:
                    mask = field = Word32_X26;
                    result = static_cast<uint32_t>(static_cast<int32_t>(G) >> 6);
                    break;
                case R_HEX_GOT_11_X:
                    mask = 0;
                    field = Word32_U6;
                    result = G;
                    break;
                default:
                    DEBUG_PRINT("Unknown relocation type");
                    return (int)rel->type();
                }
                uint32_t* target = (uint32_t*)((char *)base + target_shdr.offset + rel->offset);
                *target = (*target & (~mask)) | (result & field);
            }
        }
        return 0;
    }
};

}  // namespace Elf

#endif
