#ifndef HALIDE_ELF_H
#define HALIDE_ELF_H

#include "Debug.h"

namespace Halide {

namespace Internal {

namespace Elf {

enum {
    EI_MAG = 0x464c457f,
    ELFCLASS32 = 1,
    ELFCLASS64 = 2,
    ELFDATA2LSB = 1,
    ELFDATA2MSB = 2,
};

enum {
    SHT_NULL = 0,
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_STRTAB = 3,
    SHT_RELA = 4,
    SHT_REL = 9,
    SHT_DYNSYM = 11,
};

enum {
    SHF_WRITE = 0x1,
    SHF_ALLOC = 0x2,
    SHF_EXECINSTR = 0x4,
    SHF_MASKPROC = 0xf0000000
};

enum {
    ET_NONE = 0,
    ET_REL = 1,
    ET_EXEC = 2,
    ET_DYN = 3,
    ET_CORE = 4,
    ET_LOPROC = 0xff00,
    ET_HIPROC = 0xffff
};

enum {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
};

#pragma pack(push, 1)

#define PACKED __attribute__((packed))

struct Ident {
    uint32_t magic;
    uint8_t bitness;
    uint8_t endianness;
    uint8_t version;
    uint8_t abi;
    uint8_t abi_version;
    uint8_t padding[7];
};

template <typename AddrType>
struct PACKED Ehdr{
    Ident e_ident;

    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    AddrType e_entry;
    AddrType e_phoff;
    AddrType e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

template <typename AddrType>
struct PACKED Phdr {
    uint32_t p_type;
    AddrType p_offset;
    AddrType p_vaddr;
    AddrType p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

template <typename AddrType>
struct PACKED Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    AddrType sh_addr;
    AddrType sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;

    uint32_t entry_count() const { return sh_size / sh_entsize; }
    uint32_t entry_offset(uint32_t i) const { return sh_offset + i*sh_entsize; }
};

template <typename AddrType>
struct PACKED Sym {
    uint32_t st_name;
    AddrType st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
};

struct PACKED Rel {
    uint32_t r_offset;
    uint32_t r_info;

    uint32_t sym() { return r_info >> 8; }
    uint8_t type() { return static_cast<uint8_t>(r_info & 0xFF); }
};

struct PACKED Rela : public Rel {
    int32_t r_addend;

    using Rel::r_offset;
    using Rel::r_info;
    using Rel::sym;
    using Rel::type;
};

enum {
    DT_NULL = 0,
    DT_HASH = 4,
    DT_STRTAB = 5,
    DT_SYMTAB = 6,
    DT_RELA = 7,
    DT_RELASZ = 8,
    DT_RELAENT = 9,
    DT_STRSZ = 10,
    DT_SYMENT = 11,
    DT_REL = 17,
    DT_RELSZ = 18,
    DT_RELENT = 19,
    DT_TEXTREL = 22,
};

enum {
    PF_X = 0x1,
    PF_W = 0x2,
    PF_R = 0x4,
};

template <typename AddrType>
struct Dyn {
    int32_t d_tag;
    union {
        uint32_t d_val;
        AddrType d_ptr;
    };

    static Dyn make_val(int32_t tag, uint32_t val) { Dyn ret; ret.d_tag = tag; ret.d_val = val; return ret; }
    static Dyn make_ptr(int32_t tag, AddrType ptr) { Dyn ret; ret.d_tag = tag; ret.d_ptr = ptr; return ret; }
};

#pragma pack(pop)

template<typename AddrType>
class Object {
public:
    std::vector<uint8_t>& obj;
    Object(std::vector<uint8_t>& obj) : obj(obj) {}

    uint8_t *base() { return obj.data(); }
    const uint8_t *base() const { return obj.data(); }

    Ehdr<AddrType> &header() { return *reinterpret_cast<Ehdr<AddrType> *>(obj.data()); }
    const Ehdr<AddrType> &header() const { return *reinterpret_cast<const Ehdr<AddrType> *>(obj.data()); }

    uint16_t section_count() const { return header().e_shnum; }
    uint16_t program_count() const { return header().e_phnum; }

    Shdr<AddrType> &section_header(uint16_t i) {
        return *reinterpret_cast<Shdr<AddrType> *>(base() + header().e_shoff + i*header().e_shentsize);
    }
    const Shdr<AddrType> &section_header(uint16_t i) const {
        return *reinterpret_cast<Shdr<AddrType> *>(base() + header().e_shoff + i*header().e_shentsize);
    }

    Phdr<AddrType> &program_header(uint16_t i) {
        return *reinterpret_cast<Phdr<AddrType> *>(base() + header().e_phoff + i*header().e_phentsize);
    }
    const Phdr<AddrType> &program_header(uint16_t i) const {
        return *reinterpret_cast<Phdr<AddrType> *>(base() + header().e_phoff + i*header().e_phentsize);
    }

    uint8_t *section_ptr(const Shdr<AddrType> &shdr) { return base() + shdr.offset; }
    const uint8_t *section_ptr(const Shdr<AddrType> &shdr) const { return base() + shdr.offset; }

    template <typename T>
    T& section_entry(const Shdr<AddrType>& shdr, uint32_t i) {
        return *reinterpret_cast<T*>(base() + shdr.entry_offset(i));
    }
    template <typename T>
    const T& section_entry(const Shdr<AddrType>& shdr, uint32_t i) const {
        return *reinterpret_cast<const T*>(base() + shdr.entry_offset(i));
    }

    Shdr<AddrType> *find_section_by_type(uint32_t type) {
        for (uint16_t i = 0; i < section_count(); i++) {
            if (section_header(i).sh_type == type) {
                return &section_header(i);
            }
        }
        return NULL;
    }

    Shdr<AddrType> *find_rel_for_section(uint16_t target_section) {
        for (uint16_t i = 0; i < section_count(); i++) {
            Shdr<AddrType> &shdr = section_header(i);
            if (shdr.sh_type == SHT_REL && shdr.sh_info == target_section) {
                return &shdr;
            }
        }
        return NULL;
    }

    Shdr<AddrType> *find_rela_for_section(uint16_t target_section) {
        for (uint16_t i = 0; i < section_count(); i++) {
            Shdr<AddrType> &shdr = section_header(i);
            if (shdr.sh_type == SHT_RELA && shdr.sh_info == target_section) {
                return &shdr;
            }
        }
        return NULL;
    }

    Shdr<AddrType> *get_string_table() {
        return find_section_by_type(SHT_STRTAB);
    }

    Shdr<AddrType> *get_symbol_table() {
        return find_section_by_type(SHT_SYMTAB);
    }

    const char *string(uint32_t index, const Shdr<AddrType>* strtab = NULL) const {
        if (strtab == NULL) strtab = get_string_table();
        internal_assert(strtab);
        return (const char *)section_ptr(*strtab) + index;
    }
};

}  // namespace Elf

}  // namespace Internal

}  // namespace Halide

#endif
