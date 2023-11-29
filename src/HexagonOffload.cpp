#include <iostream>
#include <memory>

#include "Closure.h"
#include "Elf.h"
#include "HexagonOffload.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "InjectHostDevBufferCopies.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LowerParallelTasks.h"
#include "Module.h"
#include "Param.h"
#include "Substitute.h"

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
    EF_HEXAGON_MACH_V60 = 0x60,  // Deprecated
    EF_HEXAGON_MACH_V61 = 0x61,  // Deprecated?
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
    switch (type) {
    case Section::SHT_NULL:
        return "SHT_NULL";
    case Section::SHT_PROGBITS:
        return "SHT_PROGBITS";
    case Section::SHT_SYMTAB:
        return "SHT_SYMTAB";
    case Section::SHT_STRTAB:
        return "SHT_STRTAB";
    case Section::SHT_RELA:
        return "SHT_RELA";
    case Section::SHT_HASH:
        return "SHT_HASH";
    case Section::SHT_DYNAMIC:
        return "SHT_DYNAMIC";
    case Section::SHT_NOTE:
        return "SHT_NOTE";
    case Section::SHT_NOBITS:
        return "SHT_NOBITS";
    case Section::SHT_REL:
        return "SHT_REL";
    case Section::SHT_SHLIB:
        return "SHT_SHLIB";
    case Section::SHT_DYNSYM:
        return "SHT_DYNSYM";
    case Section::SHT_LOPROC:
        return "SHT_LOPROC";
    case Section::SHT_HIPROC:
        return "SHT_HIPROC";
    case Section::SHT_LOUSER:
        return "SHT_LOUSER";
    case Section::SHT_HIUSER:
        return "SHT_HIUSER";
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
    for (const Section &s : obj.sections()) {
        oss << s.get_name() << ", Type = " << section_type_string(s.get_type()) << ", Size = " << hex(s.get_size()) << ", Alignment = " << s.get_alignment() << "\n";
    }
    return oss.str();
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
        for (int i = 31; i >= 0; i--) {
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
            // uint32_t lo = inst & ((1 << 13) - 1);

            // We only understand the ones where hi starts with 010
            internal_assert((hi >> 10) == 2);

            // Low 6 bits of val go in the following bits.
            mask = 63 << 20;

        } else if ((inst >> 24) == 72) {
            // Example instruction encoding that has this high byte (ignoring bits 1 and 2):
            // 0100 1ii0  000i iiii  PPit tttt  iiii iiii
            debug(4) << "Instruction-specific case A\n";
            mask = 0x061f20ff;
        } else if ((inst >> 24) == 73) {
            // 0100 1ii1  000i iiii  PPii iiii  iiid dddd
            debug(4) << "Instruction-specific case B\n";
            mask = 0x061f3fe0;
        } else if ((inst >> 24) == 120) {
            // 0111 1000  ii-i iiii  PPii iiii  iiid dddd
            debug(4) << "Instruction-specific case C\n";
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

        } else if ((inst >> 24) == 126) {
            // 0111 1110 0uu0 iiii PP0i iiii iiid dddd
            // 0111 1110 0uu0 iiii PP1i iiii iiid dddd
            // 0111 1110 0uu1 iiii PP0i iiii iiid dddd
            // 0111 1110 0uu1 iiii PP1i iiii iiid dddd
            mask = 0x000f1fe0;
        } else if ((inst >> 24) == 65 || (inst >> 24) == 77) {
            // 0100 0001 000s ssss PP0t tiii iiid dddd
            // 0100 0001 001s ssss PP0t tiii iiid dddd
            // 0100 0001 010s ssss PP0t tiii iiid dddd
            // 0100 0001 011s ssss PP0t tiii iiid dddd
            // 0100 0001 100s ssss PP0t tiii iiid dddd
            // 0100 0001 110s ssss PP0t tiii iiid dddd
            // TODO: Add instructions to comment for mask 77.
            mask = 0x000007e0;
        } else if ((inst >> 21) == 540) {
            // 0100 0011 100s ssss PP0t tiii iiid dddd
            mask = 0x000007e0;
        } else if ((inst >> 28) == 11) {
            // 1011 iiii iiis ssss PPii iiii iiid dddd
            mask = 0x0fe03fe0;
        } else {
            internal_error << "Unhandled instruction type! Instruction = " << inst << "\n";
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
    const uint32_t Word32 = 0xffffffff;
    const uint32_t Word16 = 0xffff;
    const uint32_t Word8 = 0xff;
    const uint32_t Word32_B22 = 0x01ff3ffe;
    const uint32_t Word32_B15 = 0x00df20fe;
    const uint32_t Word32_B13 = 0x00202ffe;
    const uint32_t Word32_B9 = 0x003000fe;
    const uint32_t Word32_B7 = 0x00001f18;
    const uint32_t Word32_GP = 0;  // The mask is instruction-specific
    const uint32_t Word32_X26 = 0x0fff3fff;
    const uint32_t Word32_U6 = 0;  // The mask is instruction-specific
    const uint32_t Word32_R6 = 0x000007e0;
    const uint32_t Word32_LO = 0x00c03fff;
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
        do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A) >> 16, _unsigned, truncate);
        do_reloc(fixup_addr + 4, Word32_LO, uintptr_t(S + A), _unsigned, truncate);
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
        } else {
            flags = Elf::EF_HEXAGON_MACH_V62;
        }
    }

    uint16_t get_machine() override {
        return EM_HEXAGON;
    }
    uint32_t get_flags() override {
        return flags;
    }
    uint32_t get_version() override {
        return EV_CURRENT;
    }
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
            0x00, 0x40, 0x00, 0x00,  // { immext (#0) (Relocation:R_HEX_B32_PCREL_X)
            0x0e, 0xc0, 0x49, 0x6a,  //   r14 = add (pc, ##GOTn@PCREL) }  (Relocation:R_HEX_6_PCREL_X)
            0x1c, 0xc0, 0x8e, 0x91,  //   r28 = memw (r14)
            0x00, 0xc0, 0x9c, 0x52,  //   jumpr r28
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

    Expr visit(const Load *op) override {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            return Load::make(op->type, op->name, mutate(op->index), op->image,
                              i->second, mutate(op->predicate), op->alignment);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            return Store::make(op->name, mutate(op->value), mutate(op->index),
                               i->second, mutate(op->predicate), op->alignment);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    ReplaceParams(const std::map<std::string, Parameter> &replacements)
        : replacements(replacements) {
    }
};

Stmt replace_params(const Stmt &s, const std::map<std::string, Parameter> &replacements) {
    return ReplaceParams(replacements).mutate(s);
}

class InjectHexagonRpc : public IRMutator {
    std::map<std::string, Expr> state_bufs;

    Module &device_code;

    Expr state_var_ptr(const std::string &name, Type type) {
        Expr &buf = state_bufs[name];
        if (!buf.defined()) {
            auto storage = Buffer<void *>::make_scalar(name + "_buf");
            storage() = nullptr;
            buf = Variable::make(type_of<halide_buffer_t *>(), storage.name() + ".buffer", storage);
        }
        return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
    }

    Expr module_state() {
        return Call::make(type_of<void *>(), "halide_hexagon_get_module_state", {state_var_ptr("hexagon_module_state", type_of<void *>())}, Call::Extern);
    }

    Expr module_state_ptr() {
        return state_var_ptr("hexagon_module_state", type_of<void *>());
    }

    // Create a Buffer containing the given buffer/size, and return an
    // expression for a pointer to the first element.
    Expr buffer_ptr(const uint8_t *buffer, size_t size, const char *name) {
        Buffer<uint8_t> code((int)size, name);
        memcpy(code.data(), buffer, (int)size);
        Expr buf = Variable::make(type_of<halide_buffer_t *>(), string(name) + ".buffer", code);
        return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
    }

    using IRMutator::visit;

    Stmt visit(const For *loop) override {
        if (loop->device_api != DeviceAPI::Hexagon) {
            return IRMutator::visit(loop);
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
        Stmt body;
        if (is_const_one(loop->extent)) {
            body = LetStmt::make(loop->name, loop->min, loop->body);
        } else {
            body = For::make(loop->name, loop->min, loop->extent, loop->for_type, loop->partition_policy,
                             DeviceAPI::None, loop->body);
        }

        // Build a closure for the device code.
        // Note that we must do this *before* calling lower_parallel_tasks();
        // otherwise the Closure may fail to find buffers that are referenced
        // only in the closure.
        // TODO: Should this move the body of the loop to Hexagon,
        // or the loop itself? Currently, this moves the loop itself.
        Closure c;
        c.include(body);

        std::vector<LoweredFunc> closure_implementations;
        body = lower_parallel_tasks(body, closure_implementations, hex_name, device_code.target());
        for (auto &lowered_func : closure_implementations) {
            device_code.append(lowered_func);
        }

        // A buffer parameter potentially generates 3 scalar parameters (min,
        // extent, stride) per dimension. Pipelines with many buffers may
        // generate extreme numbers of scalar parameters, which can cause
        // problems for LLVM. This logic moves scalar parameters of the type
        // matching the type of these scalars to a single buffer.
        // TODO(dsharlet): Maybe this is Int(64) in some cases?
        Type scalars_buffer_type = Int(32);
        std::string scalars_buffer_name = "scalar_indices";
        std::vector<Stmt> scalars_buffer_init;
        for (auto i = c.vars.begin(); i != c.vars.end();) {
            if (i->second == scalars_buffer_type) {
                int index = scalars_buffer_init.size();
                scalars_buffer_init.push_back(Store::make(scalars_buffer_name, Variable::make(scalars_buffer_type, i->first),
                                                          index, Parameter(), const_true(), ModulusRemainder()));
                Expr replacement = Load::make(scalars_buffer_type, scalars_buffer_name, index, Buffer<>(),
                                              Parameter(), const_true(), ModulusRemainder());
                body = LetStmt::make(i->first, replacement, body);

                i = c.vars.erase(i);
            } else {
                ++i;
            }
        }
        if (!scalars_buffer_init.empty()) {
            // If we put some scalars in the scalars buffer, add it to the closure.
            Closure::Buffer scalars_buffer;
            scalars_buffer.type = scalars_buffer_type;
            scalars_buffer.dimensions = 1;
            scalars_buffer.read = true;
            scalars_buffer.write = false;
            c.buffers[scalars_buffer_name] = scalars_buffer;
        }
        int scalars_buffer_extent = scalars_buffer_init.size();

        // Make an argument list, and generate a function in the
        // device_code module. The hexagon runtime code expects
        // the arguments to appear in the order of (input buffers,
        // output buffers, input scalars).  Scalars must be last
        // for the scalar arguments to shadow the symbols of the
        // buffer that get generated by CodeGen_LLVM.
        std::vector<LoweredArgument> input_buffers, output_buffers;
        std::map<std::string, Parameter> replacement_params;
        for (const auto &i : c.buffers) {
            if (i.second.write) {
                Argument::Kind kind = Argument::OutputBuffer;
                output_buffers.emplace_back(i.first, kind, i.second.type, i.second.dimensions, ArgumentEstimates{});
            } else {
                Argument::Kind kind = Argument::InputBuffer;
                input_buffers.emplace_back(i.first, kind, i.second.type, i.second.dimensions, ArgumentEstimates{});
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

            // Add an assert to the body that validates the alignment of the
            // buffer. These buffers are either allocated by FastRPC or
            // halide_hexagon_device_interface buffers, either should be aligned
            // to 128 bytes.
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
        for (const auto &i : c.vars) {
            LoweredArgument arg(i.first, Argument::InputScalar, i.second, 0, ArgumentEstimates{});
            args.push_back(arg);
        }
        // We need the _argv function but not the _metadata.
        device_code.append(LoweredFunc(hex_name, args, body, LinkageType::ExternalPlusArgv));

        // Generate a call to hexagon_device_run.
        std::vector<Expr> arg_sizes;
        std::vector<Expr> arg_ptrs;
        std::vector<Expr> arg_flags;

        for (const auto &i : c.buffers) {
            // Buffers are passed to the hexagon host runtime as just device
            // handles (uint64) and host (uint8*) fields. They correspond
            // to the 'hexagon_device_pointer' struct declared elsewhere;
            // we don't use that struct here because it's simple enough that
            // just using `make_struct`() for it is simpler.
            if (i.first != scalars_buffer_name) {
                // If this isn't the scalars buffer, assume it has a '.buffer'
                // description in the IR.
                Expr buf = Variable::make(type_of<halide_buffer_t *>(), i.first + ".buffer");
                Expr device = Call::make(UInt(64), Call::buffer_get_device, {buf}, Call::Extern);
                Expr host = Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
                Expr pseudo_buffer = Call::make(Handle(), Call::make_struct, {device, host}, Call::Intrinsic);
                arg_ptrs.push_back(pseudo_buffer);
                arg_sizes.emplace_back((uint64_t)(pseudo_buffer.type().bytes()));
            } else {
                // If this is the scalars buffer, it doesn't have a .buffer
                // field. Rather than make one, It's easier to just skip the
                // buffer_get_host call and reference the allocation directly.
                // TODO: This is a bit of an ugly hack, it would be nice to find
                // a better way to identify buffers without a '.buffer' description.
                Expr host = Variable::make(Handle(), i.first);
                Expr pseudo_buffer = Call::make(Handle(), Call::make_struct, {make_zero(UInt(64)), host}, Call::Intrinsic);
                arg_ptrs.push_back(pseudo_buffer);
                arg_sizes.emplace_back((uint64_t)scalars_buffer_extent * scalars_buffer_type.bytes());
            }

            // In the flags parameter, bit 0 set indicates the
            // buffer is read, bit 1 set indicates the buffer is
            // written. If neither are set, the argument is a scalar.
            int flags = 0;
            if (i.second.read) {
                flags |= 0x1;
            }
            if (i.second.write) {
                flags |= 0x2;
            }
            arg_flags.emplace_back(flags);
        }
        for (const auto &i : c.vars) {
            Expr arg = Variable::make(i.second, i.first);
            Expr arg_ptr = Call::make(type_of<void *>(), Call::make_struct, {arg}, Call::Intrinsic);
            arg_sizes.emplace_back((uint64_t)i.second.bytes());
            arg_ptrs.push_back(arg_ptr);
            arg_flags.emplace_back(0x0);
        }

        // The argument list is terminated with an argument of size 0.
        arg_sizes.emplace_back((uint64_t)0);

        std::string pipeline_name = hex_name + "_argv";
        std::vector<Expr> params;
        params.push_back(module_state());
        params.emplace_back(pipeline_name);
        params.push_back(state_var_ptr(hex_name, type_of<int>()));
        params.push_back(Call::make(type_of<uint64_t *>(), Call::make_struct, arg_sizes, Call::Intrinsic));
        params.push_back(Call::make(type_of<void **>(), Call::make_struct, arg_ptrs, Call::Intrinsic));
        params.push_back(Call::make(type_of<int *>(), Call::make_struct, arg_flags, Call::Intrinsic));

        Stmt offload_call = call_extern_and_assert("halide_hexagon_run", params);
        if (!scalars_buffer_init.empty()) {
            offload_call = Block::make(Block::make(scalars_buffer_init), offload_call);
        }
        offload_call = Allocate::make(scalars_buffer_name, scalars_buffer_type, MemoryType::Auto,
                                      {Expr(scalars_buffer_extent)}, const_true(), offload_call);
        return offload_call;
    }

public:
    InjectHexagonRpc(Module &device_code)
        : device_code(device_code) {
    }

    Stmt inject(Stmt s) {
        s = mutate(s);

        if (!device_code.functions().empty()) {
            // Wrap the statement in calls to halide_initialize_kernels.
            Expr runtime_buf_var = Variable::make(type_of<struct halide_buffer_t *>(), runtime_module_name + ".buffer");
            Expr runtime_size = Call::make(Int(32), Call::buffer_get_extent, {runtime_buf_var, 0}, Call::Extern);
            Expr runtime_ptr = Call::make(Handle(), Call::buffer_get_host, {runtime_buf_var}, Call::Extern);

            Expr code_buf_var = Variable::make(type_of<struct halide_buffer_t *>(), pipeline_module_name + ".buffer");
            Expr code_size = Call::make(Int(32), Call::buffer_get_extent, {code_buf_var, 0}, Call::Extern);
            Expr code_ptr = Call::make(Handle(), Call::buffer_get_host, {code_buf_var}, Call::Extern);
            Stmt init_kernels = call_extern_and_assert("halide_hexagon_initialize_kernels",
                                                       {module_state_ptr(), code_ptr, cast<uint64_t>(code_size), runtime_ptr, cast<uint64_t>(runtime_size)});
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

    // Write intermediate bitcode to disk if requested.
    // TODO: We really need something better than this. This won't
    // work in non-trivial JIT or AOT programs.
    std::string bitcode_dump_path = get_env_variable("HL_HEXAGON_DUMP_BITCODE");
    if (!bitcode_dump_path.empty()) {
        auto fd_ostream = make_raw_fd_ostream(bitcode_dump_path);
        compile_llvm_module_to_llvm_bitcode(*llvm_module, *fd_ostream);
        debug(0) << "Wrote Hexagon device bitcode to " << bitcode_dump_path;
    }

    llvm::SmallVector<char, 4096> object;
    llvm::raw_svector_ostream object_stream(object);
    compile_llvm_module_to_object(*llvm_module, object_stream);

    int min_debug_level = device_code.name() == runtime_module_name ? 3 : 2;
    if (debug::debug_level() >= min_debug_level) {
        debug(0) << "Hexagon device code assembly: "
                 << "\n";
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
        dtors->append_contents((uint32_t)0);
    }

    // We call the constructors in ctors backwards starting from special
    // symbol __CTOR_END__ until we reach a 0 (NULL pointer value). So,
    // prepend the .ctors section with 0.
    auto ctors = obj->find_section(".ctors");
    if (ctors != obj->sections_end()) {
        ctors->prepend_contents((uint32_t)0);
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

        write_entire_file(input.pathname(), shared_object);

        debug(1) << "Signing tool: (" << signer << ")\n";
        std::string cmd = signer + " " + input.pathname() + " " + output.pathname();
        int result = system(cmd.c_str());
        internal_assert(result == 0)
            << "HL_HEXAGON_CODE_SIGNER failed: result = " << result
            << " for cmd (" << cmd << ")";

        shared_object = read_entire_file(output.pathname());
    }

    Halide::Buffer<uint8_t> result_buf(shared_object.size(), device_code.name());
    memcpy(result_buf.data(), shared_object.data(), shared_object.size());

    return result_buf;
}

}  // namespace Internal
}  // namespace Halide
