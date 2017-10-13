#include <iostream>
#include <fstream>
#include <memory>

#include "HexagonOffload.h"
#include "Closure.h"
#include "InjectHostDevBufferCopies.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LLVM_Output.h"
#include "LLVM_Headers.h"
#include "Param.h"
#include "RemoveTrivialForLoops.h"
#include "Substitute.h"
#include "Elf.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace Elf {

// Most of these constants were duplicated from LLVM's object parser code.
enum {
    EV_CURRENT = 1,
};

enum {
    EM_HEXAGON = 164,
};

// http://llvm.org/docs/doxygen/html/Support_2ELF_8h_source.html#l00558
enum {
    EF_HEXAGON_MACH_V2 = 0x1,
    EF_HEXAGON_MACH_V3 = 0x2,
    EF_HEXAGON_MACH_V4 = 0x3,
    EF_HEXAGON_MACH_V5 = 0x4,
    EF_HEXAGON_MACH_V55 = 0x5,
    EF_HEXAGON_MACH_V60 = 0x60,
    EF_HEXAGON_MACH_V61 = 0x61,
    EF_HEXAGON_MACH_V62 = 0x62,
    EF_HEXAGON_MACH_V65 = 0x65,
    EF_HEXAGON_MACH_V66 = 0x66,
};

enum {
    DT_HEXAGON_VER = 0x70000001,
};

// https://llvm.org/svn/llvm-project/llvm/trunk/include/llvm/Support/ELFRelocs/Hexagon.def
enum {
    R_HEX_NONE = 0,
    R_HEX_B22_PCREL = 1,
    R_HEX_B15_PCREL = 2,
    R_HEX_B7_PCREL = 3,
    R_HEX_LO16 = 4,
    R_HEX_HI16 = 5,
    R_HEX_32 = 6,
    R_HEX_16 = 7,
    R_HEX_8 = 8,
    R_HEX_GPREL16_0 = 9,
    R_HEX_GPREL16_1 = 10,
    R_HEX_GPREL16_2 = 11,
    R_HEX_GPREL16_3 = 12,
    R_HEX_HL16 = 13,
    R_HEX_B13_PCREL = 14,
    R_HEX_B9_PCREL = 15,
    R_HEX_B32_PCREL_X = 16,
    R_HEX_32_6_X = 17,
    R_HEX_B22_PCREL_X = 18,
    R_HEX_B15_PCREL_X = 19,
    R_HEX_B13_PCREL_X = 20,
    R_HEX_B9_PCREL_X = 21,
    R_HEX_B7_PCREL_X = 22,
    R_HEX_16_X = 23,
    R_HEX_12_X = 24,
    R_HEX_11_X = 25,
    R_HEX_10_X = 26,
    R_HEX_9_X = 27,
    R_HEX_8_X = 28,
    R_HEX_7_X = 29,
    R_HEX_6_X = 30,
    R_HEX_32_PCREL = 31,
    R_HEX_COPY = 32,
    R_HEX_GLOB_DAT = 33,
    R_HEX_JMP_SLOT = 34,
    R_HEX_RELATIVE = 35,
    R_HEX_PLT_B22_PCREL = 36,
    R_HEX_GOTREL_LO16 = 37,
    R_HEX_GOTREL_HI16 = 38,
    R_HEX_GOTREL_32 = 39,
    R_HEX_GOT_LO16 = 40,
    R_HEX_GOT_HI16 = 41,
    R_HEX_GOT_32 = 42,
    R_HEX_GOT_16 = 43,
    R_HEX_DTPMOD_32 = 44,
    R_HEX_DTPREL_HI16 = 46,
    R_HEX_DTPREL_32 = 47,
    R_HEX_DTPREL_16 = 48,
    R_HEX_GD_PLT_B22_PCREL = 49,
    R_HEX_GD_GOT_LO16 = 50,
    R_HEX_GD_GOT_HI16 = 51,
    R_HEX_GD_GOT_32 = 52,
    R_HEX_GD_GOT_16 = 53,
    R_HEX_IE_LO16 = 54,
    R_HEX_IE_HI16 = 55,
    R_HEX_IE_32 = 56,
    R_HEX_IE_GOT_LO16 = 57,
    R_HEX_IE_GOT_HI16 = 58,
    R_HEX_IE_GOT_32 = 59,
    R_HEX_IE_GOT_16 = 60,
    R_HEX_TPREL_LO16 = 61,
    R_HEX_TPREL_HI16 = 62,
    R_HEX_TPREL_32 = 63,
    R_HEX_TPREL_16 = 64,
    R_HEX_6_PCREL_X = 65,
    R_HEX_GOTREL_32_6_X = 66,
    R_HEX_GOTREL_16_X = 67,
    R_HEX_GOTREL_11_X = 68,
    R_HEX_GOT_32_6_X = 69,
    R_HEX_GOT_16_X = 70,
    R_HEX_GOT_11_X = 71,
    R_HEX_DTPREL_32_6_X = 72,
    R_HEX_DTPREL_16_X = 73,
    R_HEX_DTPREL_11_X = 74,
    R_HEX_GD_GOT_32_6_X = 75,
    R_HEX_GD_GOT_16_X = 76,
    R_HEX_GD_GOT_11_X = 77,
    R_HEX_IE_32_6_X = 78,
    R_HEX_IE_16_X = 79,
    R_HEX_IE_GOT_32_6_X = 80,
    R_HEX_IE_GOT_16_X = 81,
    R_HEX_IE_GOT_11_X = 82,
    R_HEX_TPREL_32_6_X = 83,
    R_HEX_TPREL_16_X = 84,
    R_HEX_TPREL_11_X = 85,
    R_HEX_LD_PLT_B22_PCREL = 86,
    R_HEX_LD_GOT_LO16 = 87,
    R_HEX_LD_GOT_HI16 = 88,
    R_HEX_LD_GOT_32 = 89,
    R_HEX_LD_GOT_16 = 90,
    R_HEX_LD_GOT_32_6_X = 91,
    R_HEX_LD_GOT_16_X = 92,
    R_HEX_LD_GOT_11_X = 93,
};

// This logic comes from support from Qualcomm.
bool maybe_branch_inst(uint32_t reloc_type) {
    switch (reloc_type) {
    case R_HEX_PLT_B22_PCREL:
    case R_HEX_B22_PCREL:
    case R_HEX_B22_PCREL_X:
    case R_HEX_B15_PCREL:
    case R_HEX_B15_PCREL_X:
    case R_HEX_B13_PCREL:
    case R_HEX_B13_PCREL_X:
    case R_HEX_B9_PCREL:
    case R_HEX_B9_PCREL_X:
    case R_HEX_B7_PCREL:
    case R_HEX_B7_PCREL_X:
    case R_HEX_B32_PCREL_X:
    case R_HEX_32_PCREL:
    case R_HEX_6_PCREL_X:

    case R_HEX_LO16:
    case R_HEX_HI16:
    case R_HEX_16:
    case R_HEX_8:
    case R_HEX_32_6_X:
    case R_HEX_16_X:
    case R_HEX_12_X:
    case R_HEX_11_X:
    case R_HEX_10_X:
    case R_HEX_9_X:
    case R_HEX_8_X:
    case R_HEX_7_X:
    case R_HEX_6_X:
    case R_HEX_32:
        return true;
    default:
        return false;
    }
}

std::string hex(uint32_t x) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "0x%08x", x);
    return buffer;
}

std::string section_type_string(Section::Type type) {
    switch(type) {
    case Section::SHT_NULL: return "SHT_NULL";
    case Section::SHT_PROGBITS: return "SHT_PROGBITS";
    case Section::SHT_SYMTAB: return "SHT_SYMTAB";
    case Section::SHT_STRTAB: return "SHT_STRTAB";
    case Section::SHT_RELA: return "SHT_RELA";
    case Section::SHT_HASH: return "SHT_HASH";
    case Section::SHT_DYNAMIC: return "SHT_DYNAMIC";
    case Section::SHT_NOTE: return "SHT_NOTE";
    case Section::SHT_NOBITS: return "SHT_NOBITS";
    case Section::SHT_REL: return "SHT_REL";
    case Section::SHT_SHLIB: return "SHT_SHLIB";
    case Section::SHT_DYNSYM: return "SHT_DYNSYM";
    case Section::SHT_LOPROC: return "SHT_LOPROC";
    case Section::SHT_HIPROC: return "SHT_HIPROC";
    case Section::SHT_LOUSER: return "SHT_LOUSER";
    case Section::SHT_HIUSER: return "SHT_HIUSER";
    default:
        return "UNKNOWN TYPE";
    }
}
std::string print_sections(const Object &obj) {
    std::ostringstream oss;
    if (obj.sections_size() == 0) {
        oss << "No sections in object\n";
        return oss.str();
    }
    for (const Section &s: obj.sections()) {
        oss << s.get_name() << ", Type = " << section_type_string(s.get_type()) << ", Size = " << hex(s.get_size()) << ", Alignment = " << s.get_alignment() << "\n";
    }
    return oss.str();
}

// NULL-terminated list of Hexagon instructions (defined at the end of
// this file).
extern const char *hexagon_instructions[];

// Given an instruction and an encoding from hexagon_instructions,
// check if the instruction is one of these encoded instructions,
// and if so, return the mask for relocation. Returns 0 otherwise.
uint32_t get_mask_for_instruction(uint32_t instruction, const char *encoding) {
    uint32_t mask = 0;
    int instruction_bits = strlen(encoding);
    internal_assert(instruction_bits == 32);
    for (int i = 0; i < instruction_bits; i++) {
        char encoding_i = encoding[instruction_bits - i - 1];
        int inst_i = (instruction >> i) & 1;
        switch (encoding_i) {
        case '0':
            if (inst_i != 0) return 0;
            break;
        case '1':
            if (inst_i != 1) return 0;
            break;
        case 'i':
            mask |= (1 << i);
            break;
        }
    }
    return mask;
}

void do_reloc(char *addr, uint32_t mask, uintptr_t val, bool is_signed, bool verify) {
    uint32_t inst = *((uint32_t *)addr);
    debug(4) << "Relocation in instruction: " << hex(inst) << "\n";
    debug(4) << "val: " << hex(val) << "\n";
    debug(4) << "mask: " << hex(mask) << "\n";

    if (!mask) {
        // The mask depends on the instruction. To implement
        // relocations for new instructions see
        // instruction_encodings.txt
        // First print the bits so I can search for it in the
        // instruction encodings.
        debug(4) << "Instruction bits: ";
        for (int i = 31; i >=0; i--) {
            debug(4) << (int)((inst >> i) & 1);
        }
        debug(4) << "\n";

        if ((inst & (3 << 14)) == 0) {
            // Some instructions are actually pairs of 16-bit
            // subinstructions. See section 3.7 in the
            // programmer's reference.
            debug(4) << "Duplex!\n";

            int iclass = ((inst >> 29) << 1) | ((inst >> 13) & 1);
            debug(4) << "Class: " << hex(iclass) << "\n";
            debug(4) << "Hi: ";
            for (int i = 28; i >= 16; i--) {
                debug(4) << (int)((inst >> i) & 1);
            }
            debug(4) << "\n";
            debug(4) << "Lo: ";
            for (int i = 12; i >= 0; i--) {
                debug(4) << (int)((inst >> i) & 1);
            }
            debug(4) << "\n";

            // We only know how to do the ones where the high
            // subinstruction is an immediate assignment. (marked
            // as A in table 9-4 in the programmer's reference
            // manual).
            internal_assert(iclass >= 3 && iclass <= 7);

            // Pull out the subinstructions. They're the low 13
            // bits of each half-word.
            uint32_t hi = (inst >> 16) & ((1 << 13) - 1);
            //uint32_t lo = inst & ((1 << 13) - 1);

            // We only understand the ones where hi starts with 010
            internal_assert((hi >> 10) == 2);

            // Low 6 bits of val go in the following bits.
            mask = 63 << 20;
        } else {
            for (const char **encoding = hexagon_instructions; *encoding; encoding++) {
                mask = get_mask_for_instruction(inst, *encoding);
                if (mask) {
                    break;
                }
            }
            internal_assert(mask != 0) << "Unknown instruction " << inst;
        }
    }

    uintptr_t old_val = val;
    bool consumed_every_bit = false;
    for (int i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            internal_assert((inst & (1 << i)) == 0);

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

    internal_assert(!verify || consumed_every_bit)
        << "Relocation overflow inst=" << hex(inst)
        << "mask=" << hex(mask) << " val=" << hex(old_val) << "\n";

    debug(4) << "Relocated instruction: " << hex(inst) << "\n";

    *((uint32_t *)addr) = inst;
}

void do_relocation(uint32_t fixup_offset, char *fixup_addr, uint32_t type,
                   const Symbol *sym, uint32_t sym_offset, int32_t addend,
                   Elf::Section &got) {
    // Hexagon relocations are specified in section 11.5 in
    // the Hexagon Application Binary Interface spec.

    // Now we can define the variables from Table 11-5.
    uint32_t S = sym_offset;
    uint32_t P = fixup_offset;
    intptr_t A = addend;
    uint32_t GP = 0;

    uint32_t G = got.contents_size();
    for (const Relocation &r : got.relocations()) {
        if (r.get_symbol() == sym) {
            G = r.get_offset();
            debug(2) << "Reusing G=" << G << " for symbol " << sym->get_name() << "\n";
            break;
        }
    }

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

    bool needs_got_entry = false;

    switch (type) {
    case R_HEX_B22_PCREL:
        do_reloc(fixup_addr, Word32_B22, intptr_t(S + A - P) >> 2, _signed, verify);
        break;
    case R_HEX_B15_PCREL:
        // Untested
        do_reloc(fixup_addr, Word32_B15, intptr_t(S + A - P) >> 2, _signed, verify);
        break;
    case R_HEX_B7_PCREL:
        do_reloc(fixup_addr, Word32_B7, intptr_t(S + A - P) >> 2, _signed, verify);
        break;
    case R_HEX_LO16:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_HI16:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A) >> 16, _unsigned, truncate);
        break;
    case R_HEX_32:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32, intptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_16:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word16, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_8:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word8, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_GPREL16_0:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP), _unsigned, verify);
        break;
    case R_HEX_GPREL16_1:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 1, _unsigned, verify);
        break;
    case R_HEX_GPREL16_2:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 2, _unsigned, verify);
        break;
    case R_HEX_GPREL16_3:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 3, _unsigned, verify);
        break;
    case R_HEX_HL16:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr,   Word32_LO, uintptr_t(S + A) >> 16, _unsigned, truncate);
        do_reloc(fixup_addr+4, Word32_LO, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_B13_PCREL:
        do_reloc(fixup_addr, Word32_B13, intptr_t(S + A - P) >> 2, _signed, verify);
        break;
    case R_HEX_B9_PCREL:
        do_reloc(fixup_addr, Word32_B9, intptr_t(S + A - P) >> 2, _signed, verify);
        break;
    case R_HEX_B32_PCREL_X:
        do_reloc(fixup_addr, Word32_X26, intptr_t(S + A - P) >> 6, _signed, truncate);
        break;
    case R_HEX_32_6_X:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_X26, uintptr_t(S + A) >> 6, _unsigned, verify);
        break;
    case R_HEX_B22_PCREL_X:
        do_reloc(fixup_addr, Word32_B22, intptr_t(S + A - P) & 0x3f, _signed, verify);
        break;
    case R_HEX_B15_PCREL_X:
        do_reloc(fixup_addr, Word32_B15, intptr_t(S + A - P) & 0x3f, _signed, verify);
        break;
    case R_HEX_B13_PCREL_X:
        do_reloc(fixup_addr, Word32_B13, intptr_t(S + A - P) & 0x3f, _signed, verify);
        break;
    case R_HEX_B9_PCREL_X:
        do_reloc(fixup_addr, Word32_B9, intptr_t(S + A - P) & 0x3f, _signed, verify);
        break;
    case R_HEX_B7_PCREL_X:
        do_reloc(fixup_addr, Word32_B7, intptr_t(S + A - P) & 0x3f, _signed, verify);
        break;
    case R_HEX_16_X:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_12_X:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_R6, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_11_X:
    case R_HEX_10_X:
    case R_HEX_9_X:
    case R_HEX_8_X:
    case R_HEX_7_X:
    case R_HEX_6_X:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A), _unsigned, truncate);
        break;
    case R_HEX_32_PCREL:
        do_reloc(fixup_addr, Word32, intptr_t(S + A - P), _signed, verify);
        break;
    case R_HEX_6_PCREL_X:
        do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A - P), _unsigned, truncate);
        break;
    case R_HEX_GOT_32_6_X:
        do_reloc(fixup_addr, Word32_X26, intptr_t(G) >> 6, _signed, truncate);
        needs_got_entry = true;
        break;
    case R_HEX_GOT_16_X:
        do_reloc(fixup_addr, Word32_U6, intptr_t(G), _signed, truncate);
        needs_got_entry = true;
        break;
    case R_HEX_GOT_11_X:
        do_reloc(fixup_addr, Word32_U6, uintptr_t(G), _unsigned, truncate);
        needs_got_entry = true;
        break;

    default:
        internal_error << "Unhandled relocation type " << type << "\n";
    }

    if (needs_got_entry && G == got.contents_size()) {
        debug(2) << "Adding GOT entry " << G << " for symbol " << sym->get_name() << "\n";
        got.append_contents((uint32_t)0);
        got.add_relocation(Relocation(R_HEX_GLOB_DAT, G, 0, sym));
    }
}

class HexagonLinker : public Linker {
public:
    uint32_t flags;

    HexagonLinker(const Target &target) {
        if (target.has_feature(Target::HVX_v66)) {
            flags = Elf::EF_HEXAGON_MACH_V66;
        } else if (target.has_feature(Target::HVX_v65)) {
            flags = Elf::EF_HEXAGON_MACH_V65;
        } else if (target.has_feature(Target::HVX_v62)) {
            flags = Elf::EF_HEXAGON_MACH_V62;
        } else {
            flags = Elf::EF_HEXAGON_MACH_V60;
        }
    }

    uint16_t get_machine() override { return EM_HEXAGON; }
    uint32_t get_flags() override { return flags; }
    uint32_t get_version() override { return EV_CURRENT; }
    void append_dynamic(Section &dynamic) override {
        dynamic.append_contents((uint32_t)DT_HEXAGON_VER);
        dynamic.append_contents((uint32_t)0x3);
    }

    uint64_t get_got_entry(Section &got, const Symbol &sym) override {
        // Check if we already made a got entry for this symbol.
        for (const Relocation &r : got.relocations()) {
            if (r.get_symbol() == &sym && r.get_type() == R_HEX_GLOB_DAT) {
                internal_assert(r.get_addend() == 0);
                return r.get_offset();
            }
        }

        uint64_t got_offset = got.contents_size();
        got.append_contents((uint32_t)0);
        got.add_relocation(Elf::Relocation(R_HEX_GLOB_DAT, got_offset, 0, &sym));
        return got_offset;
    }

    bool needs_plt_entry(const Relocation &r) override {
        return maybe_branch_inst(r.get_type());
    }

    Symbol add_plt_entry(const Symbol &sym, Section &plt, Section &got, const Symbol &got_sym) override {
        if (got.contents_empty()) {
            // The PLT hasn't been started, initialize it now.
            plt.set_alignment(16);

            std::vector<char> padding(64, (char)0);
            // TODO: Make a .plt0 entry that supports lazy binding.
            plt.set_contents(padding.begin(), padding.end());
        }

        static const uint8_t hexagon_plt1[] = {
            0x00, 0x40, 0x00, 0x00, // { immext (#0) (Relocation:R_HEX_B32_PCREL_X)
            0x0e, 0xc0, 0x49, 0x6a, //   r14 = add (pc, ##GOTn@PCREL) }  (Relocation:R_HEX_6_PCREL_X)
            0x1c, 0xc0, 0x8e, 0x91, //   r28 = memw (r14)
            0x00, 0xc0, 0x9c, 0x52, //   jumpr r28
        };

        debug(2) << "Adding PLT entry for symbol " << sym.get_name() << "\n";

        // Add a GOT entry for this symbol.
        uint64_t got_offset = got.contents_size();
        got.append_contents((uint32_t)0);
        got.add_relocation(Elf::Relocation(R_HEX_JMP_SLOT, got_offset, 0, &sym));

        // Add the PLT code.
        uint32_t plt_offset = plt.get_size();
        plt.append_contents(hexagon_plt1, hexagon_plt1 + sizeof(hexagon_plt1));

        plt.add_relocation(Relocation(R_HEX_B32_PCREL_X, plt_offset + 0, got_offset, &got_sym));
        plt.add_relocation(Relocation(R_HEX_6_PCREL_X, plt_offset + 4, got_offset + 4, &got_sym));

        // Make a symbol for the PLT entry.
        Symbol plt_sym("plt_" + sym.get_name());
        plt_sym
            .set_type(Symbol::STT_FUNC)
            .set_binding(Symbol::STB_LOCAL)
            .define(&plt, plt_offset, sizeof(hexagon_plt1));

        return plt_sym;
    }

    Relocation relocate(uint64_t fixup_offset, char *fixup_addr, uint64_t type,
                        const Elf::Symbol *sym, uint64_t sym_offset, int64_t addend,
                        Elf::Section &got) override {
        if (type == R_HEX_32) {
            // Don't do this relocation, generate a new R_HEX_RELATIVE relocation instead.
            return Relocation(R_HEX_RELATIVE, fixup_offset, sym_offset + addend, nullptr);
        }
        do_relocation(fixup_offset, fixup_addr, type, sym, sym_offset, addend, got);
        return Relocation();
    }
};

}  // namespace Elf

namespace {

const std::string runtime_module_name = "halide_shared_runtime";
const std::string pipeline_module_name = "halide_hexagon_code";

// Replace the parameter objects of loads/stores with a new parameter
// object.
class ReplaceParams : public IRMutator {
    const std::map<std::string, Parameter> &replacements;

    using IRMutator::visit;

    void visit(const Load *op) {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            expr = Load::make(op->type, op->name, mutate(op->index), op->image,
                              i->second, mutate(op->predicate));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            stmt = Store::make(op->name, mutate(op->value), mutate(op->index),
                               i->second, mutate(op->predicate));
        } else {
            IRMutator::visit(op);
        }
    }

public:
    ReplaceParams(const std::map<std::string, Parameter> &replacements)
        : replacements(replacements) {}
};

Stmt replace_params(Stmt s, const std::map<std::string, Parameter> &replacements) {
    return ReplaceParams(replacements).mutate(s);
}

class InjectHexagonRpc : public IRMutator {
    std::map<std::string, Expr> state_bufs;

    Module &device_code;

    // Alignment info for Int(32) variables in scope, so we don't lose
    // the information when creating Hexagon kernels.
    Scope<ModulusRemainder> alignment_info;

    Expr state_var(const std::string& name, Type type) {
        return Let::make(name, state_var_ptr(name, type),
                         Load::make(type_of<void*>(), name, 0,
                                    Buffer<>(), Parameter(), const_true()));
    }

    Expr state_var_ptr(const std::string& name, Type type) {
        Expr &buf = state_bufs[name];
        if (!buf.defined()) {
            auto storage = Buffer<void *>::make_scalar(name + "_buf");
            storage() = nullptr;
            buf = Variable::make(type_of<halide_buffer_t *>(), storage.name() + ".buffer", storage);
        }
        return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
    }

    Expr module_state() {
        return state_var("hexagon_module_state", type_of<void*>());
    }

    Expr module_state_ptr() {
        return state_var_ptr("hexagon_module_state", type_of<void*>());
    }

    // Create a Buffer containing the given buffer/size, and return an
    // expression for a pointer to the first element.
    Expr buffer_ptr(const uint8_t* buffer, size_t size, const char* name) {
        Buffer<uint8_t> code((int)size, name);
        memcpy(code.data(), buffer, (int)size);
        Expr buf = Variable::make(type_of<halide_buffer_t *>(), string(name) + ".buffer", code);
        return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
    }

    using IRMutator::visit;

    void visit(const For *loop) {
        if (loop->device_api != DeviceAPI::Hexagon) {
            IRMutator::visit(loop);
            return;
        }

        // Unrolling or loop partitioning might generate multiple
        // loops with the same name, so we need to make them unique.
        // There's a bit of a hack here: the offload_rpc. prefix is
        // significant, it tells the Hexagon code generator to expect
        // the arguments to be unpacked by the Hexagon remote-side RPC
        // call, which doesn't work with standard buffers.
        std::string hex_name = unique_name("offload_rpc." + loop->name);

        // After moving this to Hexagon, it doesn't need to be marked
        // Hexagon anymore.
        Stmt body = For::make(loop->name, loop->min, loop->extent, loop->for_type,
                              DeviceAPI::None, loop->body);
        body = remove_trivial_for_loops(body);

        // Build a closure for the device code.
        // TODO: Should this move the body of the loop to Hexagon,
        // or the loop itself? Currently, this moves the loop itself.
        Closure c(body);

        // Make an argument list, and generate a function in the
        // device_code module. The hexagon runtime code expects
        // the arguments to appear in the order of (input buffers,
        // output buffers, input scalars).  Scalars must be last
        // for the scalar arguments to shadow the symbols of the
        // buffer that get generated by CodeGen_LLVM.
        std::vector<LoweredArgument> input_buffers, output_buffers;
        std::map<std::string, Parameter> replacement_params;
        for (const auto& i : c.buffers) {
            if (i.second.write) {
                Argument::Kind kind = Argument::OutputBuffer;
                output_buffers.push_back(LoweredArgument(i.first, kind, i.second.type, i.second.dimensions));
            } else {
                Argument::Kind kind = Argument::InputBuffer;
                input_buffers.push_back(LoweredArgument(i.first, kind, i.second.type, i.second.dimensions));
            }

            // Build a parameter to replace.
            Parameter p(i.second.type, true, i.second.dimensions);
            // Assert that buffers are aligned to one HVX vector.
            const int alignment = 128;
            p.set_host_alignment(alignment);
            // The other parameter constraints are already
            // accounted for by the closure grabbing those
            // arguments, so we only need to provide the host
            // alignment.
            replacement_params[i.first] = p;

            // Add an assert to the body that validates the
            // alignment of the buffer.
            if (!device_code.target().has_feature(Target::NoAsserts)) {
                Expr host_ptr = reinterpret<uint64_t>(Variable::make(Handle(), i.first));
                Expr error = Call::make(Int(32), "halide_error_unaligned_host_ptr",
                                        {i.first, alignment}, Call::Extern);
                body = Block::make(AssertStmt::make(host_ptr % alignment == 0, error), body);
            }

            // Unpack buffer parameters into the scope. They come in as host/dev struct pairs.
            Expr buf = Variable::make(Handle(), i.first + ".buffer");
            Expr host_ptr = Call::make(Handle(), "_halide_hexagon_buffer_get_host", {buf}, Call::Extern);
            Expr device_ptr = Call::make(Handle(), "_halide_hexagon_buffer_get_device", {buf}, Call::Extern);
            body = LetStmt::make(i.first + ".device", device_ptr, body);
            body = LetStmt::make(i.first, host_ptr, body);

        }
        body = replace_params(body, replacement_params);

        std::vector<LoweredArgument> args;
        args.insert(args.end(), input_buffers.begin(), input_buffers.end());
        args.insert(args.end(), output_buffers.begin(), output_buffers.end());
        for (const auto& i : c.vars) {
            LoweredArgument arg(i.first, Argument::InputScalar, i.second, 0);
            if (alignment_info.contains(i.first)) {
                arg.alignment = alignment_info.get(i.first);
            }
            args.push_back(arg);
        }
        device_code.append(LoweredFunc(hex_name, args, body, LoweredFunc::ExternalPlusMetadata));

        // Generate a call to hexagon_device_run.
        std::vector<Expr> arg_sizes;
        std::vector<Expr> arg_ptrs;
        std::vector<Expr> arg_flags;

        for (const auto& i : c.buffers) {
            // The Hexagon runtime expects buffer args to be
            // passed as just the device and host
            // field. CodeGen_Hexagon knows how to unpack buffers
            // passed this way.
            Expr buf = Variable::make(type_of<halide_buffer_t *>(), i.first + ".buffer");
            Expr device = Call::make(UInt(64), Call::buffer_get_device, {buf}, Call::Extern);
            Expr host = Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
            Expr pseudo_buffer = Call::make(Handle(), Call::make_struct, {device, host}, Call::Intrinsic);
            arg_ptrs.push_back(pseudo_buffer);
            arg_sizes.push_back(Expr((uint64_t)(pseudo_buffer.type().bytes())));

            // In the flags parameter, bit 0 set indicates the
            // buffer is read, bit 1 set indicates the buffer is
            // written. If neither are set, the argument is a scalar.
            int flags = 0;
            if (i.second.read) flags |= 0x1;
            if (i.second.write) flags |= 0x2;
            arg_flags.push_back(flags);
        }
        for (const auto& i : c.vars) {
            Expr arg = Variable::make(i.second, i.first);
            Expr arg_ptr = Call::make(type_of<void *>(), Call::make_struct, {arg}, Call::Intrinsic);
            arg_sizes.push_back(Expr((uint64_t) i.second.bytes()));
            arg_ptrs.push_back(arg_ptr);
            arg_flags.push_back(0x0);
        }

        // The argument list is terminated with an argument of size 0.
        arg_sizes.push_back(Expr((uint64_t) 0));

        std::string pipeline_name = hex_name + "_argv";
        std::vector<Expr> params;
        params.push_back(module_state());
        params.push_back(pipeline_name);
        params.push_back(state_var_ptr(hex_name, type_of<int>()));
        params.push_back(Call::make(type_of<size_t*>(), Call::make_struct, arg_sizes, Call::Intrinsic));
        params.push_back(Call::make(type_of<void**>(), Call::make_struct, arg_ptrs, Call::Intrinsic));
        params.push_back(Call::make(type_of<int*>(), Call::make_struct, arg_flags, Call::Intrinsic));

        stmt = call_extern_and_assert("halide_hexagon_run", params);
    }

    void visit(const Let *op) {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        IRMutator::visit(op);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
    }

    void visit(const LetStmt *op) {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        IRMutator::visit(op);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
    }

public:
    InjectHexagonRpc(Module &device_code) : device_code(device_code) {}

    Stmt inject(Stmt s) {
        s = mutate(s);

        if (!device_code.functions().empty()) {
            // Wrap the statement in calls to halide_initialize_kernels.
            Expr runtime_buf_var = Variable::make(type_of<struct halide_buffer_t *>(), runtime_module_name + ".buffer");
            Expr runtime_size = Call::make(Int(32), Call::buffer_get_extent, { runtime_buf_var, 0 }, Call::Extern);
            Expr runtime_ptr = Call::make(Handle(), Call::buffer_get_host, { runtime_buf_var }, Call::Extern);

            Expr code_buf_var = Variable::make(type_of<struct halide_buffer_t *>(), pipeline_module_name + ".buffer");
            Expr code_size = Call::make(Int(32), Call::buffer_get_extent, { code_buf_var, 0 }, Call::Extern);
            Expr code_ptr = Call::make(Handle(), Call::buffer_get_host, { code_buf_var }, Call::Extern);
            Stmt init_kernels = call_extern_and_assert("halide_hexagon_initialize_kernels",
                                                       { module_state_ptr(), code_ptr, cast<uint64_t>(code_size), runtime_ptr, cast<uint64_t>(runtime_size) });
            s = Block::make(init_kernels, s);
        }

        // TODO: This can probably go away due to general debug info at the submodule compile level.
        debug(1) << "Hexagon device code module: " << device_code << "\n";

        return s;
    }
};

}  // namespace

Stmt inject_hexagon_rpc(Stmt s, const Target &host_target,
                        Module &containing_module) {
    // Make a new target for the device module.
    Target target(Target::NoOS, Target::Hexagon, 32);
    // There are two ways of offloading, on device and on host.
    // In the former we have true QuRT available, while on the
    // latter we simulate the Hexagon side code with a barebones
    // Shim layer, ie. NO QURT!
    if (host_target.arch == Target::ARM) {
        target.os = Target::QuRT;
    }

    // These feature flags are propagated from the host target to the
    // device module.
    //
    // TODO: We'd like Target::Debug to be in this list too, but trunk
    // llvm currently disagrees with hexagon clang as to what
    // constitutes valid debug info.
    static const Target::Feature shared_features[] = {
        Target::Profile,
        Target::NoAsserts,
        Target::HVX_64,
        Target::HVX_128,
        Target::HVX_v62,
        Target::HVX_v65,
        Target::HVX_v66,
    };
    for (Target::Feature i : shared_features) {
        if (host_target.has_feature(i)) {
            target = target.with_feature(i);
        }
    }

    Module shared_runtime(runtime_module_name, target);
    Module hexagon_module(pipeline_module_name, target.with_feature(Target::NoRuntime));
    InjectHexagonRpc injector(hexagon_module);
    s = injector.inject(s);

    if (!hexagon_module.functions().empty()) {
        containing_module.append(hexagon_module);
        containing_module.append(shared_runtime);
    }

    return s;
}

Buffer<uint8_t> compile_module_to_hexagon_shared_object(const Module &device_code) {
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(device_code, context));

    llvm::SmallVector<char, 4096> object;
    llvm::raw_svector_ostream object_stream(object);
    compile_llvm_module_to_object(*llvm_module, object_stream);

    int min_debug_level = device_code.name() == runtime_module_name ? 3 : 2;
    if (debug::debug_level() >= min_debug_level) {
        debug(0) << "Hexagon device code assembly: " << "\n";
        llvm::SmallString<4096> assembly;
        llvm::raw_svector_ostream assembly_stream(assembly);
        compile_llvm_module_to_assembly(*llvm_module, assembly_stream);
        debug(0) << assembly.c_str() << "\n";
    }

    auto obj = Elf::Object::parse_object(object.data(), object.size());
    internal_assert(obj);

    // Generate just one .text section.
    obj->merge_text_sections();

    // Make .bss a real section.
    auto bss = obj->find_section(".bss");
    if (bss != obj->sections_end()) {
        bss->set_alignment(128);
        // TODO: We should set the type to SHT_NOBITS
        // This will cause a difference in MemSize and FileSize like so:
        //        FileSize = (MemSize - size_of_bss)
        // When the Hexagon loader is used on 8998 and later targets,
        // the difference is filled with zeroes thereby initializing the .bss
        // section.
        bss->set_type(Elf::Section::SHT_PROGBITS);
        std::fill(bss->contents_begin(), bss->contents_end(), 0);
    }

    auto dtors = obj->find_section(".dtors");
    if (dtors != obj->sections_end()) {
        dtors->append_contents((uint32_t) 0);
    }

    // We call the constructors in ctors backwards starting from special
    // symbol __CTOR_END__ until we reach a 0 (NULL pointer value). So,
    // prepend the .ctors section with 0.
    auto ctors = obj->find_section(".ctors");
    if (ctors != obj->sections_end()) {
        ctors->prepend_contents((uint32_t) 0);
    }

    debug(2) << print_sections(*obj);

    // Link into a shared object.
    std::string soname = "lib" + device_code.name() + ".so";
    Elf::HexagonLinker linker(device_code.target());
    std::vector<std::string> dependencies = {
        "libhalide_hexagon_remote_skel.so",
    };
    std::vector<char> shared_object = obj->write_shared_object(&linker, dependencies, soname);

    std::string signer = get_env_variable("HL_HEXAGON_CODE_SIGNER");
    if (!signer.empty()) {
        // If signer is specified, shell out to a tool/script that will
        // sign the Hexagon code in a specific way. The tool is expected
        // to be of the form
        //
        //     signer /path/to/unsigned.so /path/to/signed.so
        //
        // where unsigned and signed paths must not be the same file.
        // If the signed file already exists, it will be overwritten.

        TemporaryFile input("hvx_unsigned", ".so");
        TemporaryFile output("hvx_signed", ".so");

        debug(1) << "Signing Hexagon code: " << input.pathname() << " -> " << output.pathname() << "\n";

        {
            std::ofstream f(input.pathname());
            f.write(shared_object.data(), shared_object.size());
            f.flush();
            internal_assert(f.good());
            f.close();
        }

        debug(1) << "Signing tool: (" << signer << ")\n";
        std::string cmd = signer + " " + input.pathname() + " " + output.pathname();
        int result = system(cmd.c_str());
        internal_assert(result == 0)
            << "HL_HEXAGON_CODE_SIGNER failed: result = " << result
            << " for cmd (" << cmd << ")";

        {
            std::ifstream f(output.pathname());
            f.seekg(0, std::ifstream::end);
            size_t signed_size = f.tellg();
            shared_object.resize(signed_size);
            f.seekg(0, std::ifstream::beg);
            f.read(shared_object.data(), shared_object.size());
            internal_assert(f.good());
            f.close();
        }
    }

    Halide::Buffer<uint8_t> result_buf(shared_object.size(), device_code.name());
    memcpy(result_buf.data(), shared_object.data(), shared_object.size());

    return result_buf;
}

namespace Elf {

// This array lists all the instruction encodings for hexagon
// instructions. This is just a summary for the purpose of
// implementing masks for relocations. See the Hexagon V6x
// Programmer's Reference Manual for the details. All instructions are
// 32-bit. The fields mean:
//
// 0/1 bits identify the instruction
//
// - bits are don't matter
//
// P bits tell you where the instruction is in an instruction packet.
//
// s,d,t,x,y bits specify the register of one of the operands
//
// i bits are the immediate field. These are the bits you care about if
//   you're doing instruction-specific relocations. They should be zero
//   for unrelocated instructions.
//
// If you encounter a new type of instruction that requires an
// instruction-specific relocation, look for a matching sequence of 0/1
// bits in this list, then derive the mask from the location of the i
// bits.
//
// Note that this this list does not include duplex instructions, which
// are two 16-bit instructions packed into a single 32-bit value. The
// Programmer's Reference Manual is unclear on how those are
// encoded. Look for a plausible sequence of zero bits, put the immediate
// in there.
//
// The best way to test if you're relocating properly is to dump the
// object after doing relocations, then run it through the hexagon
// disassembler and see how it interprets the relocated instructions.

const char *hexagon_instructions[] = {
    "0001000000iissssPP0IIIIIiiiiiii-",
    "0001000000iissssPP1IIIIIiiiiiii-",
    "0001000001iissssPP0IIIIIiiiiiii-",
    "0001000001iissssPP1IIIIIiiiiiii-",
    "0001000010iissssPP0IIIIIiiiiiii-",
    "0001000010iissssPP1IIIIIiiiiiii-",
    "0001000011iissssPP0IIIIIiiiiiii-",
    "0001000011iissssPP1IIIIIiiiiiii-",
    "0001000100iissssPP0IIIIIiiiiiii-",
    "0001000100iissssPP1IIIIIiiiiiii-",
    "0001000101iissssPP0IIIIIiiiiiii-",
    "0001000101iissssPP1IIIIIiiiiiii-",
    "0001000110iissssPP0---00iiiiiii-",
    "0001000110iissssPP0---01iiiiiii-",
    "0001000110iissssPP0---11iiiiiii-",
    "0001000110iissssPP1---00iiiiiii-",
    "0001000110iissssPP1---01iiiiiii-",
    "0001000110iissssPP1---11iiiiiii-",
    "0001000111iissssPP0---00iiiiiii-",
    "0001000111iissssPP0---01iiiiiii-",
    "0001000111iissssPP0---11iiiiiii-",
    "0001000111iissssPP1---00iiiiiii-",
    "0001000111iissssPP1---01iiiiiii-",
    "0001000111iissssPP1---11iiiiiii-",
    "0001001000iissssPP0IIIIIiiiiiii-",
    "0001001000iissssPP1IIIIIiiiiiii-",
    "0001001001iissssPP0IIIIIiiiiiii-",
    "0001001001iissssPP1IIIIIiiiiiii-",
    "0001001010iissssPP0IIIIIiiiiiii-",
    "0001001010iissssPP1IIIIIiiiiiii-",
    "0001001011iissssPP0IIIIIiiiiiii-",
    "0001001011iissssPP1IIIIIiiiiiii-",
    "0001001100iissssPP0IIIIIiiiiiii-",
    "0001001100iissssPP1IIIIIiiiiiii-",
    "0001001101iissssPP0IIIIIiiiiiii-",
    "0001001101iissssPP1IIIIIiiiiiii-",
    "0001001110iissssPP0---00iiiiiii-",
    "0001001110iissssPP0---01iiiiiii-",
    "0001001110iissssPP0---11iiiiiii-",
    "0001001110iissssPP1---00iiiiiii-",
    "0001001110iissssPP1---01iiiiiii-",
    "0001001110iissssPP1---11iiiiiii-",
    "0001001111iissssPP0---00iiiiiii-",
    "0001001111iissssPP0---01iiiiiii-",
    "0001001111iissssPP0---11iiiiiii-",
    "0001001111iissssPP1---00iiiiiii-",
    "0001001111iissssPP1---01iiiiiii-",
    "0001001111iissssPP1---11iiiiiii-",
    "0001010000iissssPP00ttttiiiiiii-",
    "0001010000iissssPP01ttttiiiiiii-",
    "0001010000iissssPP10ttttiiiiiii-",
    "0001010000iissssPP11ttttiiiiiii-",
    "0001010001iissssPP00ttttiiiiiii-",
    "0001010001iissssPP01ttttiiiiiii-",
    "0001010001iissssPP10ttttiiiiiii-",
    "0001010001iissssPP11ttttiiiiiii-",
    "0001010010iissssPP00ttttiiiiiii-",
    "0001010010iissssPP01ttttiiiiiii-",
    "0001010010iissssPP10ttttiiiiiii-",
    "0001010010iissssPP11ttttiiiiiii-",
    "0001010011iissssPP00ttttiiiiiii-",
    "0001010011iissssPP01ttttiiiiiii-",
    "0001010011iissssPP10ttttiiiiiii-",
    "0001010011iissssPP11ttttiiiiiii-",
    "0001010100iissssPP00ttttiiiiiii-",
    "0001010100iissssPP01ttttiiiiiii-",
    "0001010100iissssPP10ttttiiiiiii-",
    "0001010100iissssPP11ttttiiiiiii-",
    "0001010101iissssPP00ttttiiiiiii-",
    "0001010101iissssPP01ttttiiiiiii-",
    "0001010101iissssPP10ttttiiiiiii-",
    "0001010101iissssPP11ttttiiiiiii-",
    "00010110--iiddddPPIIIIIIiiiiiii-",
    "00010111--iissssPP--ddddiiiiiii-",
    "0010000000ii-sssPP0tttttiiiiiii-",
    "0010000000ii-sssPP1tttttiiiiiii-",
    "0010000001ii-sssPP0tttttiiiiiii-",
    "0010000001ii-sssPP1tttttiiiiiii-",
    "0010000010ii-sssPP0tttttiiiiiii-",
    "0010000010ii-sssPP1tttttiiiiiii-",
    "0010000011ii-sssPP0tttttiiiiiii-",
    "0010000011ii-sssPP1tttttiiiiiii-",
    "0010000100ii-sssPP0tttttiiiiiii-",
    "0010000100ii-sssPP1tttttiiiiiii-",
    "0010000101ii-sssPP0tttttiiiiiii-",
    "0010000101ii-sssPP1tttttiiiiiii-",
    "0010000110ii-sssPP0tttttiiiiiii-",
    "0010000110ii-sssPP1tttttiiiiiii-",
    "0010000111ii-sssPP0tttttiiiiiii-",
    "0010000111ii-sssPP1tttttiiiiiii-",
    "0010001000ii-sssPP0tttttiiiiiii-",
    "0010001000ii-sssPP1tttttiiiiiii-",
    "0010001001ii-sssPP0tttttiiiiiii-",
    "0010001001ii-sssPP1tttttiiiiiii-",
    "0010010000ii-sssPP0IIIIIiiiiiii-",
    "0010010000ii-sssPP1IIIIIiiiiiii-",
    "0010010001ii-sssPP0IIIIIiiiiiii-",
    "0010010001ii-sssPP1IIIIIiiiiiii-",
    "0010010010ii-sssPP0IIIIIiiiiiii-",
    "0010010010ii-sssPP1IIIIIiiiiiii-",
    "0010010011ii-sssPP0IIIIIiiiiiii-",
    "0010010011ii-sssPP1IIIIIiiiiiii-",
    "0010010100ii-sssPP0IIIIIiiiiiii-",
    "0010010100ii-sssPP1IIIIIiiiiiii-",
    "0010010101ii-sssPP0IIIIIiiiiiii-",
    "0010010101ii-sssPP1IIIIIiiiiiii-",
    "0010010110ii-sssPP0-----iiiiiii-",
    "0010010110ii-sssPP1-----iiiiiii-",
    "0010010111ii-sssPP0-----iiiiiii-",
    "0010010111ii-sssPP1-----iiiiiii-",
    "0010011000ii-sssPP0-----iiiiiii-",
    "0010011000ii-sssPP1-----iiiiiii-",
    "0010011001ii-sssPP0-----iiiiiii-",
    "0010011001ii-sssPP1-----iiiiiii-",
    "0010011010ii-sssPP0-----iiiiiii-",
    "0010011010ii-sssPP1-----iiiiiii-",
    "0010011011ii-sssPP0-----iiiiiii-",
    "0010011011ii-sssPP1-----iiiiiii-",
    "00110000000sssssPPitttttivvddddd",
    "00110000001sssssPPitttttivvddddd",
    "00110000010sssssPPitttttivvddddd",
    "00110000011sssssPPitttttivvddddd",
    "00110000100sssssPPitttttivvddddd",
    "00110000110sssssPPitttttivvddddd",
    "00110001000sssssPPitttttivvddddd",
    "00110001001sssssPPitttttivvddddd",
    "00110001010sssssPPitttttivvddddd",
    "00110001011sssssPPitttttivvddddd",
    "00110001100sssssPPitttttivvddddd",
    "00110001110sssssPPitttttivvddddd",
    "00110010000sssssPPitttttivvddddd",
    "00110010001sssssPPitttttivvddddd",
    "00110010010sssssPPitttttivvddddd",
    "00110010011sssssPPitttttivvddddd",
    "00110010100sssssPPitttttivvddddd",
    "00110010110sssssPPitttttivvddddd",
    "00110011000sssssPPitttttivvddddd",
    "00110011001sssssPPitttttivvddddd",
    "00110011010sssssPPitttttivvddddd",
    "00110011011sssssPPitttttivvddddd",
    "00110011100sssssPPitttttivvddddd",
    "00110011110sssssPPitttttivvddddd",
    "00110100000sssssPPiuuuuuivvttttt",
    "00110100010sssssPPiuuuuuivvttttt",
    "00110100011sssssPPiuuuuuivvttttt",
    "00110100100sssssPPiuuuuuivvttttt",
    "00110100101sssssPPiuuuuuivv00ttt",
    "00110100101sssssPPiuuuuuivv01ttt",
    "00110100101sssssPPiuuuuuivv10ttt",
    "00110100110sssssPPiuuuuuivvttttt",
    "00110101000sssssPPiuuuuuivvttttt",
    "00110101010sssssPPiuuuuuivvttttt",
    "00110101011sssssPPiuuuuuivvttttt",
    "00110101100sssssPPiuuuuuivvttttt",
    "00110101101sssssPPiuuuuuivv00ttt",
    "00110101101sssssPPiuuuuuivv01ttt",
    "00110101101sssssPPiuuuuuivv10ttt",
    "00110101110sssssPPiuuuuuivvttttt",
    "00110110000sssssPPiuuuuuivvttttt",
    "00110110010sssssPPiuuuuuivvttttt",
    "00110110011sssssPPiuuuuuivvttttt",
    "00110110100sssssPPiuuuuuivvttttt",
    "00110110101sssssPPiuuuuuivv00ttt",
    "00110110101sssssPPiuuuuuivv01ttt",
    "00110110101sssssPPiuuuuuivv10ttt",
    "00110110110sssssPPiuuuuuivvttttt",
    "00110111000sssssPPiuuuuuivvttttt",
    "00110111010sssssPPiuuuuuivvttttt",
    "00110111011sssssPPiuuuuuivvttttt",
    "00110111100sssssPPiuuuuuivvttttt",
    "00110111101sssssPPiuuuuuivv00ttt",
    "00110111101sssssPPiuuuuuivv01ttt",
    "00110111101sssssPPiuuuuuivv10ttt",
    "00110111110sssssPPiuuuuuivvttttt",
    "00111000000sssssPPIiiiiiivvIIIII",
    "00111000001sssssPPIiiiiiivvIIIII",
    "00111000010sssssPPIiiiiiivvIIIII",
    "00111000100sssssPPIiiiiiivvIIIII",
    "00111000101sssssPPIiiiiiivvIIIII",
    "00111000110sssssPPIiiiiiivvIIIII",
    "00111001000sssssPPIiiiiiivvIIIII",
    "00111001001sssssPPIiiiiiivvIIIII",
    "00111001010sssssPPIiiiiiivvIIIII",
    "00111001100sssssPPIiiiiiivvIIIII",
    "00111001101sssssPPIiiiiiivvIIIII",
    "00111001110sssssPPIiiiiiivvIIIII",
    "00111010000sssssPPittttti--ddddd",
    "00111010001sssssPPittttti--ddddd",
    "00111010010sssssPPittttti--ddddd",
    "00111010011sssssPPittttti--ddddd",
    "00111010100sssssPPittttti--ddddd",
    "00111010110sssssPPittttti--ddddd",
    "00111011000sssssPPiuuuuui--ttttt",
    "00111011010sssssPPiuuuuui--ttttt",
    "00111011011sssssPPiuuuuui--ttttt",
    "00111011100sssssPPiuuuuui--ttttt",
    "00111011101sssssPPiuuuuui--00ttt",
    "00111011101sssssPPiuuuuui--01ttt",
    "00111011101sssssPPiuuuuui--10ttt",
    "00111011110sssssPPiuuuuui--ttttt",
    "0011110--00sssssPPIiiiiiiIIIIIII",
    "0011110--01sssssPPIiiiiiiIIIIIII",
    "0011110--10sssssPPIiiiiiiIIIIIII",
    "00111110-00sssssPP0iiiiii00ttttt",
    "00111110-00sssssPP0iiiiii01ttttt",
    "00111110-00sssssPP0iiiiii10ttttt",
    "00111110-00sssssPP0iiiiii11ttttt",
    "00111110-01sssssPP0iiiiii00ttttt",
    "00111110-01sssssPP0iiiiii01ttttt",
    "00111110-01sssssPP0iiiiii10ttttt",
    "00111110-01sssssPP0iiiiii11ttttt",
    "00111110-10sssssPP0iiiiii00ttttt",
    "00111110-10sssssPP0iiiiii01ttttt",
    "00111110-10sssssPP0iiiiii10ttttt",
    "00111110-10sssssPP0iiiiii11ttttt",
    "00111111-00sssssPP0iiiiii00IIIII",
    "00111111-00sssssPP0iiiiii01IIIII",
    "00111111-00sssssPP0iiiiii10IIIII",
    "00111111-00sssssPP0iiiiii11IIIII",
    "00111111-01sssssPP0iiiiii00IIIII",
    "00111111-01sssssPP0iiiiii01IIIII",
    "00111111-01sssssPP0iiiiii10IIIII",
    "00111111-01sssssPP0iiiiii11IIIII",
    "00111111-10sssssPP0iiiiii00IIIII",
    "00111111-10sssssPP0iiiiii01IIIII",
    "00111111-10sssssPP0iiiiii10IIIII",
    "00111111-10sssssPP0iiiiii11IIIII",
    "01000000000sssssPPitttttiiiii0vv",
    "01000000010sssssPPitttttiiiii0vv",
    "01000000011sssssPPitttttiiiii0vv",
    "01000000100sssssPPitttttiiiii0vv",
    "01000000101sssssPPi00tttiiiii0vv",
    "01000000101sssssPPi01tttiiiii0vv",
    "01000000101sssssPPi10tttiiiii0vv",
    "01000000110sssssPPitttttiiiii0vv",
    "01000001000sssssPP0ttiiiiiiddddd",
    "01000001001sssssPP0ttiiiiiiddddd",
    "01000001010sssssPP0ttiiiiiiddddd",
    "01000001011sssssPP0ttiiiiiiddddd",
    "01000001100sssssPP0ttiiiiiiddddd",
    "01000001110sssssPP0ttiiiiiiddddd",
    "01000010000sssssPPitttttiiiii0vv",
    "01000010010sssssPPitttttiiiii0vv",
    "01000010011sssssPPitttttiiiii0vv",
    "01000010100sssssPPitttttiiiii0vv",
    "01000010101sssssPPi00tttiiiii0vv",
    "01000010101sssssPPi01tttiiiii0vv",
    "01000010101sssssPPi10tttiiiii0vv",
    "01000010110sssssPPitttttiiiii0vv",
    "01000011000sssssPP0ttiiiiiiddddd",
    "01000011001sssssPP0ttiiiiiiddddd",
    "01000011010sssssPP0ttiiiiiiddddd",
    "01000011011sssssPP0ttiiiiiiddddd",
    "01000011100sssssPP0ttiiiiiiddddd",
    "01000011110sssssPP0ttiiiiiiddddd",
    "01000100000sssssPPitttttiiiii0vv",
    "01000100010sssssPPitttttiiiii0vv",
    "01000100011sssssPPitttttiiiii0vv",
    "01000100100sssssPPitttttiiiii0vv",
    "01000100101sssssPPi00tttiiiii0vv",
    "01000100101sssssPPi01tttiiiii0vv",
    "01000100101sssssPPi10tttiiiii0vv",
    "01000100110sssssPPitttttiiiii0vv",
    "01000101000sssssPP0ttiiiiiiddddd",
    "01000101001sssssPP0ttiiiiiiddddd",
    "01000101010sssssPP0ttiiiiiiddddd",
    "01000101011sssssPP0ttiiiiiiddddd",
    "01000101100sssssPP0ttiiiiiiddddd",
    "01000101110sssssPP0ttiiiiiiddddd",
    "01000110000sssssPPitttttiiiii0vv",
    "01000110010sssssPPitttttiiiii0vv",
    "01000110011sssssPPitttttiiiii0vv",
    "01000110100sssssPPitttttiiiii0vv",
    "01000110101sssssPPi00tttiiiii0vv",
    "01000110101sssssPPi01tttiiiii0vv",
    "01000110101sssssPPi10tttiiiii0vv",
    "01000110110sssssPPitttttiiiii0vv",
    "01000111000sssssPP0ttiiiiiiddddd",
    "01000111001sssssPP0ttiiiiiiddddd",
    "01000111010sssssPP0ttiiiiiiddddd",
    "01000111011sssssPP0ttiiiiiiddddd",
    "01000111100sssssPP0ttiiiiiiddddd",
    "01000111110sssssPP0ttiiiiiiddddd",
    "01001ii0000iiiiiPPitttttiiiiiiii",
    "01001ii0010iiiiiPPitttttiiiiiiii",
    "01001ii0011iiiiiPPitttttiiiiiiii",
    "01001ii0100iiiiiPPitttttiiiiiiii",
    "01001ii0101iiiiiPPi00tttiiiiiiii",
    "01001ii0101iiiiiPPi01tttiiiiiiii",
    "01001ii0101iiiiiPPi10tttiiiiiiii",
    "01001ii0110iiiiiPPitttttiiiiiiii",
    "01001ii1000iiiiiPPiiiiiiiiiddddd",
    "01001ii1001iiiiiPPiiiiiiiiiddddd",
    "01001ii1010iiiiiPPiiiiiiiiiddddd",
    "01001ii1011iiiiiPPiiiiiiiiiddddd",
    "01001ii1100iiiiiPPiiiiiiiiiddddd",
    "01001ii1110iiiiiPPiiiiiiiiiddddd",
    "01010000101sssssPP--------------",
    "01010001000sssssPP----uu--------",
    "01010001001sssssPP----uu--------",
    "01010010100sssssPP--------------",
    "01010010101sssssPP--------------",
    "01010011010sssssPP-00-uu--------",
    "01010011010sssssPP-01-uu--------",
    "01010011010sssssPP-10-uu--------",
    "01010011010sssssPP-11-uu--------",
    "01010011011sssssPP-00-uu--------",
    "01010011011sssssPP-01-uu--------",
    "01010011011sssssPP-10-uu--------",
    "01010011011sssssPP-11-uu--------",
    "0101010000------PP-iiiii---iii--",
    "0101010001------PP-iiiii---iii--",
    "0101010010------PP-iiiii---iii--",
    "01010110110sssssPP000-----------",
    "0101011111000000PP0---0000000010",
    "0101100iiiiiiiiiPPiiiiiiiiiiiii-",
    "0101101iiiiiiiiiPPiiiiiiiiiiiii0",
    "01011100ii0iiiiiPPi00-uuiiiiiii-",
    "01011100ii0iiiiiPPi01-uuiiiiiii-",
    "01011100ii0iiiiiPPi10-uuiiiiiii-",
    "01011100ii0iiiiiPPi11-uuiiiiiii-",
    "01011100ii1iiiiiPPi00-uuiiiiiii-",
    "01011100ii1iiiiiPPi01-uuiiiiiii-",
    "01011100ii1iiiiiPPi10-uuiiiiiii-",
    "01011100ii1iiiiiPPi11-uuiiiiiii-",
    "01011101ii0iiiiiPPi-0-uuiiiiiii-",
    "01011101ii1iiiiiPPi-0-uuiiiiiii-",
    "01100000000sssssPP-iiiii---ii---",
    "01100000001sssssPP-iiiii---ii---",
    "01100000101sssssPP-iiiii---ii---",
    "01100000110sssssPP-iiiii---ii---",
    "01100000111sssssPP-iiiii---ii---",
    "0110000100isssssPPi0iiiiiiiiiii-",
    "0110000100isssssPPi1iiiiiiiiiii-",
    "0110000101isssssPPi0iiiiiiiiiii-",
    "0110000101isssssPPi1iiiiiiiiiii-",
    "0110000110isssssPPi0iiiiiiiiiii-",
    "0110000110isssssPPi1iiiiiiiiiii-",
    "0110000111isssssPPi0iiiiiiiiiii-",
    "0110000111isssssPPi1iiiiiiiiiii-",
    "01100010001sssssPP---------ddddd",
    "01100010010sssssPP--------------",
    "01100011001sssssPP---------ddddd",
    "01101000000sssssPP---------ddddd",
    "01101001000IIIIIPP-iiiiiIIIii-II",
    "01101001001IIIIIPP-iiiiiIIIii-II",
    "01101001101IIIIIPP-iiiiiIIIii-II",
    "01101001110IIIIIPP-iiiiiIIIii-II",
    "01101001111IIIIIPP-iiiiiIIIii-II",
    "01101010000sssssPP---------ddddd",
    "0110101001001001PP-iiiiii--ddddd",
    "011010110000--ssPP0---tt------dd",
    "011010110000--ssPP1---tt1--1--dd",
    "011010110001--ssPP0---ttuu----dd",
    "011010110001--ssPP1---tt1--1--dd",
    "011010110010--ssPP0---tt------dd",
    "011010110011--ssPP0---ttuu----dd",
    "011010110100--ssPP0---tt------dd",
    "011010110101--ssPP0---ttuu----dd",
    "011010110110--ssPP0---tt------dd",
    "011010110111--ssPP0---ttuu----dd",
    "011010111000--ssPP0-----------dd",
    "011010111001--ssPP0---ttuu----dd",
    "011010111010--ssPP0-----------dd",
    "011010111011--ssPP0---ttuu----dd",
    "011010111100--ssPP0-----------dd",
    "011010111101--ssPP0---ttuu----dd",
    "011010111110--ssPP0---tt------dd",
    "011010111111--ssPP0---ttuu----dd",
    "01101100001-----PP------000-----",
    "01110000000sssssPP0--------ddddd",
    "01110000000sssssPP1-00uu---ddddd",
    "01110000000sssssPP1-01uu---ddddd",
    "01110000000sssssPP1-10uu---ddddd",
    "01110000000sssssPP1-11uu---ddddd",
    "01110000001sssssPP0--------ddddd",
    "01110000001sssssPP1-00uu---ddddd",
    "01110000001sssssPP1-01uu---ddddd",
    "01110000001sssssPP1-10uu---ddddd",
    "01110000001sssssPP1-11uu---ddddd",
    "01110000011sssssPP0--------ddddd",
    "01110000100sssssPP1-00uu---ddddd",
    "01110000100sssssPP1-01uu---ddddd",
    "01110000100sssssPP1-10uu---ddddd",
    "01110000100sssssPP1-11uu---ddddd",
    "01110000101sssssPP0--------ddddd",
    "01110000101sssssPP1-00uu---ddddd",
    "01110000101sssssPP1-01uu---ddddd",
    "01110000101sssssPP1-10uu---ddddd",
    "01110000101sssssPP1-11uu---ddddd",
    "01110000110sssssPP0--------ddddd",
    "01110000110sssssPP1-00uu---ddddd",
    "01110000110sssssPP1-01uu---ddddd",
    "01110000110sssssPP1-10uu---ddddd",
    "01110000110sssssPP1-11uu---ddddd",
    "01110000111sssssPP0--------ddddd",
    "01110000111sssssPP1-00uu---ddddd",
    "01110000111sssssPP1-01uu---ddddd",
    "01110000111sssssPP1-10uu---ddddd",
    "01110000111sssssPP1-11uu---ddddd",
    "01110001ii1xxxxxPPiiiiiiiiiiiiii",
    "01110010ii1xxxxxPPiiiiiiiiiiiiii",
    "01110011-00sssssPP1iiiiiiiiddddd",
    "01110011-01sssssPP1iiiiiiiiddddd",
    "011100110uusssssPP0iiiiiiiiddddd",
    "01110011-10sssssPP1iiiiiiiiddddd",
    "01110011-11sssssPP1iiiiiiiiddddd",
    "011100111uusssssPP0iiiiiiiiddddd",
    "011101000uusssssPP0iiiiiiiiddddd",
    "011101000uusssssPP1iiiiiiiiddddd",
    "011101001uusssssPP0iiiiiiiiddddd",
    "011101001uusssssPP1iiiiiiiiddddd",
    "0111010100isssssPPiiiiiiiii000dd",
    "0111010100isssssPPiiiiiiiii100dd",
    "0111010101isssssPPiiiiiiiii000dd",
    "0111010101isssssPPiiiiiiiii100dd",
    "01110101100sssssPPiiiiiiiii000dd",
    "01110101100sssssPPiiiiiiiii100dd",
    "0111011000isssssPPiiiiiiiiiddddd",
    "0111011001isssssPPiiiiiiiiiddddd",
    "0111011010isssssPPiiiiiiiiiddddd",
    "01111000ii-iiiiiPPiiiiiiiiiddddd",
    "0111101uuIIIIIIIPPIiiiiiiiiddddd",
    "011111000IIIIIIIPPIiiiiiiiiddddd",
    "011111001--IIIIIPPIiiiiiiiiddddd",
    "011111100uu0iiiiPP0iiiiiiiiddddd",
    "011111100uu0iiiiPP1iiiiiiiiddddd",
    "011111101uu0iiiiPP0iiiiiiiiddddd",
    "011111101uu0iiiiPP1iiiiiiiiddddd",
    "01111111--------PP--------------",
    "10000000000sssssPP------100ddddd",
    "10000000000sssssPP------101ddddd",
    "10000000000sssssPP------110ddddd",
    "10000000000sssssPP------111ddddd",
    "10000000000sssssPPiiiiii000ddddd",
    "10000000000sssssPPiiiiii001ddddd",
    "10000000000sssssPPiiiiii010ddddd",
    "10000000000sssssPPiiiiii011ddddd",
    "10000000001sssssPP00iiii000ddddd",
    "10000000010sssssPP------100ddddd",
    "10000000010sssssPP------101ddddd",
    "10000000010sssssPP------110ddddd",
    "10000000010sssssPP------111ddddd",
    "10000000100sssssPP------100ddddd",
    "10000000100sssssPP------101ddddd",
    "10000000100sssssPP------110ddddd",
    "10000000100sssssPP------111ddddd",
    "10000000110sssssPP------100ddddd",
    "10000000110sssssPP------101ddddd",
    "10000000110sssssPP------110ddddd",
    "10000000111sssssPP0-----000ddddd",
    "10000000111sssssPP0-----001ddddd",
    "10000000111sssssPP0-----010ddddd",
    "10000000111sssssPP0-----011ddddd",
    "10000000111sssssPP0-----110ddddd",
    "10000000111sssssPP0-----111ddddd",
    "10000001IIIsssssPPiiiiiiIIIddddd",
    "1000001000-sssssPPiiiiii000xxxxx",
    "1000001000-sssssPPiiiiii001xxxxx",
    "1000001000-sssssPPiiiiii010xxxxx",
    "1000001000-sssssPPiiiiii011xxxxx",
    "1000001000-sssssPPiiiiii100xxxxx",
    "1000001000-sssssPPiiiiii101xxxxx",
    "1000001000-sssssPPiiiiii110xxxxx",
    "1000001000-sssssPPiiiiii111xxxxx",
    "1000001001-sssssPPiiiiii000xxxxx",
    "1000001001-sssssPPiiiiii001xxxxx",
    "1000001001-sssssPPiiiiii010xxxxx",
    "1000001001-sssssPPiiiiii011xxxxx",
    "1000001001-sssssPPiiiiii100xxxxx",
    "1000001001-sssssPPiiiiii101xxxxx",
    "1000001001-sssssPPiiiiii110xxxxx",
    "1000001001-sssssPPiiiiii111xxxxx",
    "1000001010-sssssPPiiiiii001xxxxx",
    "1000001010-sssssPPiiiiii010xxxxx",
    "1000001010-sssssPPiiiiii011xxxxx",
    "10000011IIIsssssPPiiiiiiIIIxxxxx",
    "1000010000-sssssPP------00-ddddd",
    "1000010000-sssssPP------01-ddddd",
    "1000010000-sssssPP------10-ddddd",
    "1000010000-sssssPP------11-ddddd",
    "1000010001-sssssPP------00-ddddd",
    "1000010001-sssssPP------01-ddddd",
    "1000010001-sssssPP------10-ddddd",
    "100001001--sssssPP------000ddddd",
    "100001001--sssssPP------001ddddd",
    "100001001--sssssPP------010ddddd",
    "100001001--sssssPP------011ddddd",
    "100001001--sssssPP------100ddddd",
    "100001001--sssssPP------101ddddd",
    "100001001--sssssPP------110ddddd",
    "10000101010sssssPP------------dd",
    "10000101100sssssPPiiiiii------dd",
    "10000101101sssssPPiiiiii------dd",
    "10000101111sssssPP0iiiii------dd",
    "10000110--------PP----tt---ddddd",
    "1000011100isssssPPIIIIIIiiixxxxx",
    "1000011101isssssPPIIIIIIiiixxxxx",
    "1000011110isssssPPIIIIIIiiixxxxx",
    "1000011111isssssPPIIIIIIiiixxxxx",
    "10001000000sssssPP------001ddddd",
    "10001000001sssssPP------001ddddd",
    "10001000010sssssPP------000ddddd",
    "10001000010sssssPP------001ddddd",
    "10001000010sssssPP------010ddddd",
    "10001000010sssssPP------100ddddd",
    "10001000011sssssPP------000ddddd",
    "10001000011sssssPP------001ddddd",
    "10001000011sssssPP00iiii100ddddd",
    "10001000011sssssPP00iiii101ddddd",
    "10001000011sssssPP------011ddddd",
    "10001000011sssssPPiiiiii010ddddd",
    "10001000100sssssPP------000ddddd",
    "10001000100sssssPP------001ddddd",
    "10001000100sssssPP------010ddddd",
    "10001000100sssssPP------100ddddd",
    "10001000100sssssPP------110ddddd",
    "10001000101sssssPP------001ddddd",
    "10001000110sssssPP------000ddddd",
    "10001000110sssssPP------001ddddd",
    "10001000110sssssPP0iiiii100ddddd",
    "10001000111sssssPP------001ddddd",
    "10001000111sssssPP------010ddddd",
    "10001000111sssssPP------100ddddd",
    "10001001-1----ssPP---------ddddd",
    "10001010IIIsssssPPiiiiiiIIIddddd",
    "10001011001sssssPP------000ddddd",
    "10001011010sssssPP------000ddddd",
    "10001011011sssssPP------000ddddd",
    "10001011011sssssPP------001ddddd",
    "10001011100sssssPP------000ddddd",
    "10001011100sssssPP------001ddddd",
    "10001011101sssssPP------000ddddd",
    "10001011111sssssPP------0eeddddd",
    "10001100000sssssPP0iiiii000ddddd",
    "10001100000sssssPP0iiiii001ddddd",
    "10001100000sssssPP0iiiii010ddddd",
    "10001100000sssssPP0iiiii011ddddd",
    "10001100000sssssPP------100ddddd",
    "10001100000sssssPP------101ddddd",
    "10001100000sssssPP------110ddddd",
    "10001100000sssssPP------111ddddd",
    "10001100001sssssPPiiiiii000ddddd",
    "10001100010sssssPP0iiiii010ddddd",
    "10001100010sssssPP------100ddddd",
    "10001100010sssssPP------101ddddd",
    "10001100010sssssPP------110ddddd",
    "10001100010sssssPP------111ddddd",
    "10001100100sssssPP------100ddddd",
    "10001100100sssssPP------101ddddd",
    "10001100100sssssPP------110ddddd",
    "10001100110sssssPP0iiiii000ddddd",
    "10001100110sssssPP0iiiii001ddddd",
    "10001100110sssssPP0iiiii010ddddd",
    "10001100110sssssPP------100ddddd",
    "10001100110sssssPP------101ddddd",
    "10001100110sssssPP------110ddddd",
    "10001100110sssssPP------111ddddd",
    "10001100111sssssPP0iiiii00-ddddd",
    "10001100111sssssPP0iiiii10-ddddd",
    "10001100111sssssPP0iiiii11-ddddd",
    "100011010IIsssssPP0iiiiiIIIddddd",
    "100011011IIsssssPP0iiiiiIIIddddd",
    "1000111000-sssssPP0iiiii000xxxxx",
    "1000111000-sssssPP0iiiii001xxxxx",
    "1000111000-sssssPP0iiiii010xxxxx",
    "1000111000-sssssPP0iiiii011xxxxx",
    "1000111000-sssssPP0iiiii100xxxxx",
    "1000111000-sssssPP0iiiii101xxxxx",
    "1000111000-sssssPP0iiiii110xxxxx",
    "1000111000-sssssPP0iiiii111xxxxx",
    "1000111001-sssssPP0iiiii000xxxxx",
    "1000111001-sssssPP0iiiii001xxxxx",
    "1000111001-sssssPP0iiiii010xxxxx",
    "1000111001-sssssPP0iiiii011xxxxx",
    "1000111001-sssssPP0iiiii100xxxxx",
    "1000111001-sssssPP0iiiii101xxxxx",
    "1000111001-sssssPP0iiiii110xxxxx",
    "1000111001-sssssPP0iiiii111xxxxx",
    "1000111010-sssssPP0iiiii001xxxxx",
    "1000111010-sssssPP0iiiii010xxxxx",
    "1000111010-sssssPP0iiiii011xxxxx",
    "100011110IIsssssPP0iiiiiIIIxxxxx",
    "1001000000011110PP0--------11110",
    "10010010000sssssPP00------0ddddd",
    "10010010000sssssPP01------0ddddd",
    "10010100000sssssPP0--iiiiiiiiiii",
    "1001011000011110PP0000-----11110",
    "1001011000011110PP0010ss---11110",
    "1001011000011110PP0100ss---11110",
    "1001011000011110PP0110ss---11110",
    "1001011000011110PP1010ss---11110",
    "1001011000011110PP1100ss---11110",
    "1001011000011110PP1110ss---11110",
    "10010ii0001sssssPPiiiiiiiiiddddd",
    "10010ii0010sssssPPiiiiiiiiiyyyyy",
    "10010ii0011sssssPPiiiiiiiiiddddd",
    "10010ii0100sssssPPiiiiiiiiiyyyyy",
    "10010ii0101sssssPPiiiiiiiiiddddd",
    "10010ii0111sssssPPiiiiiiiiiddddd",
    "10010ii1000sssssPPiiiiiiiiiddddd",
    "10010ii1001sssssPPiiiiiiiiiddddd",
    "10010ii1010sssssPPiiiiiiiiiddddd",
    "10010ii1011sssssPPiiiiiiiiiddddd",
    "10010ii1100sssssPPiiiiiiiiiddddd",
    "10010ii1110sssssPPiiiiiiiiiddddd",
    "10011000001xxxxxPPu0--0iiiiddddd",
    "10011000001xxxxxPPu0--1-0--ddddd",
    "10011000010xxxxxPPu0--0iiiiyyyyy",
    "10011000010xxxxxPPu0--1-0--yyyyy",
    "10011000011xxxxxPPu0--0iiiiddddd",
    "10011000011xxxxxPPu0--1-0--ddddd",
    "10011000100xxxxxPPu0--0iiiiyyyyy",
    "10011000100xxxxxPPu0--1-0--yyyyy",
    "10011000101xxxxxPPu0--0iiiiddddd",
    "10011000101xxxxxPPu0--1-0--ddddd",
    "10011000111xxxxxPPu0--0iiiiddddd",
    "10011000111xxxxxPPu0--1-0--ddddd",
    "10011001000xxxxxPPu0--0iiiiddddd",
    "10011001000xxxxxPPu0--1-0--ddddd",
    "10011001001xxxxxPPu0--0iiiiddddd",
    "10011001001xxxxxPPu0--1-0--ddddd",
    "10011001010xxxxxPPu0--0iiiiddddd",
    "10011001010xxxxxPPu0--1-0--ddddd",
    "10011001011xxxxxPPu0--0iiiiddddd",
    "10011001011xxxxxPPu0--1-0--ddddd",
    "10011001100xxxxxPPu0--0iiiiddddd",
    "10011001100xxxxxPPu0--1-0--ddddd",
    "10011001110xxxxxPPu0--0iiiiddddd",
    "10011001110xxxxxPPu0--1-0--ddddd",
    "10011010001eeeeePP01IIII-IIddddd",
    "10011010001xxxxxPP00---iiiiddddd",
    "10011010010eeeeePP01IIII-IIyyyyy",
    "10011010010xxxxxPP00---iiiiyyyyy",
    "10011010011eeeeePP01IIII-IIddddd",
    "10011010011xxxxxPP00---iiiiddddd",
    "10011010100eeeeePP01IIII-IIyyyyy",
    "10011010100xxxxxPP00---iiiiyyyyy",
    "10011010101eeeeePP01IIII-IIddddd",
    "10011010101xxxxxPP00---iiiiddddd",
    "10011010111eeeeePP01IIII-IIddddd",
    "10011010111xxxxxPP00---iiiiddddd",
    "10011011000eeeeePP01IIII-IIddddd",
    "10011011000xxxxxPP00---iiiiddddd",
    "10011011000xxxxxPP100ttiiiiddddd",
    "10011011000xxxxxPP101ttiiiiddddd",
    "10011011000xxxxxPP110ttiiiiddddd",
    "10011011000xxxxxPP111ttiiiiddddd",
    "10011011001eeeeePP01IIII-IIddddd",
    "10011011001xxxxxPP00---iiiiddddd",
    "10011011001xxxxxPP100ttiiiiddddd",
    "10011011001xxxxxPP101ttiiiiddddd",
    "10011011001xxxxxPP110ttiiiiddddd",
    "10011011001xxxxxPP111ttiiiiddddd",
    "10011011010eeeeePP01IIII-IIddddd",
    "10011011010xxxxxPP00---iiiiddddd",
    "10011011010xxxxxPP100ttiiiiddddd",
    "10011011010xxxxxPP101ttiiiiddddd",
    "10011011010xxxxxPP110ttiiiiddddd",
    "10011011010xxxxxPP111ttiiiiddddd",
    "10011011011eeeeePP01IIII-IIddddd",
    "10011011011xxxxxPP00---iiiiddddd",
    "10011011011xxxxxPP100ttiiiiddddd",
    "10011011011xxxxxPP101ttiiiiddddd",
    "10011011011xxxxxPP110ttiiiiddddd",
    "10011011011xxxxxPP111ttiiiiddddd",
    "10011011100eeeeePP01IIII-IIddddd",
    "10011011100xxxxxPP00---iiiiddddd",
    "10011011100xxxxxPP100ttiiiiddddd",
    "10011011100xxxxxPP101ttiiiiddddd",
    "10011011100xxxxxPP110ttiiiiddddd",
    "10011011100xxxxxPP111ttiiiiddddd",
    "10011011110eeeeePP01IIII-IIddddd",
    "10011011110xxxxxPP00---iiiiddddd",
    "10011011110xxxxxPP100ttiiiiddddd",
    "10011011110xxxxxPP101ttiiiiddddd",
    "10011011110xxxxxPP110ttiiiiddddd",
    "10011011110xxxxxPP111ttiiiiddddd",
    "10011100001tttttPPi1IIIIiIIddddd",
    "10011100001xxxxxPPu0----0--ddddd",
    "10011100010tttttPPi1IIIIiIIyyyyy",
    "10011100010xxxxxPPu0----0--yyyyy",
    "10011100011tttttPPi1IIIIiIIddddd",
    "10011100011xxxxxPPu0----0--ddddd",
    "10011100100tttttPPi1IIIIiIIyyyyy",
    "10011100100xxxxxPPu0----0--yyyyy",
    "10011100101tttttPPi1IIIIiIIddddd",
    "10011100101xxxxxPPu0----0--ddddd",
    "10011100111tttttPPi1IIIIiIIddddd",
    "10011100111xxxxxPPu0----0--ddddd",
    "10011101000tttttPPi1IIIIiIIddddd",
    "10011101000xxxxxPPu0----0--ddddd",
    "10011101001tttttPPi1IIIIiIIddddd",
    "10011101001xxxxxPPu0----0--ddddd",
    "10011101010tttttPPi1IIIIiIIddddd",
    "10011101010xxxxxPPu0----0--ddddd",
    "10011101011tttttPPi1IIIIiIIddddd",
    "10011101011xxxxxPPu0----0--ddddd",
    "10011101100tttttPPi1IIIIiIIddddd",
    "10011101100xxxxxPPu0----0--ddddd",
    "10011101110tttttPPi1IIIIiIIddddd",
    "10011101110xxxxxPPu0----0--ddddd",
    "10011110001xxxxxPPu0----0--ddddd",
    "10011110010xxxxxPPu0----0--yyyyy",
    "10011110011xxxxxPPu0----0--ddddd",
    "10011110100xxxxxPPu0----0--yyyyy",
    "10011110101xxxxxPPu0----0--ddddd",
    "10011110111xxxxxPPu0----0--ddddd",
    "10011111000iiiiiPP100tti1--ddddd",
    "10011111000iiiiiPP101tti1--ddddd",
    "10011111000iiiiiPP110tti1--ddddd",
    "10011111000iiiiiPP111tti1--ddddd",
    "10011111000xxxxxPPu0----0--ddddd",
    "10011111001iiiiiPP100tti1--ddddd",
    "10011111001iiiiiPP101tti1--ddddd",
    "10011111001iiiiiPP110tti1--ddddd",
    "10011111001iiiiiPP111tti1--ddddd",
    "10011111001xxxxxPPu0----0--ddddd",
    "10011111010iiiiiPP100tti1--ddddd",
    "10011111010iiiiiPP101tti1--ddddd",
    "10011111010iiiiiPP110tti1--ddddd",
    "10011111010iiiiiPP111tti1--ddddd",
    "10011111010xxxxxPPu0----0--ddddd",
    "10011111011iiiiiPP100tti1--ddddd",
    "10011111011iiiiiPP101tti1--ddddd",
    "10011111011iiiiiPP110tti1--ddddd",
    "10011111011iiiiiPP111tti1--ddddd",
    "10011111011xxxxxPPu0----0--ddddd",
    "10011111100iiiiiPP100tti1--ddddd",
    "10011111100iiiiiPP101tti1--ddddd",
    "10011111100iiiiiPP110tti1--ddddd",
    "10011111100iiiiiPP111tti1--ddddd",
    "10011111100xxxxxPPu0----0--ddddd",
    "10011111110iiiiiPP100tti1--ddddd",
    "10011111110iiiiiPP101tti1--ddddd",
    "10011111110iiiiiPP110tti1--ddddd",
    "10011111110iiiiiPP111tti1--ddddd",
    "10011111110xxxxxPPu0----0--ddddd",
    "10100000000sssssPP--------------",
    "10100000001sssssPP--------------",
    "10100000010sssssPP--------------",
    "1010000010011101PP000iiiiiiiiiii",
    "10100000101sssssPP-ttttt------dd",
    "10100000110sssssPP0-------------",
    "10100000111sssssPP0ttttt------dd",
    "10100110000sssssPP-ttttt--------",
    "10100110100sssssPP-ttttt--------",
    "10100ii1000sssssPPitttttiiiiiiii",
    "10100ii1010sssssPPitttttiiiiiiii",
    "10100ii1011sssssPPitttttiiiiiiii",
    "10100ii1100sssssPPitttttiiiiiiii",
    "10100ii1101sssssPPi00tttiiiiiiii",
    "10100ii1101sssssPPi01tttiiiiiiii",
    "10100ii1101sssssPPi10tttiiiiiiii",
    "10100ii1110sssssPPitttttiiiiiiii",
    "10101000000-----PP--------------",
    "10101000010-----PP--------------",
    "10101001000xxxxxPPuttttt0-----1-",
    "10101001000xxxxxPPuttttt0iiii-0-",
    "10101001010xxxxxPPuttttt0-----1-",
    "10101001010xxxxxPPuttttt0iiii-0-",
    "10101001011xxxxxPPuttttt0-----1-",
    "10101001011xxxxxPPuttttt0iiii-0-",
    "10101001100xxxxxPPuttttt0-----1-",
    "10101001100xxxxxPPuttttt0iiii-0-",
    "10101001101xxxxxPPu00ttt0-----1-",
    "10101001101xxxxxPPu00ttt0iiii-0-",
    "10101001101xxxxxPPu01ttt0-----1-",
    "10101001101xxxxxPPu01ttt0iiii-0-",
    "10101001101xxxxxPPu10ttt0-----1-",
    "10101001101xxxxxPPu10ttt0iiii-0-",
    "10101001110xxxxxPPuttttt0-----1-",
    "10101001110xxxxxPPuttttt0iiii-0-",
    "10101011000eeeeePP0ttttt1-IIIIII",
    "10101011000xxxxxPP0ttttt0iiii-0-",
    "10101011000xxxxxPP1ttttt0iiii0vv",
    "10101011000xxxxxPP1ttttt0iiii1vv",
    "10101011000xxxxxPP1ttttt1iiii0vv",
    "10101011000xxxxxPP1ttttt1iiii1vv",
    "10101011010eeeeePP0ttttt1-IIIIII",
    "10101011010xxxxxPP0ttttt0iiii-0-",
    "10101011010xxxxxPP1ttttt0iiii0vv",
    "10101011010xxxxxPP1ttttt0iiii1vv",
    "10101011010xxxxxPP1ttttt1iiii0vv",
    "10101011010xxxxxPP1ttttt1iiii1vv",
    "10101011011eeeeePP0ttttt1-IIIIII",
    "10101011011xxxxxPP0ttttt0iiii-0-",
    "10101011011xxxxxPP1ttttt0iiii0vv",
    "10101011011xxxxxPP1ttttt0iiii1vv",
    "10101011011xxxxxPP1ttttt1iiii0vv",
    "10101011011xxxxxPP1ttttt1iiii1vv",
    "10101011100eeeeePP0ttttt1-IIIIII",
    "10101011100xxxxxPP0ttttt0iiii-0-",
    "10101011100xxxxxPP1ttttt0iiii0vv",
    "10101011100xxxxxPP1ttttt0iiii1vv",
    "10101011100xxxxxPP1ttttt1iiii0vv",
    "10101011100xxxxxPP1ttttt1iiii1vv",
    "10101011101eeeeePP000ttt1-IIIIII",
    "10101011101eeeeePP001ttt1-IIIIII",
    "10101011101eeeeePP010ttt1-IIIIII",
    "10101011101xxxxxPP000ttt0iiii-0-",
    "10101011101xxxxxPP001ttt0iiii-0-",
    "10101011101xxxxxPP010ttt0iiii-0-",
    "10101011101xxxxxPP100ttt0iiii0vv",
    "10101011101xxxxxPP100ttt0iiii1vv",
    "10101011101xxxxxPP100ttt1iiii0vv",
    "10101011101xxxxxPP100ttt1iiii1vv",
    "10101011101xxxxxPP101ttt0iiii0vv",
    "10101011101xxxxxPP101ttt0iiii1vv",
    "10101011101xxxxxPP101ttt1iiii0vv",
    "10101011101xxxxxPP101ttt1iiii1vv",
    "10101011101xxxxxPP110ttt0iiii0vv",
    "10101011101xxxxxPP110ttt0iiii1vv",
    "10101011101xxxxxPP110ttt1iiii0vv",
    "10101011101xxxxxPP110ttt1iiii1vv",
    "10101011110eeeeePP0ttttt1-IIIIII",
    "10101011110xxxxxPP0ttttt0iiii-0-",
    "10101011110xxxxxPP1ttttt0iiii0vv",
    "10101011110xxxxxPP1ttttt0iiii1vv",
    "10101011110xxxxxPP1ttttt1iiii0vv",
    "10101011110xxxxxPP1ttttt1iiii1vv",
    "10101101000uuuuuPPittttt1iIIIIII",
    "10101101000xxxxxPPuttttt0-------",
    "10101101010uuuuuPPittttt1iIIIIII",
    "10101101010xxxxxPPuttttt0-------",
    "10101101011uuuuuPPittttt1iIIIIII",
    "10101101011xxxxxPPuttttt0-------",
    "10101101100uuuuuPPittttt1iIIIIII",
    "10101101100xxxxxPPuttttt0-------",
    "10101101101uuuuuPPi00ttt1iIIIIII",
    "10101101101uuuuuPPi01ttt1iIIIIII",
    "10101101101uuuuuPPi10ttt1iIIIIII",
    "10101101101xxxxxPPu00ttt0-------",
    "10101101101xxxxxPPu01ttt0-------",
    "10101101101xxxxxPPu10ttt0-------",
    "10101101110uuuuuPPittttt1iIIIIII",
    "10101101110xxxxxPPuttttt0-------",
    "10101111000---iiPP0ttttt1iiii0vv",
    "10101111000---iiPP0ttttt1iiii1vv",
    "10101111000---iiPP1ttttt1iiii0vv",
    "10101111000---iiPP1ttttt1iiii1vv",
    "10101111000xxxxxPPuttttt0-------",
    "10101111010---iiPP0ttttt1iiii0vv",
    "10101111010---iiPP0ttttt1iiii1vv",
    "10101111010---iiPP1ttttt1iiii0vv",
    "10101111010---iiPP1ttttt1iiii1vv",
    "10101111010xxxxxPPuttttt0-------",
    "10101111011---iiPP0ttttt1iiii0vv",
    "10101111011---iiPP0ttttt1iiii1vv",
    "10101111011---iiPP1ttttt1iiii0vv",
    "10101111011---iiPP1ttttt1iiii1vv",
    "10101111011xxxxxPPuttttt0-------",
    "10101111100---iiPP0ttttt1iiii0vv",
    "10101111100---iiPP0ttttt1iiii1vv",
    "10101111100---iiPP1ttttt1iiii0vv",
    "10101111100---iiPP1ttttt1iiii1vv",
    "10101111100xxxxxPPuttttt0-------",
    "10101111101---iiPP000ttt1iiii0vv",
    "10101111101---iiPP000ttt1iiii1vv",
    "10101111101---iiPP001ttt1iiii0vv",
    "10101111101---iiPP001ttt1iiii1vv",
    "10101111101---iiPP010ttt1iiii0vv",
    "10101111101---iiPP010ttt1iiii1vv",
    "10101111101---iiPP100ttt1iiii0vv",
    "10101111101---iiPP100ttt1iiii1vv",
    "10101111101---iiPP101ttt1iiii0vv",
    "10101111101---iiPP101ttt1iiii1vv",
    "10101111101---iiPP110ttt1iiii0vv",
    "10101111101---iiPP110ttt1iiii1vv",
    "10101111101xxxxxPPu00ttt0-------",
    "10101111101xxxxxPPu01ttt0-------",
    "10101111101xxxxxPPu10ttt0-------",
    "10101111110---iiPP0ttttt1iiii0vv",
    "10101111110---iiPP0ttttt1iiii1vv",
    "10101111110---iiPP1ttttt1iiii0vv",
    "10101111110---iiPP1ttttt1iiii1vv",
    "10101111110xxxxxPPuttttt0-------",
    "1011iiiiiiisssssPPiiiiiiiiiddddd",
    "110000001--sssssPP-tttttiiiddddd",
    "1100000100-sssssPP-ttttt00-ddddd",
    "1100000100-sssssPP-ttttt01-ddddd",
    "1100000100-sssssPP-ttttt10-ddddd",
    "1100000100-sssssPP-ttttt11-ddddd",
    "1100000101-sssssPP-ttttt000ddddd",
    "1100000101-sssssPP-ttttt001ddddd",
    "1100000101-sssssPP-ttttt010ddddd",
    "1100000101-sssssPP-ttttt100ddddd",
    "1100000101-sssssPP-ttttt110ddddd",
    "1100000110-sssssPP-ttttt000ddddd",
    "1100000110-sssssPP-ttttt010ddddd",
    "1100000110-sssssPP-ttttt011ddddd",
    "1100000110-sssssPP-ttttt100ddddd",
    "1100000110-sssssPP-ttttt101ddddd",
    "1100000111-sssssPP-ttttt00-ddddd",
    "1100000111-sssssPP-ttttt01-ddddd",
    "1100000111-sssssPP-ttttt10-ddddd",
    "1100000111-sssssPP-ttttt11-ddddd",
    "11000010100sssssPP-ttttt-uuddddd",
    "11000010101sssssPP-ttttt-uuddddd",
    "11000010110sssssPP-ttttt-xxddddd",
    "11000010111sssssPP-ttttt-xxddddd",
    "1100001110-sssssPP-ttttt00-ddddd",
    "1100001110-sssssPP-ttttt01-ddddd",
    "1100001110-sssssPP-ttttt10-ddddd",
    "1100001110-sssssPP-ttttt11-ddddd",
    "1100001111-sssssPPittttt11iddddd",
    "1100001111-sssssPP-ttttt00-ddddd",
    "1100001111-sssssPP-ttttt01-ddddd",
    "11000100000sssssPP0tttttiiiddddd",
    "1100011001-sssssPP-ttttt00-ddddd",
    "1100011001-sssssPP-ttttt01-ddddd",
    "1100011001-sssssPP-ttttt10-ddddd",
    "1100011001-sssssPP-ttttt11-ddddd",
    "1100011010-iiiiiPP-ttttt11iddddd",
    "1100011010-sssssPP-ttttt00-ddddd",
    "1100011010-sssssPP-ttttt01-ddddd",
    "1100011010-sssssPP-ttttt10-ddddd",
    "1100011011-sssssPP-ttttt00-ddddd",
    "1100011011-sssssPP-ttttt10-ddddd",
    "1100011011-sssssPP-ttttt11-ddddd",
    "11000111010sssssPP-ttttt------dd",
    "11000111011sssssPP-ttttt------dd",
    "11000111100sssssPP-ttttt------dd",
    "11000111101sssssPP-ttttt------dd",
    "11000111110sssssPP-ttttt010---dd",
    "11000111110sssssPP-ttttt011---dd",
    "11000111110sssssPP-ttttt100---dd",
    "11000111110sssssPP-ttttt101---dd",
    "11000111110sssssPP-ttttt110---dd",
    "11000111110sssssPP-ttttt111---dd",
    "11000111111sssssPP-ttttt000---dd",
    "11000111111sssssPP-ttttt001---dd",
    "11000111111sssssPP-ttttt011---dd",
    "11000111111sssssPP-ttttt100---dd",
    "11001000---sssssPP-ttttt---xxxxx",
    "1100100100-sssssPP-ttttt00-ddddd",
    "1100100100-sssssPP-ttttt01-ddddd",
    "110010100--sssssPP0ttttt---xxxxx",
    "1100101010-sssssPP0ttttt000xxxxx",
    "11001011000sssssPP-ttttt00-xxxxx",
    "11001011000sssssPP-ttttt01-xxxxx",
    "11001011000sssssPP-ttttt10-xxxxx",
    "11001011000sssssPP-ttttt11-xxxxx",
    "11001011001sssssPP0xxxxx001uuuuu",
    "11001011001sssssPP0xxxxx101uuuuu",
    "11001011001sssssPP0xxxxx110uuuuu",
    "11001011001sssssPP1ttttt111xxxxx",
    "11001011001sssssPP1xxxxx001uuuuu",
    "11001011001sssssPP1xxxxx101uuuuu",
    "11001011001sssssPP1xxxxx110uuuuu",
    "11001011010sssssPP-ttttt00-xxxxx",
    "11001011010sssssPP-ttttt01-xxxxx",
    "11001011010sssssPP-ttttt10-xxxxx",
    "11001011010sssssPP-ttttt11-xxxxx",
    "11001011011sssssPP-ttttt00-xxxxx",
    "11001011011sssssPP-ttttt01-xxxxx",
    "11001011011sssssPP-ttttt10-xxxxx",
    "11001011011sssssPP-ttttt11-xxxxx",
    "11001011100sssssPP-ttttt00-xxxxx",
    "11001011100sssssPP-ttttt01-xxxxx",
    "11001011100sssssPP-ttttt10-xxxxx",
    "11001011100sssssPP-ttttt11-xxxxx",
    "11001011101sssssPPittttt--ixxxxx",
    "11001011110sssssPP-ttttt00-xxxxx",
    "11001011110sssssPP-ttttt01-xxxxx",
    "11001011110sssssPP-ttttt10-xxxxx",
    "11001011110sssssPP-ttttt11-xxxxx",
    "1100110000-sssssPP-ttttt00-xxxxx",
    "1100110000-sssssPP-ttttt01-xxxxx",
    "1100110000-sssssPP-ttttt10-xxxxx",
    "1100110000-sssssPP-ttttt11-xxxxx",
    "1100110001-sssssPP-ttttt00-xxxxx",
    "1100110001-sssssPP-ttttt01-xxxxx",
    "1100110001-sssssPP-ttttt10-xxxxx",
    "1100110001-sssssPP-ttttt11-xxxxx",
    "1100110010-sssssPP-ttttt00-xxxxx",
    "1100110010-sssssPP-ttttt01-xxxxx",
    "1100110010-sssssPP-ttttt10-xxxxx",
    "1100110010-sssssPP-ttttt11-xxxxx",
    "1100110011-sssssPP-ttttt00-xxxxx",
    "1100110011-sssssPP-ttttt01-xxxxx",
    "1100110011-sssssPP-ttttt10-xxxxx",
    "1100110011-sssssPP-ttttt11-xxxxx",
    "11010000---sssssPP-ttttt---ddddd",
    "11010001---sssssPP-ttttt-uuddddd",
    "110100100--sssssPP0ttttt011---dd",
    "110100100--sssssPP0ttttt100---dd",
    "110100100--sssssPP0ttttt101---dd",
    "110100100--sssssPP0ttttt110---dd",
    "110100100--sssssPP0ttttt111---dd",
    "110100100--sssssPP1ttttt000---dd",
    "110100100--sssssPP1ttttt010---dd",
    "110100100--sssssPP1ttttt011---dd",
    "110100100--sssssPP1ttttt100---dd",
    "110100100--sssssPP1ttttt101---dd",
    "11010010100sssssPP-ttttt000---dd",
    "11010010100sssssPP-ttttt010---dd",
    "11010010100sssssPP-ttttt100---dd",
    "11010010111sssssPP-ttttt000---dd",
    "11010010111sssssPP-ttttt001---dd",
    "11010010111sssssPP-ttttt010---dd",
    "11010010111sssssPP-ttttt011---dd",
    "11010011000sssssPP-ttttt000ddddd",
    "11010011000sssssPP-ttttt001ddddd",
    "11010011000sssssPP-ttttt010ddddd",
    "11010011000sssssPP-ttttt011ddddd",
    "11010011000sssssPP-ttttt100ddddd",
    "11010011000sssssPP-ttttt101ddddd",
    "11010011000sssssPP-ttttt110ddddd",
    "11010011000sssssPP-ttttt111ddddd",
    "11010011001sssssPP-ttttt000ddddd",
    "11010011001sssssPP-ttttt001ddddd",
    "11010011001sssssPP-ttttt010ddddd",
    "11010011001sssssPP-ttttt011ddddd",
    "11010011001sssssPP-ttttt100ddddd",
    "11010011001sssssPP-ttttt101ddddd",
    "11010011001sssssPP-ttttt110ddddd",
    "11010011001sssssPP-ttttt111ddddd",
    "11010011010sssssPP-ttttt000ddddd",
    "11010011010sssssPP-ttttt001ddddd",
    "11010011010sssssPP-ttttt010ddddd",
    "11010011010sssssPP-ttttt011ddddd",
    "11010011010sssssPP-ttttt100ddddd",
    "11010011010sssssPP-ttttt101ddddd",
    "11010011010sssssPP-ttttt11-ddddd",
    "11010011011sssssPP-ttttt000ddddd",
    "11010011011sssssPP-ttttt001ddddd",
    "11010011011sssssPP-ttttt010ddddd",
    "11010011011sssssPP-ttttt011ddddd",
    "11010011011sssssPP-ttttt100ddddd",
    "11010011011sssssPP-ttttt101ddddd",
    "11010011011sssssPP-ttttt110ddddd",
    "11010011011sssssPP-ttttt111ddddd",
    "11010011100sssssPP-ttttt000ddddd",
    "11010011100sssssPP-ttttt001ddddd",
    "11010011100sssssPP-ttttt010ddddd",
    "11010011100sssssPP-ttttt011ddddd",
    "11010011100sssssPP-ttttt10-ddddd",
    "11010011100sssssPP-ttttt11-ddddd",
    "11010011101sssssPP-ttttt000ddddd",
    "11010011101sssssPP-ttttt001ddddd",
    "11010011101sssssPP-ttttt010ddddd",
    "11010011101sssssPP-ttttt101ddddd",
    "11010011101sssssPP-ttttt110ddddd",
    "11010011101sssssPP-ttttt111ddddd",
    "11010011110sssssPP-ttttt000ddddd",
    "11010011110sssssPP-ttttt001ddddd",
    "11010011110sssssPP-ttttt010ddddd",
    "11010011110sssssPP-ttttt011ddddd",
    "11010011110sssssPP-ttttt100ddddd",
    "11010011110sssssPP-ttttt101ddddd",
    "11010011110sssssPP-ttttt110ddddd",
    "11010011110sssssPP-ttttt111ddddd",
    "11010011111sssssPP-ttttt000ddddd",
    "11010011111sssssPP-ttttt001ddddd",
    "11010011111sssssPP-ttttt010ddddd",
    "11010011111sssssPP-ttttt011ddddd",
    "11010011111sssssPP-ttttt100ddddd",
    "11010011111sssssPP-ttttt111ddddd",
    "11010100--1sssssPP-ttttt---ddddd",
    "11010101000sssssPP-ttttt00-ddddd",
    "11010101000sssssPP-ttttt01-ddddd",
    "11010101000sssssPP-ttttt10-ddddd",
    "11010101000sssssPP-ttttt11-ddddd",
    "11010101001sssssPP-ttttt00-ddddd",
    "11010101001sssssPP-ttttt01-ddddd",
    "11010101001sssssPP-ttttt10-ddddd",
    "11010101001sssssPP-ttttt11-ddddd",
    "11010101010sssssPP-ttttt000ddddd",
    "11010101010sssssPP-ttttt001ddddd",
    "11010101010sssssPP-ttttt010ddddd",
    "11010101010sssssPP-ttttt011ddddd",
    "11010101010sssssPP-ttttt100ddddd",
    "11010101010sssssPP-ttttt101ddddd",
    "11010101010sssssPP-ttttt110ddddd",
    "11010101010sssssPP-ttttt111ddddd",
    "11010101011sssssPP-ttttt000ddddd",
    "11010101011sssssPP-ttttt001ddddd",
    "11010101011sssssPP-ttttt010ddddd",
    "11010101011sssssPP-ttttt011ddddd",
    "11010101011sssssPP-ttttt100ddddd",
    "11010101011sssssPP-ttttt101ddddd",
    "11010101011sssssPP-ttttt110ddddd",
    "11010101011sssssPP-ttttt111ddddd",
    "11010101100sssssPP-ttttt0--ddddd",
    "11010101100sssssPP-ttttt1--ddddd",
    "11010101101sssssPP-ttttt0--ddddd",
    "11010101101sssssPP-ttttt1--ddddd",
    "11010101110sssssPP-ttttt0--ddddd",
    "11010101110sssssPP-ttttt1--ddddd",
    "11010101111sssssPP-ttttt---ddddd",
    "1101011000i-----PPiiiiiiiiiddddd",
    "1101011001i-----PPiiiiiiiiiddddd",
    "110101110iisssssPPitttttiiiddddd",
    "11011000IiisssssPPidddddiiiIIIII",
    "1101100100i-----PPiiiiiiiiiddddd",
    "1101100101i-----PPiiiiiiiiiddddd",
    "1101101000isssssPPiiiiiiiiixxxxx",
    "1101101001ixxxxxPPiiiiiiiiiuuuuu",
    "1101101010isssssPPiiiiiiiiixxxxx",
    "110110110iisssssPPidddddiiiuuuuu",
    "110110111iisssssPPidddddiiiuuuuu",
    "11011100000sssssPP-iiiiiiii00-dd",
    "11011100000sssssPP-iiiiiiii01-dd",
    "11011100001sssssPP-iiiiiiii00-dd",
    "11011100001sssssPP-iiiiiiii01-dd",
    "11011100010sssssPP-0iiiiiii00-dd",
    "11011100010sssssPP-0iiiiiii01-dd",
    "11011100100sssssPP-000iiiii10-dd",
    "11011101-00sssssPP-iiiiiiii00-dd",
    "11011101-00sssssPP-iiiiiiii01-dd",
    "11011101-01sssssPP-iiiiiiii00-dd",
    "11011101-01sssssPP-iiiiiiii01-dd",
    "11011101-10sssssPP-0iiiiiii00-dd",
    "11011101-10sssssPP-0iiiiiii01-dd",
    "11011110iiixxxxxPPiIIIIIiii0i00-",
    "11011110iiixxxxxPPiIIIIIiii0i01-",
    "11011110iiixxxxxPPiIIIIIiii0i10-",
    "11011110iiixxxxxPPiIIIIIiii0i11-",
    "11011110iiixxxxxPPiIIIIIiii1i00-",
    "11011110iiixxxxxPPiIIIIIiii1i01-",
    "11011110iiixxxxxPPiIIIIIiii1i10-",
    "11011110iiixxxxxPPiIIIIIiii1i11-",
    "110111110iisssssPPidddddiiiuuuuu",
    "110111111iisssssPPidddddiiiuuuuu",
    "111000000--sssssPP0iiiiiiiiddddd",
    "111000001--sssssPP0iiiiiiiiddddd",
    "111000010--sssssPP0iiiiiiiixxxxx",
    "111000011--sssssPP0iiiiiiiixxxxx",
    "111000100--sssssPP0iiiiiiiixxxxx",
    "111000101--sssssPP0iiiiiiiixxxxx",
    "11100011000sssssPP-yyyyy---uuuuu",
    "11100100N00sssssPP-ttttt-00ddddd",
    "11100100N00sssssPP-ttttt-01ddddd",
    "11100100N00sssssPP-ttttt-10ddddd",
    "11100100N00sssssPP-ttttt-11ddddd",
    "11100100N01sssssPP-ttttt-00ddddd",
    "11100100N01sssssPP-ttttt-01ddddd",
    "11100100N01sssssPP-ttttt-10ddddd",
    "11100100N01sssssPP-ttttt-11ddddd",
    "11100100N10sssssPP-ttttt-00ddddd",
    "11100100N10sssssPP-ttttt-01ddddd",
    "11100100N10sssssPP-ttttt-10ddddd",
    "11100100N10sssssPP-ttttt-11ddddd",
    "11100101000sssssPP0ttttt000ddddd",
    "11100101000sssssPP0ttttt001ddddd",
    "11100101000sssssPP0ttttt010ddddd",
    "11100101010sssssPP0ttttt000ddddd",
    "11100101010sssssPP0ttttt001ddddd",
    "11100101100sssssPP0ttttt001ddddd",
    "11100101N00sssssPP0ttttt101ddddd",
    "11100101N00sssssPP0ttttt110ddddd",
    "11100101N00sssssPP0ttttt111ddddd",
    "11100101N10sssssPP0ttttt110ddddd",
    "11100110N00sssssPP-ttttt000xxxxx",
    "11100110N00sssssPP-ttttt001xxxxx",
    "11100110N00sssssPP-ttttt010xxxxx",
    "11100110N00sssssPP-ttttt011xxxxx",
    "11100110N01sssssPP-ttttt000xxxxx",
    "11100110N01sssssPP-ttttt001xxxxx",
    "11100110N01sssssPP-ttttt010xxxxx",
    "11100110N01sssssPP-ttttt011xxxxx",
    "11100110N10sssssPP-ttttt000xxxxx",
    "11100110N10sssssPP-ttttt001xxxxx",
    "11100110N10sssssPP-ttttt010xxxxx",
    "11100110N10sssssPP-ttttt011xxxxx",
    "11100110N11sssssPP-ttttt000xxxxx",
    "11100110N11sssssPP-ttttt001xxxxx",
    "11100110N11sssssPP-ttttt010xxxxx",
    "11100110N11sssssPP-ttttt011xxxxx",
    "11100111000sssssPP0ttttt000xxxxx",
    "11100111000sssssPP0ttttt001xxxxx",
    "11100111000sssssPP0ttttt010xxxxx",
    "11100111001sssssPP0ttttt000xxxxx",
    "11100111001sssssPP0ttttt001xxxxx",
    "11100111010sssssPP0ttttt000xxxxx",
    "11100111011sssssPP0ttttt000xxxxx",
    "11100111100sssssPP0ttttt001xxxxx",
    "11100111110sssssPP0ttttt001xxxxx",
    "11100111N00sssssPP0ttttt101xxxxx",
    "11100111N00sssssPP0ttttt110xxxxx",
    "11100111N00sssssPP0ttttt111xxxxx",
    "11100111N10sssssPP0ttttt110xxxxx",
    "11100111N10sssssPP0ttttt111xxxxx",
    "11100111N11sssssPP0ttttt101xxxxx",
    "11101000000sssssPP0ttttt000ddddd",
    "11101000000sssssPP0ttttt001ddddd",
    "11101000000sssssPP0ttttt010ddddd",
    "11101000001sssssPP0ttttt000ddddd",
    "11101000010sssssPP0ttttt000ddddd",
    "11101000010sssssPP0ttttt001ddddd",
    "11101000010sssssPP0ttttt010ddddd",
    "11101000011sssssPP0ttttt000ddddd",
    "11101000011sssssPP0ttttt001ddddd",
    "11101000100sssssPP0ttttt001ddddd",
    "11101000101sssssPP0ttttt000ddddd",
    "11101000101sssssPP0ttttt001ddddd",
    "11101000101sssssPP0ttttt100ddddd",
    "11101000110sssssPP0ttttt001ddddd",
    "11101000111sssssPP0ttttt000ddddd",
    "11101000111sssssPP0ttttt100ddddd",
    "11101000N00sssssPP0ttttt100ddddd",
    "11101000N00sssssPP0ttttt101ddddd",
    "11101000N00sssssPP0ttttt111ddddd",
    "11101000N01sssssPP0ttttt010ddddd",
    "11101000N01sssssPP0ttttt101ddddd",
    "11101000N01sssssPP0ttttt110ddddd",
    "11101000N01sssssPP0ttttt111ddddd",
    "11101000N10sssssPP0ttttt100ddddd",
    "11101000N10sssssPP0ttttt101ddddd",
    "11101000N10sssssPP0ttttt110ddddd",
    "11101000N10sssssPP0ttttt111ddddd",
    "11101000N11sssssPP0ttttt101ddddd",
    "11101000N11sssssPP0ttttt111ddddd",
    "111010010-1sssssPP0ttttt111ddddd",
    "111010010--sssssPP0ttttt-01ddddd",
    "111010011-1sssssPP0ttttt110ddddd",
    "111010011-1sssssPP0ttttt111ddddd",
    "11101010000sssssPP0ttttt000xxxxx",
    "11101010000sssssPP0ttttt001xxxxx",
    "11101010000sssssPP0ttttt010xxxxx",
    "11101010001sssssPP0ttttt001xxxxx",
    "11101010001sssssPP0ttttt100xxxxx",
    "11101010010sssssPP0ttttt000xxxxx",
    "11101010010sssssPP0ttttt001xxxxx",
    "11101010010sssssPP0ttttt010xxxxx",
    "11101010010sssssPP0ttttt100xxxxx",
    "11101010011sssssPP0ttttt001xxxxx",
    "11101010100sssssPP0ttttt001xxxxx",
    "11101010101sssssPP0ttttt0eexxxxx",
    "11101010101sssssPP0ttttt100xxxxx",
    "11101010110sssssPP0ttttt001xxxxx",
    "11101010111sssssPP0ttttt0eeddddd",
    "11101010111sssssPP0ttttt100xxxxx",
    "11101010N00sssssPP0ttttt100xxxxx",
    "11101010N00sssssPP0ttttt101xxxxx",
    "11101010N00sssssPP0ttttt111xxxxx",
    "11101010N01sssssPP0ttttt101xxxxx",
    "11101010N01sssssPP0ttttt110xxxxx",
    "11101010N01sssssPP0ttttt111xxxxx",
    "11101010N10sssssPP0ttttt101xxxxx",
    "11101010N10sssssPP0ttttt111xxxxx",
    "11101010N11sssssPP0ttttt101xxxxx",
    "11101010N11sssssPP0ttttt110xxxxx",
    "11101010N11sssssPP0ttttt111xxxxx",
    "11101011000sssssPP0ttttt000ddddd",
    "11101011010sssssPP0ttttt000ddddd",
    "11101011100sssssPP0ttttt000ddddd",
    "11101011100sssssPP0ttttt001ddddd",
    "11101011110sssssPP0ttttt000ddddd",
    "11101011110sssssPP0ttttt001ddddd",
    "11101011111sssssPP0ttttt1eeddddd",
    "11101100N00sssssPP-ttttt000ddddd",
    "11101100N00sssssPP-ttttt001ddddd",
    "11101100N00sssssPP-ttttt010ddddd",
    "11101100N00sssssPP-ttttt011ddddd",
    "11101100N00sssssPP-ttttt100ddddd",
    "11101100N00sssssPP-ttttt101ddddd",
    "11101100N00sssssPP-ttttt110ddddd",
    "11101100N00sssssPP-ttttt111ddddd",
    "11101100N01sssssPP-ttttt000ddddd",
    "11101100N01sssssPP-ttttt001ddddd",
    "11101100N01sssssPP-ttttt010ddddd",
    "11101100N01sssssPP-ttttt011ddddd",
    "11101100N01sssssPP-ttttt100ddddd",
    "11101100N01sssssPP-ttttt101ddddd",
    "11101100N01sssssPP-ttttt110ddddd",
    "11101100N01sssssPP-ttttt111ddddd",
    "11101100N10sssssPP-ttttt000ddddd",
    "11101100N10sssssPP-ttttt001ddddd",
    "11101100N10sssssPP-ttttt010ddddd",
    "11101100N10sssssPP-ttttt011ddddd",
    "11101101000sssssPP0ttttt000ddddd",
    "11101101001sssssPP0ttttt001ddddd",
    "11101101010sssssPP0ttttt001ddddd",
    "11101101011sssssPP0ttttt001ddddd",
    "11101101101sssssPP0ttttt000ddddd",
    "11101101101sssssPP0ttttt001ddddd",
    "11101101101sssssPP0ttttt100ddddd",
    "11101101111sssssPP0ttttt000ddddd",
    "11101101111sssssPP0ttttt100ddddd",
    "11101101N01sssssPP0ttttt110ddddd",
    "11101101N01sssssPP0ttttt111ddddd",
    "11101101N0NsssssPP0ttttt0NNddddd",
    "11101101N11sssssPP0ttttt110ddddd",
    "11101110N00sssssPP-ttttt000xxxxx",
    "11101110N00sssssPP-ttttt001xxxxx",
    "11101110N00sssssPP-ttttt010xxxxx",
    "11101110N00sssssPP-ttttt011xxxxx",
    "11101110N00sssssPP-ttttt100xxxxx",
    "11101110N00sssssPP-ttttt101xxxxx",
    "11101110N00sssssPP-ttttt110xxxxx",
    "11101110N00sssssPP-ttttt111xxxxx",
    "11101110N01sssssPP-ttttt000xxxxx",
    "11101110N01sssssPP-ttttt001xxxxx",
    "11101110N01sssssPP-ttttt010xxxxx",
    "11101110N01sssssPP-ttttt011xxxxx",
    "11101110N01sssssPP-ttttt100xxxxx",
    "11101110N01sssssPP-ttttt101xxxxx",
    "11101110N01sssssPP-ttttt110xxxxx",
    "11101110N01sssssPP-ttttt111xxxxx",
    "11101110N10sssssPP-ttttt000xxxxx",
    "11101110N10sssssPP-ttttt001xxxxx",
    "11101110N10sssssPP-ttttt010xxxxx",
    "11101110N10sssssPP-ttttt011xxxxx",
    "11101110N11sssssPP-ttttt000xxxxx",
    "11101110N11sssssPP-ttttt001xxxxx",
    "11101110N11sssssPP-ttttt010xxxxx",
    "11101110N11sssssPP-ttttt011xxxxx",
    "11101111000sssssPP0ttttt000xxxxx",
    "11101111000sssssPP0ttttt001xxxxx",
    "11101111000sssssPP0ttttt011xxxxx",
    "11101111000sssssPP0ttttt100xxxxx",
    "11101111000sssssPP0ttttt101xxxxx",
    "11101111000sssssPP0ttttt110xxxxx",
    "11101111000sssssPP0ttttt111xxxxx",
    "11101111001sssssPP0ttttt000xxxxx",
    "11101111001sssssPP0ttttt001xxxxx",
    "11101111001sssssPP0ttttt010xxxxx",
    "11101111010sssssPP0ttttt000xxxxx",
    "11101111010sssssPP0ttttt001xxxxx",
    "11101111010sssssPP0ttttt010xxxxx",
    "11101111010sssssPP0ttttt011xxxxx",
    "11101111011sssssPP0ttttt000xxxxx",
    "11101111011sssssPP0ttttt001xxxxx",
    "11101111011sssssPP0ttttt1uuxxxxx",
    "11101111100sssssPP0ttttt001xxxxx",
    "11101111100sssssPP0ttttt011xxxxx",
    "11101111110sssssPP0ttttt000xxxxx",
    "11101111110sssssPP0ttttt001xxxxx",
    "11101111110sssssPP0ttttt010xxxxx",
    "11101111110sssssPP0ttttt011xxxxx",
    "11110001000sssssPP-ttttt---ddddd",
    "11110001001sssssPP-ttttt---ddddd",
    "11110001011sssssPP-ttttt---ddddd",
    "11110001100sssssPP-ttttt---ddddd",
    "11110001101sssssPP-ttttt---ddddd",
    "11110010-00sssssPP-ttttt---000dd",
    "11110010-00sssssPP-ttttt---100dd",
    "11110010-10sssssPP-ttttt---000dd",
    "11110010-10sssssPP-ttttt---100dd",
    "11110010-11sssssPP-ttttt---000dd",
    "11110010-11sssssPP-ttttt---100dd",
    "11110011000sssssPP-ttttt---ddddd",
    "11110011001sssssPP-ttttt---ddddd",
    "11110011010sssssPP-ttttt---ddddd",
    "11110011011sssssPP-ttttt---ddddd",
    "11110011100sssssPP-ttttt---ddddd",
    "11110011101sssssPP-ttttt---ddddd",
    "11110011110sssssPP-ttttt---ddddd",
    "11110011111sssssPP-ttttt---ddddd",
    "11110100---sssssPP-ttttt-uuddddd",
    "111101010--sssssPP-ttttt---ddddd",
    "111101011--sssssPP-ttttt---ddddd",
    "11110110000sssssPP-ttttt---ddddd",
    "11110110001sssssPP-ttttt---ddddd",
    "11110110010sssssPP-ttttt---ddddd",
    "11110110011sssssPP-ttttt---ddddd",
    "11110110100sssssPP-ttttt---ddddd",
    "11110110101sssssPP-ttttt---ddddd",
    "11110110110sssssPP-ttttt---ddddd",
    "11110110111sssssPP-ttttt---ddddd",
    "11110111-00sssssPP-ttttt---ddddd",
    "11110111-01sssssPP-ttttt---ddddd",
    "11110111-11sssssPP-ttttt---ddddd",
    "11111001-00sssssPP0ttttt0uuddddd",
    "11111001-00sssssPP0ttttt1uuddddd",
    "11111001-00sssssPP1ttttt0uuddddd",
    "11111001-00sssssPP1ttttt1uuddddd",
    "11111001-01sssssPP0ttttt0uuddddd",
    "11111001-01sssssPP0ttttt1uuddddd",
    "11111001-01sssssPP1ttttt0uuddddd",
    "11111001-01sssssPP1ttttt1uuddddd",
    "11111001-11sssssPP0ttttt0uuddddd",
    "11111001-11sssssPP0ttttt1uuddddd",
    "11111001-11sssssPP1ttttt0uuddddd",
    "11111001-11sssssPP1ttttt1uuddddd",
    "111110110-0sssssPP0ttttt0uuddddd",
    "111110110-0sssssPP0ttttt1uuddddd",
    "111110110-0sssssPP1ttttt0uuddddd",
    "111110110-0sssssPP1ttttt1uuddddd",
    "111110110-1sssssPP0ttttt0uuddddd",
    "111110110-1sssssPP0ttttt1uuddddd",
    "111110110-1sssssPP1ttttt0uuddddd",
    "111110110-1sssssPP1ttttt1uuddddd",
    "11111101---sssssPP0ttttt0uuddddd",
    "11111101---sssssPP0ttttt1uuddddd",
    "11111101---sssssPP1ttttt0uuddddd",
    "11111101---sssssPP1ttttt1uuddddd",
    NULL,
};

}  // namespace Elf
}  // namespace Internal
}  // namespace Halide
