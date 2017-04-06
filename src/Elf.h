#ifndef HALIDE_ELF_H
#define HALIDE_ELF_H

#include <algorithm>
#include <memory>
#include <vector>
#include <list>
#include <string>
#include <iterator>
#include <limits>

namespace Halide {
namespace Internal {
namespace Elf {

// This ELF parser mostly deserializes the object into a graph
// structure in memory. It replaces indices into tables (sections,
// symbols, etc.) with a weakly referenced graph of pointers. The
// Object datastructure owns all of the objects. This namespace exists
// because it is very difficult to use LLVM's object parser to modify
// an object (it's fine for parsing only). This was built using
// http://www.skyfree.org/linux/references/ELF_Format.pdf as a reference
// for the ELF structs and constants.


class Object;
class Symbol;
class Section;
class Relocation;

// Helpful wrapper to allow range-based for loops.
template <typename T>
class iterator_range {
    T b, e;
public:
    iterator_range(T b, T e) : b(b), e(e) {}

    T begin() const { return b; }
    T end() const { return e; }
};

/** Describes a symbol */
class Symbol {
public:
    enum Binding : uint8_t {
        STB_LOCAL = 0,
        STB_GLOBAL = 1,
        STB_WEAK = 2,
        STB_LOPROC = 13,
        STB_HIPROC = 15,
    };

    enum Type : uint8_t {
        STT_NOTYPE = 0,
        STT_OBJECT = 1,
        STT_FUNC = 2,
        STT_SECTION = 3,
        STT_FILE = 4,
        STT_LOPROC = 13,
        STT_HIPROC = 15,
    };

    enum Visibility : uint8_t {
        STV_DEFAULT = 0,
        STV_INTERNAL = 1,
        STV_HIDDEN = 2,
        STV_PROTECTED = 3,
    };

private:
    std::string name;
    const Section *definition = nullptr;
    uint64_t offset = 0;
    uint32_t size = 0;
    Binding binding = STB_LOCAL;
    Type type = STT_NOTYPE;
    Visibility visibility = STV_DEFAULT;

public:
    Symbol() {}
    Symbol(const std::string &name) : name(name) {}

    /** Accesses the name of this symbol. */
    ///@{
    Symbol &set_name(const std::string &name) {
        this->name = name;
        return *this;
    }
    const std::string &get_name() const { return name; }
    ///@}

    /** Accesses the type of this symbol. */
    ///@{
    Symbol &set_type(Type type) {
        this->type = type;
        return *this;
    }
    Type get_type() const { return type; }
    ///@}

    /** Accesses the properties that describe the definition of this symbol. */
    ///@{
    Symbol &define(const Section *section, uint64_t offset, uint32_t size) {
        this->definition = section;
        this->offset = offset;
        this->size = size;
        return *this;
    }
    bool is_defined() const { return definition != nullptr; }
    const Section *get_section() const { return definition; }
    uint64_t get_offset() const { return offset; }
    uint32_t get_size() const { return size; }
    ///@}

    /** Access the binding and visibility of this symbol. See the ELF
     * spec for more information about these properties. */
    ///@{
    Symbol &set_binding(Binding binding) {
        this->binding = binding;
        return *this;
    }
    Symbol &set_visibility(Visibility visibility) {
        this->visibility = visibility;
        return *this;
    }
    Binding get_binding() const { return binding; }
    Visibility get_visibility() const { return visibility; }
    ///@}
};

/** Describes a relocation to be applied to an offset of a section in
 * an Object. */
class Relocation {
    uint32_t type = 0;
    uint64_t offset = 0;
    int64_t addend = 0;
    const Symbol *symbol = nullptr;

public:
    Relocation() {}
    Relocation(uint32_t type, uint64_t offset, int64_t addend, const Symbol *symbol)
        : type(type), offset(offset), addend(addend), symbol(symbol) {}

    /** The type of relocation to be applied. The meaning of this
     * value depends on the machine of the object. */
    ///@{
    Relocation &set_type(uint32_t type) {
        this->type = type;
        return *this;
    }
    uint32_t get_type() const { return type; }
    ///@}

    /** Where to apply the relocation. This is relative to the section
     * the relocation belongs to. */
    ///@{
    Relocation &set_offset(uint64_t offset) {
        this->offset = offset;
        return *this;
    }
    uint64_t get_offset() const { return offset; }
    ///@}

    /** The value to replace with the relocation is the address of the symbol plus the addend. */
    ///@{
    Relocation &set_symbol(const Symbol *symbol) {
        this->symbol = symbol;
        return *this;
    }
    Relocation &set_addend(int64_t addend) {
        this->addend = addend;
        return *this;
    }
    const Symbol *get_symbol() const { return symbol; }
    int64_t get_addend() const { return addend; }
    ///@}
};

/** Describes a section of an object file. */
class Section {
public:
    enum Type : uint32_t {
        SHT_NULL = 0,
        SHT_PROGBITS = 1,
        SHT_SYMTAB = 2,
        SHT_STRTAB = 3,
        SHT_RELA = 4,
        SHT_HASH = 5,
        SHT_DYNAMIC = 6,
        SHT_NOTE = 7,
        SHT_NOBITS = 8,
        SHT_REL = 9,
        SHT_SHLIB = 10,
        SHT_DYNSYM = 11,
        SHT_LOPROC = 0x70000000,
        SHT_HIPROC = 0x7fffffff,
        SHT_LOUSER = 0x80000000,
        SHT_HIUSER = 0xffffffff,
    };

    enum Flag : uint32_t {
        SHF_WRITE = 0x1,
        SHF_ALLOC = 0x2,
        SHF_EXECINSTR = 0x4,
        SHF_MASKPROC = 0xf0000000,
    };

    typedef std::vector<Relocation> RelocationList;
    typedef RelocationList::iterator relocation_iterator;
    typedef RelocationList::const_iterator const_relocation_iterator;

    typedef std::vector<char>::iterator contents_iterator;
    typedef std::vector<char>::const_iterator const_contents_iterator;

private:
    std::string name;
    Type type = SHT_NULL;
    uint32_t flags = 0;
    std::vector<char> contents;
    // Sections may have a size larger than the contents.
    uint64_t size = 0;
    uint64_t alignment = 1;
    RelocationList relocs;

public:
    Section() {}
    Section(const std::string &name, Type type) : name(name), type(type) {}

    Section &set_name(const std::string &name) {
        this->name = name;
        return *this;
    }
    const std::string &get_name() const { return name; }

    Section &set_type(Type type) {
        this->type = type;
        return *this;
    }
    Type get_type() const { return type; }

    Section &set_flag(Flag flag) {
        this->flags |= flag;
        return *this; }
    Section &remove_flag(Flag flag) {
        this->flags &= ~flag;
        return *this;
    }
    Section &set_flags(uint32_t flags) {
        this->flags = flags;
        return *this;
    }
    uint32_t get_flags() const { return flags; }
    bool is_alloc() const { return (flags & SHF_ALLOC) != 0; }
    bool is_writable() const { return (flags & SHF_WRITE) != 0; }

    /** Get or set the size of the section. The size may be larger
     * than the content. */
    ///@{
    Section &set_size(uint64_t size) {
        this->size = size;
        return *this;
    }
    uint64_t get_size() const { return std::max((uint64_t) size, (uint64_t) contents.size()); }
    ///@}

    Section &set_alignment(uint64_t alignment) {
        this->alignment = alignment;
        return *this;
    }
    uint64_t get_alignment() const { return alignment; }

    Section &set_contents(std::vector<char> contents) {
        this->contents = std::move(contents);
        return *this;
    }
    template <typename It>
    Section &set_contents(It begin, It end) {
        this->contents.assign(begin, end);
        return *this;
    }
    template <typename It>
    Section &append_contents(It begin, It end) {
        this->contents.insert(this->contents.end(), begin, end);
        return *this;
    }
    /** Set or append an object to the contents, assuming T is a
     * trivially copyable datatype. */
    template <typename T>
    Section &set_contents(const std::vector<T> &contents) {
        this->contents.assign((const char *)contents.data(), (const char *)(contents.data() + contents.size()));
        return *this;
    }
    template <typename T>
    Section &append_contents(const T& x) {
        return append_contents((const char *)&x, (const char *)(&x + 1));
    }
    const std::vector<char> &get_contents() const { return contents; }
    contents_iterator contents_begin() { return contents.begin(); }
    contents_iterator contents_end() { return contents.end(); }
    const_contents_iterator contents_begin() const { return contents.begin(); }
    const_contents_iterator contents_end() const { return contents.end(); }
    const char *contents_data() const { return contents.data(); }
    size_t contents_size() const { return contents.size(); }
    bool contents_empty() const { return contents.empty(); }

    Section &set_relocations(std::vector<Relocation> relocs) {
        this->relocs = std::move(relocs);
        return *this;
    }
    template <typename It>
    Section &set_relocations(It begin, It end) {
        this->relocs.assign(begin, end);
        return *this;
    }
    void add_relocation(const Relocation &reloc) { relocs.push_back(reloc); }
    relocation_iterator relocations_begin() { return relocs.begin(); }
    relocation_iterator relocations_end() { return relocs.end(); }
    iterator_range<relocation_iterator> relocations() { return {relocs.begin(), relocs.end()}; }
    const_relocation_iterator relocations_begin() const { return relocs.begin(); }
    const_relocation_iterator relocations_end() const { return relocs.end(); }
    iterator_range<const_relocation_iterator> relocations() const { return {relocs.begin(), relocs.end()}; }
    size_t relocations_size() const { return relocs.size(); }
};

/** Base class for a target architecture to implement the target
 * specific aspects of linking. */
class Linker {
public:
    virtual ~Linker() {}

    virtual uint16_t get_machine() = 0;
    virtual uint32_t get_flags() = 0;
    virtual uint32_t get_version() = 0;
    virtual void append_dynamic(Section &dynamic) = 0;

    /** Add or get an entry to the global offset table (GOT) with a
     * relocation pointing to sym. */
    virtual uint64_t get_got_entry(Section &got, const Symbol &sym) = 0;

    /** Check to see if this relocation should go through the PLT. */
    virtual bool needs_plt_entry(const Relocation &reloc) = 0;

    /** Add a PLT entry for a symbol sym defined externally. Returns a
     * symbol representing the PLT entry. */
    virtual Symbol add_plt_entry(const Symbol &sym, Section &plt, Section &got,
                                 const Symbol &got_sym) = 0;

    /** Perform a relocation. This function may opt to not apply the
     * relocation, and return a new relocation to be performed at
     * runtime. This requires that the section to apply the relocation
     * to is writable at runtime. */
    virtual Relocation relocate(uint64_t fixup_offset, char *fixup_addr, uint64_t type,
                                const Symbol *sym, uint64_t sym_offset, int64_t addend,
                                Section &got) = 0;

};

/** Holds all of the relevant sections and symbols for an object. */
class Object {
public:
    enum Type : uint16_t {
        ET_NONE = 0,
        ET_REL = 1,
        ET_EXEC = 2,
        ET_DYN = 3,
        ET_CORE = 4,
        ET_LOPROC = 0xff00,
        ET_HIPROC = 0xffff,
    };

    // We use lists for sections and symbols to avoid iterator
    // invalidation when we modify the containers.
    typedef std::list<Section> SectionList;
    typedef typename SectionList::iterator section_iterator;
    typedef typename SectionList::const_iterator const_section_iterator;

    typedef std::list<Symbol> SymbolList;
    typedef typename SymbolList::iterator symbol_iterator;
    typedef typename SymbolList::const_iterator const_symbol_iterator;

private:
    SectionList secs;
    SymbolList syms;

    Type type = ET_NONE;
    uint16_t machine = 0;
    uint32_t version = 0;
    uint64_t entry = 0;
    uint32_t flags = 0;

    Object(const Object &);
    void operator = (const Object &);

public:
    Object() {}

    Type get_type() const { return type; }
    uint16_t get_machine() const { return machine; }
    uint32_t get_version() const { return version; }
    uint64_t get_entry() const { return entry; }
    uint32_t get_flags() const { return flags; }

    Object &set_type(Type type) {
        this->type = type;
        return *this;
    }
    Object &set_machine(uint16_t machine) {
        this->machine = machine;
        return *this;
    }
    Object &set_version(uint32_t version) {
        this->version = version;
        return *this;
    }
    Object &set_entry(uint64_t entry) {
        this->entry = entry;
        return *this;
    }
    Object &set_flags(uint32_t flags) {
        this->flags = flags;
        return *this;
    }

    /** Parse an object in memory to an Object. */
    static std::unique_ptr<Object> parse_object(const char *data, size_t size);

    /** Write a shared object in memory. */
    std::vector<char> write_shared_object(Linker *linker, const std::vector<std::string> &depedencies = {},
                                          const std::string &soname = "");

    section_iterator sections_begin() { return secs.begin(); }
    section_iterator sections_end() { return secs.end(); }
    iterator_range<section_iterator> sections() { return {secs.begin(), secs.end()}; }
    const_section_iterator sections_begin() const { return secs.begin(); }
    const_section_iterator sections_end() const { return secs.end(); }
    iterator_range<const_section_iterator> sections() const { return {secs.begin(), secs.end()}; }
    size_t sections_size() const { return secs.size(); }
    section_iterator find_section(const std::string &name);

    section_iterator add_section(const std::string &name, Section::Type type);
    section_iterator add_relocation_section(const Section &for_section);
    section_iterator erase_section(section_iterator i) { return secs.erase(i); }

    section_iterator merge_sections(const std::vector<section_iterator> &sections);
    section_iterator merge_text_sections();

    symbol_iterator symbols_begin() { return syms.begin(); }
    symbol_iterator symbols_end() { return syms.end(); }
    iterator_range<symbol_iterator> symbols() { return {syms.begin(), syms.end()}; }
    const_symbol_iterator symbols_begin() const { return syms.begin(); }
    const_symbol_iterator symbols_end() const { return syms.end(); }
    iterator_range<const_symbol_iterator> symbols() const { return {syms.begin(), syms.end()}; }
    size_t symbols_size() const { return syms.size(); }
    symbol_iterator find_symbol(const std::string &name);
    const_symbol_iterator find_symbol(const std::string &name) const;

    symbol_iterator add_symbol(const std::string &name);
};

}  // namespace Elf
}  // namespace Internal
}  // namespace Halide

#endif
