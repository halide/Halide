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

enum {
    EV_CURRENT = 1,
};

enum {
    EM_HEXAGON = 164,
};

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
};

enum {
    DT_HEXAGON_VER = 0x70000001,
};

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

// Defined below.
//extern std::vector<Instruction> instruction_encodings;

void do_reloc(char *addr, uint32_t mask, uintptr_t val, bool is_signed, bool verify) {
    uint32_t inst = *((uint32_t *)addr);
#if 0
    log_printf("Fixup inside instruction at %lx:\n  %08lx\n",
               (uint32_t)(addr - get_addr(get_section_offset(sec_text))), inst);
    log_printf("val: 0x%08lx\n", (unsigned long)val);
    log_printf("mask: 0x%08lx\n", mask);
#endif

    if (!mask) {

        // The mask depends on the instruction. To implement
        // relocations for new instructions see
        // instruction_encodings.txt
#if 0
        // First print the bits so I can search for it in the
        // instruction encodings.
        log_printf("Instruction bits: ");
        for (int i = 31; i >=0; i--) {
            log_printf("%d", (int)((inst >> i) & 1));
        }
        log_printf("\n");
#endif

        if ((inst & (3 << 14)) == 0) {
            // Some instructions are actually pairs of 16-bit
            // subinstructions. See section 3.7 in the
            // programmer's reference.
            debug(0) << "Duplex!\n";

            int iclass = ((inst >> 29) << 1) | ((inst >> 13) & 1);
#if 0
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
#endif

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

        } else if ((inst >> 24) == 72) {
            // Example instruction encoding that has this high byte (ignoring bits 1 and 2):
            // 0100 1ii0  000i iiii  PPit tttt  iiii iiii
            debug(0) << "Instruction-specific case A\n";
            mask = 0x061f20ff;
        } else if ((inst >> 24) == 73) {
            // 0100 1ii1  000i iiii  PPii iiii  iiid dddd
            debug(0) << "Instruction-specific case B\n";
            mask = 0x061f3fe0;
        } else if ((inst >> 24) == 120) {
            // 0111 1000  ii-i iiii  PPii iiii  iiid dddd
            debug(0) << "Instruction-specific case C\n";
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
            internal_error << "Unhandled instruction type!\n";
        }
    }

    //uintptr_t old_val = val;
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

    internal_assert(!verify || consumed_every_bit);

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

    uint32_t G = got.get_contents().size();
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
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A), _unsigned, truncate);
        break;
    case 5:
        // Untested
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_LO, uintptr_t(S + A) >> 16, _unsigned, truncate);
        break;
    case 6:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32, intptr_t(S + A), _unsigned, truncate);
        break;
    case 7:
        // Untested
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word16, uintptr_t(S + A), _unsigned, truncate);
        break;
    case 8:
        // Untested
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word8, uintptr_t(S + A), _unsigned, truncate);
        break;
    case 9:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP), _unsigned, verify);
        break;
    case 10:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 1, _unsigned, verify);
        break;
    case 11:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 2, _unsigned, verify);
        break;
    case 12:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_GP, uintptr_t(S + A - GP) >> 3, _unsigned, verify);
        break;
    case 13:
        // Untested
        internal_error << "Not pic code " << type << "\n";
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
        internal_error << "Not pic code " << type << "\n";
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
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_U6, uintptr_t(S + A), _unsigned, truncate);
        break;
    case 24:
        internal_error << "Not pic code " << type << "\n";
        do_reloc(fixup_addr, Word32_R6, uintptr_t(S + A), _unsigned, truncate);
        break;
    case 25: // These ones all seem to mean the same thing. Only 30 is tested.
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
        internal_error << "Not pic code " << type << "\n";
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
        needs_got_entry = true;
        break;
    case 71:
        do_reloc(fixup_addr, Word32_U6, uintptr_t(G), _unsigned, truncate);
        needs_got_entry = true;
        break;

    default:
        internal_error << "Unhandled relocation type " << type << "\n";
    }

    if (needs_got_entry && G == got.get_contents().size()) {
        debug(2) << "Adding GOT entry " << G << " for symbol " << sym->get_name() << "\n";
        got.append_contents((uint32_t)0);
        got.add_relocation(Relocation(R_HEX_GLOB_DAT, G, 0, sym));
    }
}

class HexagonLinker : public Linker {
public:
    uint32_t flags;

    HexagonLinker(const Target &target) {
        if (target.has_feature(Target::HVX_v62)) {
            flags = Elf::EF_HEXAGON_MACH_V62;
        } else {
            flags = Elf::EF_HEXAGON_MACH_V60;
        }
    }

    uint16_t get_machine() { return EM_HEXAGON; }
    uint32_t get_flags() { return flags; }
    uint32_t get_version() { return EV_CURRENT; }
    void append_dynamic(Section &dynamic) {
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

        uint64_t got_offset = got.get_contents().size();
        got.append_contents((uint32_t)0);
        got.add_relocation(Elf::Relocation(R_HEX_GLOB_DAT, got_offset, 0, &sym));
        return got_offset;
    }

    bool needs_plt_entry(const Relocation &r) override {
        return maybe_branch_inst(r.get_type());
    }

    // Add a PLT entry for the external symbol sym. plt_symbol gets
    // the new symbol in the PLT.
    Symbol get_plt_entry(const Symbol &sym, Section &plt, Section &got, const Symbol &got_sym) override {
        if (got.get_contents().empty()) {
            // The got hasn't been started, initialize it now.
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
        uint64_t got_offset = got.get_contents().size();
        got.append_contents((uint32_t)0);
        got.add_relocation(Elf::Relocation(R_HEX_JMP_SLOT, got_offset, 0, &sym));

        // Add the PLT code.
        uint32_t plt_offset = plt.get_size();
        plt.append_contents(hexagon_plt1, hexagon_plt1 + sizeof(hexagon_plt1));

        Symbol plt_sym("plt_" + sym.get_name());
        // Change sym to point to the PLT.
        plt_sym
            .set_type(Symbol::STT_FUNC)
            .set_binding(Symbol::STB_LOCAL)
            .define(&plt, plt_offset, sizeof(hexagon_plt1));

        plt.add_relocation(Relocation(R_HEX_B32_PCREL_X, plt_offset + 0, got_offset, &got_sym));
        plt.add_relocation(Relocation(R_HEX_6_PCREL_X, plt_offset + 4, got_offset + 4, &got_sym));

        return plt_sym;
    }

    Relocation relocate(uint64_t fixup_offset, char *fixup_addr, uint64_t type,
                        const Elf::Symbol *sym, uint64_t sym_offset, int64_t addend,
                        Elf::Section &got) {
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
    std::map<std::string, Expr> state_vars;

    Module device_code;

    // Alignment info for Int(32) variables in scope, so we don't lose
    // the information when creating Hexagon kernels.
    Scope<ModulusRemainder> alignment_info;

    Expr state_var(const std::string& name, Type type) {
        Expr& var = state_vars[name];
        if (!var.defined()) {
            auto storage = Buffer<void *>::make_scalar(name + "_buf");
            storage() = nullptr;
            var = Load::make(type_of<void*>(), storage.name(), 0, storage, Parameter(), const_true());
        }
        return var;
    }

    Expr state_var_ptr(const std::string& name, Type type) {
        Expr var = state_var(name, type);
        return Call::make(Handle(), Call::address_of, {var}, Call::Intrinsic);
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
        Expr ptr_0 = Load::make(type_of<uint8_t>(), name, 0, code, Parameter(), const_true());
        return Call::make(Handle(), Call::address_of, {ptr_0}, Call::Intrinsic);
    }

    using IRMutator::visit;

    void visit(const For *loop) {
        if (loop->device_api != DeviceAPI::Hexagon) {
            IRMutator::visit(loop);
            return;
        }

        // Unrolling or loop partitioning might generate multiple
        // loops with the same name, so we need to make them unique.
        std::string hex_name = unique_name("hex_" + loop->name);

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
                Expr host_ptr = reinterpret<uint64_t>(Variable::make(Handle(), i.first + ".host"));
                Expr error = Call::make(Int(32), "halide_error_unaligned_host_ptr",
                                        {i.first, alignment}, Call::Extern);
                body = Block::make(AssertStmt::make(host_ptr % alignment == 0, error), body);
            }
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
    InjectHexagonRpc(const Target &target) : device_code("hexagon", target) {}

    Stmt inject(Stmt s) {
        s = mutate(s);

        // Skip if there are no device kernels.
        if (device_code.functions().empty()) {
            return s;
        }

        // Compile the device code
        debug(1) << "Hexagon device code module: " << device_code << "\n";

        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(device_code, context));

        llvm::SmallVector<char, 4096> object;
        llvm::raw_svector_ostream object_stream(object);
        compile_llvm_module_to_object(*llvm_module, object_stream);

        if (debug::debug_level() >= 2) {
            debug(2) << "Hexagon device code assembly: " << "\n";
            llvm::SmallString<4096> assembly;
            llvm::raw_svector_ostream assembly_stream(assembly);
            compile_llvm_module_to_assembly(*llvm_module, assembly_stream);
            debug(2) << assembly.c_str() << "\n";
        }

        auto obj = Elf::Object::parse_object(object.data(), object.size());
        internal_assert(obj);

        // Generate just one .text section.
        obj->merge_text_sections();

        // Make .bss a real section.
        auto bss = obj->find_section(".bss");
        if (bss != obj->sections_end()) {
            bss->set_alignment(128);
            bss->set_type(Elf::Section::SHT_PROGBITS);
        }

        // Link into a shared object.
        Elf::HexagonLinker linker(device_code.target());
        std::vector<char> shared_object = obj->write_shared_object(&linker);

        // Wrap the statement in calls to halide_initialize_kernels.
        size_t code_size = shared_object.size();
        Expr code_ptr = buffer_ptr(reinterpret_cast<uint8_t*>(&shared_object[0]), code_size, "hexagon_code");
        Stmt init_kernels = call_extern_and_assert("halide_hexagon_initialize_kernels",
                                                   {module_state_ptr(), code_ptr, Expr((uint64_t)code_size)});
        s = Block::make(init_kernels, s);

        return s;
    }
};

}  // namespace

Stmt inject_hexagon_rpc(Stmt s, const Target &host_target) {
    // Make a new target for the device module.
    Target target(Target::NoOS, Target::Hexagon, 32);
    target = target.with_feature(Target::NoAsserts);

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
    };
    for (Target::Feature i : shared_features) {
        if (host_target.has_feature(i)) {
            target = target.with_feature(i);
        }
    }

    InjectHexagonRpc injector(target);
    s = injector.inject(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
