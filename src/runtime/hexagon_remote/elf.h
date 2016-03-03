#ifndef HALIDE_RUNTIME_HEXAGON_ELF
#define HALIDE_RUNTIME_HEXAGON_ELF

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
        Word32_U6  = 0,
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
        uint32_t addend;

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
    uint16_t symbol_count() const { return symtab_shdr.size / symtab_shdr.entsize; }

    Shdr section_header(uint16_t i) {
        return Shdr((char *)section_headers.header + i*section_headers.entry_size);
    }

    void *section_ptr(const Shdr& shdr) {
        return (char *)base + shdr.offset;
    }

    const char *string(uint32_t index) {
        return (const char *)section_ptr(strtab_shdr) + index;
    }

    const char *section_name(const Shdr& shdr) {
        return (const char *)section_ptr(names_shdr) + shdr.name;
    }
    const char *section_name(uint16_t i) {
        return section_name(section_header(i));
    }

    Sym symbol(uint32_t i) {
        return Sym((char *)base + symtab_shdr.offset + i*symtab_shdr.entsize);
    }

    int find_section(const char *name) {
        for (uint16_t i = 0; i < section_count(); i++) {
            if (strcmp(section_name(i), name) == 0) {
                return i;
            }
        }
        return -1;
    }

    int find_symbol(const char *name) {
        for (uint32_t i = 0; i < symbol_count(); i++) {
            Sym sym(symbol(i));
            if (strcmp(string(sym.name), name) == 0) {
                return i;
            }
        }
        return -1;
    }

    AddrType symbol_value(uint32_t i) {
        return symbol(i).value;
    }
    void *symbol_address(uint32_t i) {
        return (char *)base + symbol_value(i);
    }

    void *symbol_address(const char *name) {
        int sym = find_symbol(name);
        if (sym == -1) {
            return 0;
        }
        return symbol_address(sym);
    }

    int init(void *obj) {
        const uint32_t elf_magic = 0x464c457f;
        const uint8_t elf_32bit = 1;
        const uint8_t elf_little_endian = 1;
        const uint8_t elf_version_ident = 1;

        base = obj;

        // Read the header.
        void *header = obj;
        if (read<uint32_t>(header) != elf_magic)
            return __LINE__;
        if (read<uint8_t>(header) != elf_32bit)
            return __LINE__;
        if (read<uint8_t>(header) != elf_little_endian)
            return __LINE__;
        if (read<uint8_t>(header) != elf_version_ident)
            return __LINE__;
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
        uint32_t G = 0;
        //uint32_t GOT = 0;
        //uint32_t GP = 0;
        //uint32_t L = 0;

        for (uint16_t i = 0; i < section_count(); i++) {
            Shdr rel_shdr = section_header(i);
            const char *rel_shdr_name = section_name(rel_shdr);

            // Find the section these relocations apply to.
            // TODO: Maybe there's a better way to do this than searching by name?
            int section_idx = -1;
            if (rel_shdr.type == SHT_REL) {
                section_idx = find_section(rel_shdr_name + 4);  // 4 = strlen(".rel")
            } else if (rel_shdr.type == SHT_RELA) {
                section_idx = find_section(rel_shdr_name + 5);  // 5 = strlen(".rela")
            } else {
                continue;
            }
            if (section_idx == -1) {
                // Couldn't find the section these relocations apply to.
                continue;
            }

            Shdr target_section = section_header((uint16_t)section_idx);
            //std::cout << rel_shdr_name << " -> " << section_name(target_section) << std::endl;

            for (uint32_t i = 0; i < rel_shdr.size / rel_shdr.entsize; i++) {
                Rel *rel = (Rel *)((char *)base + rel_shdr.offset + i * rel_shdr.entsize);
                int64_t P = B + target_section.offset + rel->offset;
                uint32_t S = symbol_value(rel->sym());
                if (rel_shdr.type == SHT_REL) {
                } else if (rel_shdr.type == SHT_RELA) {
                    uint32_t A = ((Rela *)rel)->addend;

                    uint32_t field;
                    int64_t result;
                    switch (rel->type()) {
                    case R_HEX_B22_PCREL:
                        field = Word32_B22;
                        result = (S + A - P) >> 2;
                        break;
                    case R_HEX_32:
                        field = Word32;
                        result = S + A;
                        break;
                    case R_HEX_B32_PCREL_X:
                        field = Word32_X26;
                        result = (S + A - P) >> 6;
                        break;
                    case R_HEX_B15_PCREL_X:
                        field = Word32_B15;
                        result = (S + A - P) & 0x3f;
                        break;
                    case R_HEX_6_PCREL_X:
                        field = Word32_U6;
                        result = (S + A - P);
                        break;
                    case R_HEX_GOT_32_6_X:
                        field = Word32_X26;
                        result = G >> 6;
                        break;
                    case R_HEX_GOT_11_X:
                        field = Word32_U6;
                        result = G;
                        break;
                    default:
                        return (int)rel->type() << 16;
                    }
                    uint32_t* target = (uint32_t*)((char *)base + target_section.offset + rel->offset);
                    *target = (*target & (~field)) | (result & field);
                }
            }
        }
        return 0;
    }
};

}  // namespace Elf

#endif
