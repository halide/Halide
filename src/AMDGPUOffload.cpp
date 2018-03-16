#include <iostream>
#include <fstream>
#include <memory>

#include "AMDGPUOffload.h"
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
    EM_AMDGPU = 224,
};

// http://www.llvm.org/docs/AMDGPUUsage.html#code-object
enum {
    EF_AMDGPU_MACH_AMDGCN_GFX801 = 0x028, //       gfx801
    EF_AMDGPU_MACH_AMDGCN_GFX802 = 0x029, //       gfx802
    EF_AMDGPU_MACH_AMDGCN_GFX803 = 0x02a, //       gfx803
    EF_AMDGPU_MACH_AMDGCN_GFX810 = 0x02b, //       gfx810
    EF_AMDGPU_MACH_AMDGCN_GFX900 = 0x02c, //       gfx900
    EF_AMDGPU_MACH_AMDGCN_GFX902 = 0x02d, //       gfx902
    EF_AMDGPU_XNACK = 0x100
};

// https://github.com/llvm-mirror/llvm/blob/master/include/llvm/BinaryFormat/ELFRelocs/AMDGPU.def
enum {
    R_AMDGPU_NONE = 0,
    R_AMDGPU_ABS32_LO = 1,
    R_AMDGPU_ABS32_HI = 2,
    R_AMDGPU_ABS64 = 3,
    R_AMDGPU_REL32 = 4,
    R_AMDGPU_REL64 = 5,
    R_AMDGPU_ABS32 = 6,
    R_AMDGPU_GOTPCREL = 7,
    R_AMDGPU_GOTPCREL32_LO = 8,
    R_AMDGPU_GOTPCREL32_HI = 9,
    R_AMDGPU_REL32_LO = 10,
    R_AMDGPU_REL32_HI = 11,
    R_AMDGPU_RELATIVE64 = 13,
};

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

void do_reloc_64(char *addr, uintptr_t val) {
    *((uint64_t*)addr) = (uint64_t)val;
}

void do_reloc_32(char *addr, uintptr_t val) {
    internal_assert(val <= 0xffffffff);
    *((uint32_t*)addr) = val;
}

void do_relocation(uint32_t fixup_offset, char *fixup_addr, uint32_t type,
                   const Symbol *sym, uint32_t sym_offset, int32_t addend,
                   Elf::Section &got) {
    // Amdgpu relocations are specified in section 11.5 in
    // the Amdgpu Application Binary Interface spec.

    // Now we can define the variables from Table 11-5.
    uint32_t S = sym_offset;
    uint32_t P = fixup_offset;
    intptr_t A = addend;

    uint32_t G = got.contents_size();
    for (const Relocation &r : got.relocations()) {
        if (r.get_symbol() == sym) {
            G = r.get_offset();
            debug(2) << "Reusing G=" << G << " for symbol " << sym->get_name() << "\n";
            break;
        }
    }

    bool needs_got_entry = false;

    switch (type) {
    case R_AMDGPU_NONE:
        break;
    case R_AMDGPU_ABS32_LO:
        do_reloc_32(fixup_addr, intptr_t((S + A) & 0xFFFFFFFF));
        break;
    case R_AMDGPU_ABS32_HI:
        do_reloc_32(fixup_addr, intptr_t((S + A) >> 32));
        break;
    case R_AMDGPU_ABS64:
        do_reloc_32(fixup_addr, intptr_t(S + A));
        break;
    case R_AMDGPU_REL32:
        do_reloc_32(fixup_addr, intptr_t(S + A - P));
        break;
    case R_AMDGPU_REL64:
        do_reloc_64(fixup_addr, intptr_t(S + A - P));
        break;
    case R_AMDGPU_ABS32:
        do_reloc_32(fixup_addr, intptr_t(S + A));
        break;
    case R_AMDGPU_GOTPCREL:
        do_reloc_32(fixup_addr, intptr_t(G + GOT + A - P));
        break;
    case R_AMDGPU_GOTPCREL32_LO:
        do_reloc_32(fixup_addr, intptr_t((G + GOT + A - P) & 0xffffffff));
        break;
    case R_AMDGPU_GOTPCREL32_HI:
        do_reloc_32(fixup_addr, intptr_t((G + GOT + A - P) >> 32));
        break;
    case R_AMDGPU_REL32_LO:
        do_reloc_32(fixup_addr, intptr_t((S + A - P) & 0xffffffff));
        break;
    case R_AMDGPU_REL32_HI:
        do_reloc_32(fixup_addr, intptr_t((S + A - P) >> 32));
        break;
    case R_AMDGPU_RELATIVE64:
        do_reloc_64(fixup_addr, intptr_t(B + A));
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

class AMDGPULinker : public Linker {
public:
    uint32_t flags;

    AMDGPULinker(const Target &target) {
        if (target.has_feature(Target::AMDGPUGFX900)) {
            flags = Elf::EF_AMDGPU_MACH_AMDGCN_GFX900;
        } else {
            flags = Elf::EF_AMDGPU_MACH_AMDGCN_GFX900;
        }
    }

    uint16_t get_machine() override { return EM_AMDGPU; }
    uint32_t get_flags() override { return flags; }
    uint32_t get_version() override { return EV_CURRENT; }
    void append_dynamic(Section &dynamic) override {
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
const std::string pipeline_module_name = "halide_amdgpu_code";

// Replace the parameter objects of loads/stores with a new parameter
// object.
class ReplaceParams : public IRMutator2 {
    const std::map<std::string, Parameter> &replacements;

    using IRMutator2::visit;

    Expr visit(const Load *op) override {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            return Load::make(op->type, op->name, mutate(op->index), op->image,
                              i->second, mutate(op->predicate));
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            return Store::make(op->name, mutate(op->value), mutate(op->index),
                               i->second, mutate(op->predicate));
        } else {
            return IRMutator2::visit(op);
        }
    }

public:
    ReplaceParams(const std::map<std::string, Parameter> &replacements)
        : replacements(replacements) {}
};

Stmt replace_params(Stmt s, const std::map<std::string, Parameter> &replacements) {
    return ReplaceParams(replacements).mutate(s);
}

class InjectAmdgpuRpc : public IRMutator2 {
    std::map<std::string, Expr> state_bufs;

    Module &device_code;

    // Alignment info for Int(32) variables in scope, so we don't lose
    // the information when creating Amdgpu kernels.
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
        return state_var("amdgpu_module_state", type_of<void*>());
    }

    Expr module_state_ptr() {
        return state_var_ptr("amdgpu_module_state", type_of<void*>());
    }

    // Create a Buffer containing the given buffer/size, and return an
    // expression for a pointer to the first element.
    Expr buffer_ptr(const uint8_t* buffer, size_t size, const char* name) {
        Buffer<uint8_t> code((int)size, name);
        memcpy(code.data(), buffer, (int)size);
        Expr buf = Variable::make(type_of<halide_buffer_t *>(), string(name) + ".buffer", code);
        return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
    }

    using IRMutator2::visit;

    Stmt visit(const For *loop) override {
        if (loop->device_api != DeviceAPI::AMDGPU) {
            return IRMutator2::visit(loop);
        }

        // Unrolling or loop partitioning might generate multiple
        // loops with the same name, so we need to make them unique.
        // There's a bit of a hack here: the offload_rpc. prefix is
        // significant, it tells the Amdgpu code generator to expect
        // the arguments to be unpacked by the Amdgpu remote-side RPC
        // call, which doesn't work with standard buffers.
        std::string hex_name = unique_name("offload_rpc." + loop->name);

        // After moving this to Amdgpu, it doesn't need to be marked
        // Amdgpu anymore.
        Stmt body = For::make(loop->name, loop->min, loop->extent, loop->for_type,
                              DeviceAPI::None, loop->body);
        body = remove_trivial_for_loops(body);

        // Build a closure for the device code.
        // TODO: Should this move the body of the loop to Amdgpu,
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
            Expr host_ptr = Call::make(Handle(), "_halide_amdgpu_buffer_get_host", {buf}, Call::Extern);
            Expr device_ptr = Call::make(Handle(), "_halide_amdgpu_buffer_get_device", {buf}, Call::Extern);
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
            // The Amdgpu runtime expects buffer args to be
            // passed as just the device and host
            // field. CodeGen_Amdgpu knows how to unpack buffers
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

        return call_extern_and_assert("halide_amdgpu_run", params);
    }

    Expr visit(const Let *op) override {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        Expr expr = IRMutator2::visit(op);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
        return expr;
    }

    Stmt visit(const LetStmt *op) override {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        Stmt stmt = IRMutator2::visit(op);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
        return stmt;
    }

public:
    InjectAmdgpuRpc(Module &device_code) : device_code(device_code) {}

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
            Stmt init_kernels = call_extern_and_assert("halide_amdgpu_initialize_kernels",
                                                       { module_state_ptr(), code_ptr, cast<uint64_t>(code_size), runtime_ptr, cast<uint64_t>(runtime_size) });
            s = Block::make(init_kernels, s);
        }

        // TODO: This can probably go away due to general debug info at the submodule compile level.
        debug(1) << "Amdgpu device code module: " << device_code << "\n";

        return s;
    }
};

}  // namespace

Stmt inject_amdgpu_rpc(Stmt s, const Target &host_target,
                        Module &containing_module) {
    Target target(Target::Linux, Target::X86, 64);
    Module amdgpu_module(pipeline_module_name, target.with_feature(Target::NoRuntime));
    InjectAmdgpuRpc injector(amdgpu_module);
    s = injector.inject(s);

    if (!amdgpu_module.functions().empty()) {
        containing_module.append(amdgpu_module);
    }

    return s;
}

Buffer<uint8_t> compile_module_to_amdgpu_shared_object(const Module &device_code) {
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(device_code, context));

    // Write intermediate bitcode to disk if requested.
    // TODO: We really need something better than this. This won't
    // work in non-trivial JIT or AOT programs.
    std::string bitcode_dump_path = get_env_variable("HL_AMDGPU_DUMP_BITCODE");
    if (!bitcode_dump_path.empty()) {
        auto fd_ostream = make_raw_fd_ostream(bitcode_dump_path);
        compile_llvm_module_to_llvm_bitcode(*llvm_module, *fd_ostream);
        debug(0) << "Wrote Amdgpu device bitcode to " << bitcode_dump_path;
    }

    llvm::SmallVector<char, 4096> object;
    llvm::raw_svector_ostream object_stream(object);
    compile_llvm_module_to_object(*llvm_module, object_stream);

    int min_debug_level = device_code.name() == runtime_module_name ? 3 : 2;
    if (debug::debug_level() >= min_debug_level) {
        debug(0) << "AMDGPU device code assembly: " << "\n";
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
        // When the Amdgpu loader is used on 8998 and later targets,
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
    Elf::AMDGPULinker linker(device_code.target());
    std::vector<std::string> dependencies = {
        "libhalide_amdgpu_remote_skel.so",
    };
    std::vector<char> shared_object = obj->write_shared_object(&linker, dependencies, soname);

    std::string signer = get_env_variable("HL_AMDGPU_CODE_SIGNER");
    if (!signer.empty()) {
        // If signer is specified, shell out to a tool/script that will
        // sign the AMDGPU code in a specific way. The tool is expected
        // to be of the form
        //
        //     signer /path/to/unsigned.so /path/to/signed.so
        //
        // where unsigned and signed paths must not be the same file.
        // If the signed file already exists, it will be overwritten.

        TemporaryFile input("amdgpu_unsigned", ".so");
        TemporaryFile output("amdgpu_signed", ".so");

        debug(1) << "Signing Amdgpu code: " << input.pathname() << " -> " << output.pathname() << "\n";

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
            << "HL_AMDGPU_CODE_SIGNER failed: result = " << result
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

}  // namespace Internal
}  // namespace Halide
