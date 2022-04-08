#include "Elf.h"
#include "Debug.h"
#include "Error.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <memory>

namespace Halide {
namespace Internal {
namespace Elf {

namespace {

// http://www.skyfree.org/linux/references/ELF_Format.pdf

enum : uint32_t {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
    PT_LOPROC = 0x70000000,
    PT_HIPROC = 0x7fffffff,
};

enum : uint32_t {
    PF_X = 1,
    PF_W = 2,
    PF_R = 4,
    PF_MASKOS = 0x0ff00000,
    PF_MASKPROC = 0xf0000000,
};

enum : uint32_t {
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

enum : uint32_t {
    STN_UNDEF = 0
};

const char elf_magic[] = {0x7f, 'E', 'L', 'F'};

template<int bits>
struct Types;

template<>
struct Types<32> {
    typedef uint32_t addr_t;
    typedef int32_t addr_off_t;
};

template<typename T>
struct Ehdr {
    typedef typename T::addr_t addr_t;
    typedef typename T::addr_off_t addr_off_t;

    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    addr_t e_entry;
    addr_t e_phoff;
    addr_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

template<typename T>
struct Phdr {
    typedef typename T::addr_t addr_t;
    typedef typename T::addr_off_t addr_off_t;

    uint32_t p_type;
    uint32_t p_offset;
    addr_t p_vaddr;
    addr_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

template<typename T>
struct Shdr {
    typedef typename T::addr_t addr_t;
    typedef typename T::addr_off_t addr_off_t;

    uint32_t sh_name;
    uint32_t sh_type;
    addr_t sh_flags;
    addr_t sh_addr;
    addr_t sh_offset;
    addr_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    addr_t sh_addralign;
    addr_t sh_entsize;
};

template<typename T>
struct Rel {
    typedef typename T::addr_t addr_t;
    typedef typename T::addr_off_t addr_off_t;

    addr_t r_offset;
    addr_t r_info;

    uint32_t r_type() const {
        if (sizeof(addr_t) == 8) {
            return r_info & 0xffffffff;
        } else {
            return r_info & 0xff;
        }
    }

    uint32_t r_sym() const {
        if (sizeof(addr_t) == 8) {
            return (uint64_t)r_info >> 32;
        } else {
            return r_info >> 8;
        }
    }

    static addr_t make_info(uint32_t type, uint32_t sym) {
        if (sizeof(addr_t) == 8) {
            return (uint64_t)type | ((uint64_t)sym << 32);
        } else {
            return (type & 0xff) | (sym << 8);
        }
    }

    void set_r_type(uint32_t type) {
        r_info = make_info(type, r_sym());
    }

    void set_r_sym(uint32_t sym) {
        r_info = make_info(r_type(), sym);
    }

    Rel(addr_t offset, addr_t info)
        : r_offset(offset), r_info(info) {
    }

    Rel(addr_t offset, uint32_t type, uint32_t sym)
        : r_offset(offset), r_info(make_info(type, sym)) {
    }
};

template<typename T>
struct Rela : public Rel<T> {
    typedef typename T::addr_t addr_t;
    typedef typename T::addr_off_t addr_off_t;

    addr_off_t r_addend;

    Rela(addr_t offset, addr_t info, addr_off_t addend)
        : Rel<T>(offset, info), r_addend(addend) {
    }

    Rela(addr_t offset, uint32_t type, uint32_t sym, addr_off_t addend)
        : Rel<T>(offset, type, sym), r_addend(addend) {
    }
};

template<typename T>
struct Sym;

template<>
struct Sym<Types<32>> {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;

    uint8_t get_binding() const {
        return st_info >> 4;
    }
    uint8_t get_type() const {
        return st_info & 0xf;
    }

    static uint8_t make_info(uint8_t binding, uint8_t type) {
        return (binding << 4) | (type & 0xf);
    }

    void set_binding(uint8_t binding) {
        st_info = make_info(binding, get_type());
    }
    void set_type(uint8_t type) {
        st_info = make_info(get_binding(), type);
    }
};

template<typename T>
struct Dyn {
    typedef typename T::addr_t addr_t;
    typedef typename T::addr_off_t addr_off_t;

    uint32_t d_tag;
    union {
        uint32_t d_val;
        addr_t d_ptr;
    };
};

class StringTable {
    // TODO: We could be smarter and find substrings in the existing
    // table, not just whole strings. It would probably be fine to just
    // put every substring of each new string into the cache.
    std::map<std::string, uint32_t> cache;

public:
    std::vector<char> table;

    StringTable() {
        // For our cache to work, we need something in the table to
        // start with so index 0 isn't valid (it will be the empty
        // string).
        table.push_back(0);
    }

    uint32_t get(const std::string &str) {
        uint32_t &index = cache[str];
        if (index == 0) {
            index = table.size();
            table.insert(table.end(), str.begin(), str.end());
            table.push_back(0);
        }
        return index;
    }
};

const char *assert_string_valid(const char *name, const char *data, size_t size) {
    internal_assert(data <= name && name + strlen(name) + 1 <= data + size);
    return name;
}

template<typename T>
void append_object(std::vector<char> &buf, const T &data) {
    buf.insert(buf.end(), (const char *)&data, (const char *)(&data + 1));
}

template<typename It>
void append(std::vector<char> &buf, It begin, It end) {
    buf.reserve(buf.size() + std::distance(begin, end) * sizeof(*begin));
    for (It i = begin; i != end; i++) {
        append_object(buf, *i);
    }
}

void append_zeros(std::vector<char> &buf, size_t count) {
    buf.insert(buf.end(), count, (char)0);
}

void append_padding(std::vector<char> &buf, size_t alignment) {
    buf.resize((buf.size() + alignment - 1) & ~(alignment - 1));
}

// Cast one type to another, asserting that the type is in the range
// of the target type.
template<typename T, typename U>
T safe_cast(U x) {
    internal_assert(std::numeric_limits<T>::min() <= x && x <= std::numeric_limits<T>::max());
    return static_cast<T>(x);
}

// Assign a type from a potentially different type, using safe_cast
// above to validate the assignment.
template<typename T, typename U>
void safe_assign(T &dest, U src) {
    dest = safe_cast<T>(src);
}

unsigned long elf_hash(const char *name) {
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

template<typename T>
std::unique_ptr<Object> parse_object_internal(const char *data, size_t size) {
    Ehdr<T> header = *(const Ehdr<T> *)data;
    internal_assert(memcmp(header.e_ident, elf_magic, sizeof(elf_magic)) == 0);
    internal_assert(header.e_type == Object::ET_REL || header.e_type == Object::ET_DYN);

    std::unique_ptr<Object> obj(new Object());
    obj->set_type((Object::Type)header.e_type)
        .set_machine(header.e_machine)
        .set_version(header.e_version)
        .set_entry(header.e_entry)
        .set_flags(header.e_flags);

    auto get_section_header = [&](int idx) -> const Shdr<T> * {
        const char *at = data + header.e_shoff + idx * header.e_shentsize;
        internal_assert(data <= at && at + sizeof(Shdr<T>) <= data + size)
            << "Section header out of bounds.\n";
        return (const Shdr<T> *)at;
    };

    // Find the string table.
    const char *strings = nullptr;
    for (int i = 0; i < header.e_shnum; i++) {
        const Shdr<T> *sh = get_section_header(i);
        if (sh->sh_type == Section::SHT_STRTAB) {
            internal_assert(!strings) << "Found more than one string table.\n";
            strings = data + sh->sh_offset;
            internal_assert(data <= strings && strings + sh->sh_size <= data + size);
        }
    }
    internal_assert(strings)
        << "String table not found.\n";

    // Load the rest of the sections.
    std::map<int, Section *> section_map;
    for (uint16_t i = 0; i < header.e_shnum; i++) {
        const Shdr<T> *sh = get_section_header(i);
        if (sh->sh_type != Section::SHT_SYMTAB && sh->sh_type != Section::SHT_STRTAB &&
            sh->sh_type != Section::SHT_REL && sh->sh_type != Section::SHT_RELA) {
            const char *name = assert_string_valid(&strings[sh->sh_name], data, size);
            auto section = obj->add_section(name, (Section::Type)sh->sh_type);
            section->set_flags(sh->sh_flags)
                .set_size(sh->sh_size)
                .set_alignment(sh->sh_addralign);
            if (sh->sh_type == Section::SHT_NOBITS) {
                // This section doesn't have any data to load.
            } else if (sh->sh_type == Section::SHT_NULL) {
            } else {
                const char *sh_data = data + sh->sh_offset;
                internal_assert(data <= sh_data && sh_data + sh->sh_size <= data + size);
                section->set_contents(sh_data, sh_data + sh->sh_size);
            }
            section_map[i] = &*section;
        }
    }

    // Find and load the symbols.
    std::map<int, Symbol *> symbol_map;
    for (uint16_t i = 0; i < header.e_shnum; i++) {
        const Shdr<T> *sh = get_section_header(i);
        if (sh->sh_type == Section::SHT_SYMTAB) {
            internal_assert(sh->sh_entsize == sizeof(Sym<T>));
            // Skip symbol 0, which is a null symbol.
            for (uint64_t j = 1; j < sh->sh_size / sizeof(Sym<T>); ++j) {
                const char *sym_ptr = data + sh->sh_offset + j * sizeof(Sym<T>);
                internal_assert(data <= sym_ptr && sym_ptr + sizeof(Sym<T>) <= data + size);
                const Sym<T> &sym = *(const Sym<T> *)sym_ptr;
                const char *name = assert_string_valid(&strings[sym.st_name], data, size);
                auto symbol = obj->add_symbol(name);
                symbol->set_type((Symbol::Type)sym.get_type())
                    .set_binding((Symbol::Binding)sym.get_binding())
                    .set_visibility((Symbol::Visibility)sym.st_other);
                if (sym.st_shndx != 0) {
                    symbol->define(section_map[sym.st_shndx], sym.st_value, sym.st_size);
                }
                symbol_map[j] = &*symbol;
            }
        }
    }

    // Load relocations.
    for (uint16_t i = 0; i < header.e_shnum; i++) {
        const Shdr<T> *sh = get_section_header(i);
        internal_assert(sh->sh_type != Section::SHT_REL) << "Section::SHT_REL not supported\n";
        if (sh->sh_type == Section::SHT_RELA) {
            const char *name = assert_string_valid(&strings[sh->sh_name], data, size);
            internal_assert(strncmp(name, ".rela.", 6) == 0);
            internal_assert(sh->sh_entsize == sizeof(Rela<T>));
            auto to_relocate = obj->find_section(name + 5);
            internal_assert(to_relocate != obj->sections_end());
            // TODO: This assert should work, but it seems like this
            // isn't a reliable test. We rely on the names intead.
            // internal_assert(&*to_relocate == section_map[sh->sh_link]);
            for (uint64_t i = 0; i < sh->sh_size / sh->sh_entsize; i++) {
                const char *rela_ptr = data + sh->sh_offset + i * sh->sh_entsize;
                internal_assert(data <= rela_ptr && rela_ptr + sizeof(Rela<T>) <= data + size);
                const Rela<T> &rela = *(const Rela<T> *)rela_ptr;
                Relocation reloc(rela.r_type(), rela.r_offset, rela.r_addend, symbol_map[rela.r_sym()]);
                to_relocate->add_relocation(reloc);
            }
        }
    }

    return obj;
}
}  // namespace

std::unique_ptr<Object> Object::parse_object(const char *data, size_t size) {
    return parse_object_internal<Types<32>>(data, size);
}

Object::symbol_iterator Object::add_symbol(const std::string &name) {
    syms.emplace_back(name);
    return std::prev(syms.end());
}

Object::section_iterator Object::add_section(const std::string &name, Section::Type type) {
    secs.emplace_back(name, type);
    return std::prev(secs.end());
}

Object::section_iterator Object::find_section(const std::string &name) {
    for (section_iterator i = sections_begin(); i != sections_end(); ++i) {
        if (i->get_name() == name) {
            return i;
        }
    }
    return sections_end();
}

Object::symbol_iterator Object::find_symbol(const std::string &name) {
    for (symbol_iterator i = symbols_begin(); i != symbols_end(); ++i) {
        if (i->get_name() == name) {
            return i;
        }
    }
    return symbols_end();
}

Object::const_symbol_iterator Object::find_symbol(const std::string &name) const {
    for (const_symbol_iterator i = symbols_begin(); i != symbols_end(); ++i) {
        if (i->get_name() == name) {
            return i;
        }
    }
    return symbols_end();
}

Object::section_iterator Object::merge_sections(const std::vector<section_iterator> &to_merge) {
    internal_assert(!to_merge.empty());
    section_iterator merged = *to_merge.begin();

    std::vector<char> contents = merged->get_contents();

    for (auto i = to_merge.begin() + 1; i != to_merge.end(); ++i) {
        section_iterator s = *i;
        internal_assert(s->get_type() == merged->get_type());

        // Make the new text section have an alignment that
        // satisfies all sections. This should be gcd, not max,
        // but we assume that all of the alignments are powers of
        // 2.
        uint64_t alignment = std::max(merged->get_alignment(), s->get_alignment());
        merged->set_alignment(alignment);

        append_padding(contents, alignment);
        // The offset of the section in the new merged section.
        uint64_t offset = contents.size();
        append(contents, s->contents_begin(), s->contents_end());

        for (auto j = s->relocations_begin(); j != s->relocations_end(); j++) {
            Elf::Relocation reloc = *j;
            reloc.set_offset(reloc.get_offset() + offset);
            merged->add_relocation(reloc);
        }

        // Find all of the symbols that were defined in this section, and update them.
        for (auto j = symbols_begin(); j != symbols_end(); j++) {
            if (j->get_section() == &*s) {
                j->define(&*merged, j->get_offset() + offset, j->get_size());
            }
        }
    }

    merged->set_contents(contents.begin(), contents.end());

    // Remove all of the sections we merged.
    for (auto i = to_merge.begin() + 1; i != to_merge.end(); ++i) {
        erase_section(*i);
    }

    return merged;
}

Object::section_iterator Object::merge_text_sections() {
    std::vector<section_iterator> text_sections;
    for (auto i = sections_begin(); i != sections_end(); i++) {
        if (i->get_type() == Section::SHT_PROGBITS && starts_with(i->get_name(), ".text")) {
            text_sections.push_back(i);
        }
    }
    section_iterator text = merge_sections(text_sections);
    text->set_name(".text");
    return text;
}

template<typename T>
std::vector<char> write_shared_object_internal(Object &obj, Linker *linker, const std::vector<std::string> &dependencies,
                                               const std::string &soname) {
    typedef typename T::addr_t addr_t;

    // The buffer we will be writing to.
    std::vector<char> output;

    // Declare the things we need to put in the shared object.
    Ehdr<T> ehdr;
    std::array<Phdr<T>, 3> phdrs;
    memset(&ehdr, 0, sizeof(ehdr));
    memset(&phdrs[0], 0, sizeof(phdrs));
    auto &text_phdr = phdrs[0];
    auto &data_phdr = phdrs[1];
    auto &dyn_phdr = phdrs[2];

    // The text program header starts at the beginning of the object.
    text_phdr.p_type = PT_LOAD;
    text_phdr.p_flags = PF_X | PF_R;
    text_phdr.p_offset = 0;
    text_phdr.p_align = 4096;

    // We need to build a string table as we go.
    StringTable strings;

    // And build a list of section headers.
    std::vector<Shdr<T>> shdrs;
    // Add the null section now.
    Shdr<T> sh_null;
    memset(&sh_null, 0, sizeof(sh_null));
    shdrs.push_back(sh_null);

    // We also need a mapping of section objects to section headers.
    std::map<const Section *, uint16_t> section_idxs;

    // Define a helper function to write a section to the shared
    // object, making a section header for it.
    auto write_section = [&](const Section &s, uint64_t entsize) {
        uint64_t alignment = s.get_alignment();

        append_padding(output, alignment);
        uint64_t offset = output.size();
        debug(2) << "Writing section " << s.get_name() << " at offset " << offset << "\n";
        const std::vector<char> &contents = s.get_contents();
        append(output, contents.begin(), contents.end());
        if (contents.size() < s.get_size()) {
            append_zeros(output, s.get_size() - contents.size());
        }
        append_padding(output, alignment);

        Shdr<T> shdr;
        shdr.sh_name = strings.get(s.get_name());
        safe_assign(shdr.sh_type, s.get_type());
        safe_assign(shdr.sh_flags, s.get_flags());
        safe_assign(shdr.sh_offset, offset);
        safe_assign(shdr.sh_addr, offset);
        safe_assign(shdr.sh_size, s.get_size());
        safe_assign(shdr.sh_addralign, alignment);

        shdr.sh_link = 0;
        shdr.sh_info = 0;
        safe_assign(shdr.sh_entsize, entsize);

        uint16_t shndx = safe_cast<uint16_t>(shdrs.size());
        section_idxs[&s] = shndx;
        shdrs.push_back(shdr);
        return shndx;
    };

    // And a helper to get the offset we've given a section.
    auto get_section_offset = [&](const Section &s) -> uint64_t {
        uint16_t idx = section_idxs[&s];
        return shdrs[idx].sh_offset;
    };

    // We need to define the GOT symbol.
    uint64_t max_got_size = obj.symbols_size() * 2 * sizeof(addr_t);
    Section got(".got", Section::SHT_PROGBITS);
    got.set_alignment(4);
    got.set_size(max_got_size);
    got.set_flags(Section::SHF_ALLOC);
    Symbol got_sym("_GLOBAL_OFFSET_TABLE_");
    got_sym.define(&got, 0, max_got_size);
    got_sym.set_type(Symbol::STT_OBJECT);
    got_sym.set_visibility(Symbol::STV_HIDDEN);
    Symbol dynamic_sym("_DYNAMIC");
    dynamic_sym.define(&got, 0, 4);
    dynamic_sym.set_type(Symbol::STT_OBJECT);
    got.append_contents((addr_t)0);
    // On some platforms, GOT slots 1 and 2 are also reserved.
    got.append_contents((addr_t)0);
    got.append_contents((addr_t)0);

    // Since we can't change the object, start a map of all of the
    // symbols that we can mutate. If a symbol from the object is a
    // key in this map, we use the mapped value instead.
    std::map<const Symbol *, const Symbol *> symbols;
    symbols[&dynamic_sym] = &dynamic_sym;

    Object::section_iterator iter_dtors = obj.find_section(".dtors");
    Symbol dtor_list_sym("__DTOR_LIST__");
    if (iter_dtors != obj.sections_end()) {
        Section *dtors = &(*iter_dtors);
        dtor_list_sym.define(dtors, 0, 0);
        dtor_list_sym.set_type(Symbol::STT_NOTYPE);
        dtor_list_sym.set_visibility(Symbol::STV_DEFAULT);
        dtor_list_sym.set_binding(Symbol::STB_GLOBAL);
    }

    Object::section_iterator iter_ctors = obj.find_section(".ctors");
    Symbol ctor_end_sym("__CTOR_END__");
    if (iter_ctors != obj.sections_end()) {
        Section *ctors = &(*iter_ctors);
        internal_assert(ctors->get_size() == ctors->contents_size())
            << "There should no padding at the end of the .ctors section\n";
        ctor_end_sym.define(ctors, ctors->get_size(), 0);
        ctor_end_sym.set_type(Symbol::STT_NOTYPE);
        ctor_end_sym.set_visibility(Symbol::STV_DEFAULT);
        ctor_end_sym.set_binding(Symbol::STB_GLOBAL);
    }

    for (const Symbol &i : obj.symbols()) {
        if (i.get_name() == "_GLOBAL_OFFSET_TABLE_") {
            symbols[&i] = &got_sym;
        } else if (i.get_name() == "__DTOR_LIST__") {
            // It is our job to create this symbol. So, a defined __DTOR_LIST__
            // symbol shouldn't be present already.
            internal_assert(!i.is_defined()) << "__DTOR_LIST__ already defined\n";
            symbols[&i] = &dtor_list_sym;
        } else if (i.get_name() == "__CTOR_END__") {
            internal_assert(!i.is_defined()) << "__CTOR_END__ already defined\n";
            symbols[&i] = &ctor_end_sym;
        } else {
            symbols[&i] = &i;
        }
    }
    // Get a symbol from a relocation, accounting for the symbol map
    // above.
    auto get_symbol = [&](const Relocation &r) {
        const Symbol *sym = r.get_symbol();
        if (!sym) {
            return sym;
        }

        auto i = symbols.find(sym);
        if (i != symbols.end()) {
            return i->second;
        }
        return sym;
    };

    // Check if a relocation needs a PLT entry, which adds some
    // additional conditions on top of what the linker implementation
    // wants.
    auto needs_plt_entry = [&](const Relocation &r) {
        const Symbol *s = get_symbol(r);
        if (!s || s->is_defined()) {
            return false;
        }

        if (s->get_type() != Symbol::STT_NOTYPE) {
            return false;
        }

        return linker->needs_plt_entry(r);
    };

    // We need to build the PLT, so it can be positioned along with
    // the rest of the text sections.
    Section plt(".plt", Section::SHT_PROGBITS);
    plt.set_alignment(16);
    plt.set_flags(Section::SHF_ALLOC | Section::SHF_EXECINSTR);
    std::list<Symbol> plt_symbols;
    std::map<const Symbol *, const Symbol *> plt_defs;
    // Hack: We're defining the global offset table, so it shouldn't be treated as an external symbol.
    plt_defs[&got_sym] = &got_sym;
    for (const Section &s : obj.sections()) {
        for (const Relocation &r : s.relocations()) {
            if (!needs_plt_entry(r)) {
                continue;
            }

            const Symbol *sym = get_symbol(r);
            const Symbol *&plt_def = plt_defs[sym];
            if (plt_def) {
                // We already made a PLT entry for this symbol.
                continue;
            }

            debug(2) << "Defining PLT entry for " << sym->get_name() << "\n";
            plt_symbols.push_back(linker->add_plt_entry(*sym, plt, got, got_sym));

            plt_def = &plt_symbols.back();
            symbols[plt_def] = plt_def;
        }
    }

    // Start placing the sections into the shared object.

    // Leave room for the header, and program headers at the beginning of the file.
    append_zeros(output, sizeof(ehdr));
    append_zeros(output, sizeof(phdrs[0]) * 3);

    // We need to perform the relocations. To do that, we need to position the sections
    // where they will go in the final shared object.
    write_section(plt, 0);
    for (const Section &s : obj.sections()) {
        if (s.is_alloc() && !s.is_writable()) {
            write_section(s, 0);
        }
    }
    append_padding(output, 4096);
    text_phdr.p_filesz = output.size() - text_phdr.p_offset;

    data_phdr.p_type = PT_LOAD;
    data_phdr.p_flags = PF_W | PF_R;
    safe_assign(data_phdr.p_offset, output.size());
    data_phdr.p_align = 4096;
    for (const Section &s : obj.sections()) {
        if (s.is_alloc() && s.is_writable()) {
            write_section(s, 0);
        }
    }

    // The got will be written again later, after we add entries to it.
    write_section(got, 0);

    /// Now that we've written the sections that define symbols, we
    // can generate the symbol table.
    Section symtab(".symtab", Section::SHT_SYMTAB);
    symtab.set_alignment(4);
    symtab.set_flag(Section::SHF_ALLOC);
    std::vector<Sym<T>> syms;
    Sym<T> undef_sym;
    memset(&undef_sym, 0, sizeof(undef_sym));
    syms.push_back(undef_sym);

    // Ensure that we output the symbols deterministically, since a map of pointers
    // will vary in ordering from run to tun.
    std::vector<std::pair<const Symbol *, const Symbol *>> sorted_symbols;
    for (const auto &i : symbols) {
        sorted_symbols.emplace_back(i);
    }
    std::sort(sorted_symbols.begin(), sorted_symbols.end(),
              [&](const std::pair<const Symbol *, const Symbol *> &lhs, const std::pair<const Symbol *, const Symbol *> &rhs) {
                  return lhs.first->get_name() < rhs.first->get_name();
              });

    std::map<const Symbol *, uint16_t> symbol_idxs;
    uint64_t local_count = 0;
    for (bool is_local : {true, false}) {
        for (const auto &i : sorted_symbols) {
            const Symbol *s = i.second;
            if ((s->get_binding() == Symbol::STB_LOCAL) != is_local) {
                continue;
            }

            uint64_t value = s->get_offset();
            // In shared objects, the symbol value is a virtual address,
            // not a section offset.
            if (s->is_defined()) {
                value += get_section_offset(*s->get_section());
            }
            Sym<T> sym;
            safe_assign(sym.st_name, strings.get(s->get_name()));
            safe_assign(sym.st_value, value);
            safe_assign(sym.st_size, s->get_size());
            sym.set_type(s->get_type());
            sym.set_binding(s->get_binding());
            safe_assign(sym.st_other, s->get_visibility());
            sym.st_shndx = section_idxs[s->get_section()];

            safe_assign(symbol_idxs[s], syms.size());
            syms.push_back(sym);
        }
        if (is_local) {
            local_count = syms.size();
        }
    }
    symtab.set_contents(syms);
    uint16_t symtab_idx = write_section(symtab, sizeof(syms[0]));
    safe_assign(shdrs[symtab_idx].sh_info, local_count);

    // Also write the symbol table as SHT_DYNSYM.
    Section dynsym = symtab;
    dynsym.set_name(".dynsym");
    dynsym.set_type(Section::SHT_DYNSYM);
    uint16_t dynsym_idx = write_section(dynsym, sizeof(syms[0]));
    shdrs[dynsym_idx].sh_info = local_count;

    // We really do need to make a hash table. Make a trivial one with one bucket.
    Section hash(".hash", Section::SHT_HASH);
    hash.set_alignment(4);
    hash.set_flag(Section::SHF_ALLOC);
    size_t sym_count = syms.size();
    // TODO: Fix non-trivial hash tables so they work with dlsym.
    size_t bucket_count = 1;
    std::vector<uint32_t> hash_table(bucket_count + sym_count + 2);
    safe_assign(hash_table[0], bucket_count);
    safe_assign(hash_table[1], sym_count);
    uint32_t *buckets = &hash_table[2];
    uint32_t *chains = buckets + bucket_count;
    for (size_t i = 0; i < sym_count; i++) {
        const char *name = &strings.table[syms[i].st_name];
        uint32_t hash = elf_hash(name) % bucket_count;
        chains[i] = buckets[hash];
        safe_assign(buckets[hash], i);
    }
    hash.set_contents(hash_table);
    uint16_t hash_idx = write_section(hash, sizeof(hash_table[0]));

    auto do_relocations = [&](const Section &s) {
        debug(2) << "Processing relocations for section " << s.get_name() << "\n";
        for (const Relocation &r : s.relocations()) {
            const Symbol *sym = get_symbol(r);
            if (needs_plt_entry(r)) {
                // This relocation is a function call, we need to use the PLT entry for this symbol.
                auto plt_def = plt_defs.find(sym);
                internal_assert(plt_def != plt_defs.end());
                debug(2) << "Using PLT entry " << plt_def->second->get_name() << " for symbol " << sym->get_name() << "\n";
                sym = plt_def->second;
            }

            uint64_t fixup_offset = get_section_offset(s) + r.get_offset();
            char *fixup_addr = output.data() + fixup_offset;
            uint64_t sym_offset = 0;
            if (sym && sym->is_defined()) {
                sym_offset = get_section_offset(*sym->get_section()) + sym->get_offset();
                debug(2) << "Symbol " << sym->get_name() << " is defined at " << sym_offset << "\n";
            }
            Relocation new_reloc = linker->relocate(fixup_offset, fixup_addr, r.get_type(), sym, sym_offset, r.get_addend(), got);
            if (new_reloc.get_type() != 0) {
                // The linker wants a dynamic relocation here. This
                // section must be writable at runtime.
                internal_assert(s.is_writable());
                debug(2) << "Linker returned new relocation type " << new_reloc.get_type() << "\n";
                new_reloc.set_offset(new_reloc.get_offset() - get_section_offset(got));
                got.add_relocation(new_reloc);
            }
        }
    };

    // Now that we've generated the symbol table, we can do relocations.
    do_relocations(plt);
    for (const Section &s : obj.sections()) {
        do_relocations(s);
    }

    // Now we can write the GOT.
    internal_assert(got.contents_size() <= max_got_size);
    memcpy(output.data() + get_section_offset(got), got.contents_data(), got.contents_size());

    auto write_relocation_section = [&](const Section &s) {
        uint64_t alignment = 8;
        append_padding(output, alignment);
        uint64_t offset = output.size();
        for (const Relocation &r : s.relocations()) {
            uint64_t i_offset = get_section_offset(s) + r.get_offset();
            Rela<T> rela(i_offset, r.get_type(), symbol_idxs[get_symbol(r)], r.get_addend());
            append_object(output, rela);
        }
        uint64_t size = output.size() - offset;
        append_padding(output, alignment);

        Shdr<T> shdr;
        safe_assign(shdr.sh_name, strings.get(".rela" + s.get_name()));
        shdr.sh_type = Section::SHT_RELA;
        shdr.sh_flags = Section::SHF_ALLOC;
        safe_assign(shdr.sh_offset, offset);
        safe_assign(shdr.sh_addr, offset);
        safe_assign(shdr.sh_size, size);
        safe_assign(shdr.sh_addralign, alignment);

        safe_assign(shdr.sh_link, symtab_idx);
        safe_assign(shdr.sh_info, section_idxs[&s]);
        shdr.sh_entsize = sizeof(Rela<T>);

        uint16_t shndx = safe_cast<uint16_t>(shdrs.size());
        shdrs.push_back(shdr);
        return shndx;
    };

    addr_t rela_got_idx = write_relocation_section(got);

    // Add some strings we know we'll need in the string table after we write it.
    strings.get(soname);
    for (const auto &i : dependencies) {
        strings.get(i);
    }

    Section dynamic(".dynamic", Section::SHT_DYNAMIC);
    strings.get(dynamic.get_name());
    dynamic.set_alignment(4);
    dynamic.set_flag(Section::SHF_ALLOC);
    Section strtab(".strtab", Section::SHT_STRTAB);
    strings.get(strtab.get_name());
    strtab.set_flag(Section::SHF_ALLOC);
    strtab.set_contents(strings.table);
    uint16_t strtab_idx = write_section(strtab, 0);

    std::vector<Dyn<T>> dyn;
    auto make_dyn = [](int32_t tag, addr_t val) {
        Dyn<T> d;
        d.d_tag = tag;
        d.d_val = val;
        return d;
    };

    for (const auto &i : dependencies) {
        dyn.push_back(make_dyn(DT_NEEDED, strings.get(i)));
    }
    if (!soname.empty()) {
        dyn.push_back(make_dyn(DT_SONAME, strings.get(soname)));
    }
    dyn.push_back(make_dyn(DT_SYMBOLIC, 0));

    // This is really required...
    dyn.push_back(make_dyn(DT_HASH, get_section_offset(hash)));

    // Address of the symbol table.
    dyn.push_back(make_dyn(DT_SYMTAB, shdrs[dynsym_idx].sh_offset));
    dyn.push_back(make_dyn(DT_SYMENT, shdrs[dynsym_idx].sh_entsize));

    // Address of the string table.
    dyn.push_back(make_dyn(DT_STRTAB, get_section_offset(strtab)));
    dyn.push_back(make_dyn(DT_STRSZ, strtab.get_size()));

    // Offset to the GOT.
    dyn.push_back(make_dyn(DT_PLTGOT, get_section_offset(got)));

    // Relocations associated with the PLT.
    addr_t pltrelsz = sizeof(Rela<T>) * plt_symbols.size();
    dyn.push_back(make_dyn(DT_JMPREL, shdrs[rela_got_idx].sh_offset));
    dyn.push_back(make_dyn(DT_PLTREL, DT_RELA));
    dyn.push_back(make_dyn(DT_PLTRELSZ, pltrelsz));

    // Other relocations.
    dyn.push_back(make_dyn(DT_RELA, shdrs[rela_got_idx].sh_offset + pltrelsz));
    dyn.push_back(make_dyn(DT_RELASZ, shdrs[rela_got_idx].sh_size - pltrelsz));
    dyn.push_back(make_dyn(DT_RELAENT, sizeof(Rela<T>)));

    // DT_FINI
    Object::section_iterator iter_fini = obj.find_section(".fini.halide");
    if (iter_fini != obj.sections_end()) {
        Section &fini = *iter_fini;
        dyn.push_back(make_dyn(DT_FINI, get_section_offset(fini)));
    }

    // DT_INIT
    Object::section_iterator iter_init = obj.find_section(".init.halide");
    if (iter_init != obj.sections_end()) {
        Section &init = *iter_init;
        dyn.push_back(make_dyn(DT_INIT, get_section_offset(init)));
    }

    dynamic.set_contents(dyn);

    // Add any target specific stuff.
    linker->append_dynamic(dynamic);

    // Null terminator.
    dynamic.append_contents((uint32_t)DT_NULL);
    dynamic.append_contents((addr_t)0);

    uint16_t dyn_idx = write_section(dynamic, sizeof(dyn[0]));
    dyn_phdr.p_type = PT_DYNAMIC;
    dyn_phdr.p_offset = shdrs[dyn_idx].sh_offset;
    dyn_phdr.p_flags = PF_R;
    dyn_phdr.p_filesz = shdrs[dyn_idx].sh_size;
    dyn_phdr.p_memsz = dyn_phdr.p_filesz;
    dyn_phdr.p_align = 4;

    append_padding(output, 4096);
    safe_assign(data_phdr.p_filesz, output.size() - data_phdr.p_offset);

    // Setup the section headers.
    shdrs[symtab_idx].sh_link = strtab_idx;
    shdrs[dynsym_idx].sh_link = strtab_idx;
    shdrs[dyn_idx].sh_link = strtab_idx;
    shdrs[hash_idx].sh_link = dynsym_idx;

    // Write the section header table.
    ehdr.e_shoff = output.size();
    ehdr.e_shnum = shdrs.size();
    ehdr.e_shentsize = sizeof(shdrs[0]);
    for (auto &i : shdrs) {
        append_object(output, i);
    }

    // Now go back and write the headers.
    memcpy(ehdr.e_ident, elf_magic, 4);
    ehdr.e_ident[4] = 1;
    ehdr.e_ident[5] = 1;
    ehdr.e_ident[6] = 1;
    ehdr.e_type = Object::ET_DYN;
    ehdr.e_machine = linker->get_machine();
    ehdr.e_ehsize = sizeof(ehdr);
    ehdr.e_version = linker->get_version();
    ehdr.e_entry = obj.get_entry();
    ehdr.e_flags = linker->get_flags();
    ehdr.e_phoff = sizeof(ehdr);
    ehdr.e_phentsize = sizeof(phdrs[0]);
    ehdr.e_phnum = phdrs.size();
    ehdr.e_shstrndx = strtab_idx;

    memcpy(output.data(), &ehdr, sizeof(ehdr));
    for (auto &i : phdrs) {
        i.p_vaddr = i.p_offset;
        i.p_paddr = i.p_offset;
        i.p_memsz = i.p_filesz;
    }
    memcpy(output.data() + ehdr.e_phoff, phdrs.data(), sizeof(phdrs));

    return output;
}

std::vector<char> Object::write_shared_object(Linker *linker, const std::vector<std::string> &dependencies,
                                              const std::string &soname) {
    return write_shared_object_internal<Types<32>>(*this, linker, dependencies, soname);
}

}  // namespace Elf
}  // namespace Internal
}  // namespace Halide
