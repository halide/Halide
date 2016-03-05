#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_C.h"
#include "Elf.h"

#include <iostream>
#include <fstream>

namespace Halide {

llvm::raw_fd_ostream *new_raw_fd_ostream(const std::string &filename) {
    std::string error_string;
    #if LLVM_VERSION < 35
    llvm::raw_fd_ostream *raw_out = new llvm::raw_fd_ostream(filename.c_str(), error_string);
    #elif LLVM_VERSION == 35
    llvm::raw_fd_ostream *raw_out = new llvm::raw_fd_ostream(filename.c_str(), error_string, llvm::sys::fs::F_None);
    #else // llvm 3.6
    std::error_code err;
    llvm::raw_fd_ostream *raw_out = new llvm::raw_fd_ostream(filename.c_str(), err, llvm::sys::fs::F_None);
    if (err) error_string = err.message();
    #endif
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    return raw_out;
}

namespace Internal {

bool get_md_bool(LLVMMDNodeArgumentType value, bool &result) {
    #if LLVM_VERSION < 36 || defined(WITH_NATIVE_CLIENT)
    llvm::ConstantInt *c = llvm::cast<llvm::ConstantInt>(value);
    #else
    llvm::ConstantAsMetadata *cam = llvm::cast<llvm::ConstantAsMetadata>(value);
    llvm::ConstantInt *c = llvm::cast<llvm::ConstantInt>(cam->getValue());
    #endif
    if (c) {
        result = !c->isZero();
        return true;
    }
    return false;
}

bool get_md_string(LLVMMDNodeArgumentType value, std::string &result) {
    #if LLVM_VERSION < 36
    if (llvm::dyn_cast<llvm::ConstantAggregateZero>(value)) {
        result = "";
        return true;
    }
    llvm::ConstantDataArray *c = llvm::cast<llvm::ConstantDataArray>(value);
    if (c) {
        result = c->getAsCString();
        return true;
    }
    #else
    llvm::MDString *c = llvm::dyn_cast<llvm::MDString>(value);
    if (c) {
        result = c->getString();
        return true;
    }
    #endif
    return false;
}

void push_blob(std::vector<uint8_t>& dest, const uint8_t *begin, const uint8_t *end) {
    dest.insert(dest.end(), begin, end);
}

template <typename T>
void push_blob(std::vector<uint8_t>& dest, const T& blob) {
    push_blob(dest, reinterpret_cast<const uint8_t*>(&blob), reinterpret_cast<const uint8_t*>(&blob + 1));
}

template <typename It>
void push_blob(std::vector<uint8_t>& dest, It begin, It end) {
    for (It i = begin; i != end; i++) {
        push_blob(dest, *i);
    }
}

void align_blob(std::vector<uint8_t>& dest, size_t alignment) {
    dest.resize((dest.size() + alignment - 1) & ~(alignment - 1));
}

template <typename AddrType>
void ld_shared_impl(std::vector<uint8_t>& obj) {
    typedef Elf::Object<AddrType> ElfObj;
    typedef typename Elf::Ehdr<AddrType> Ehdr;
    typedef typename Elf::Shdr<AddrType> Shdr;
    typedef typename Elf::Phdr<AddrType> Phdr;
    typedef typename Elf::Sym<AddrType> Sym;
    typedef typename Elf::Dyn<AddrType> Dyn;
    typedef Elf::Rel Rel;
    typedef Elf::Rela Rela;
    ElfObj input(obj);

    Ehdr header = input.header();
    header.e_type = Elf::ET_DYN;
    header.e_shnum = 0;
    header.e_shoff = 0;
    header.e_shstrndx = 0;

    AddrType base_vaddr = 0x40000000;

    Shdr *symbol_table = input.get_symbol_table();
    internal_assert(symbol_table);
    std::vector<Sym> symbols;
    for (uint32_t i = 0; i < symbol_table->entry_count(); i++) {
        symbols.push_back(input.template section_entry<Sym>(*symbol_table, i));
    }

    // Gather the text and data sections together.
    std::vector<uint8_t> text;
    std::vector<uint8_t> data;
    std::vector<Rel> text_rel, data_rel;
    std::vector<Rela> text_rela, data_rela;

    // We want to keep the symbols in order, so we don't have to
    // update symbol entry indices. To do this, we keep all the
    // symbols in the above vector, and just keep pointers to the ones
    // that appear in the text and data sections so we can update
    // their offsets appropriately later.
    std::vector<Sym*> text_symbols, data_symbols;

    Shdr *string_table = input.get_string_table();
    std::vector<char> strings(&obj[string_table->sh_offset], &obj[string_table->sh_offset + string_table->sh_size]);

    for (uint16_t i = 0; i < input.section_count(); i++) {
        Shdr &shdr = input.section_header(i);
        if (shdr.sh_flags & Elf::SHF_ALLOC) {
            std::vector<uint8_t> *dest;
            std::vector<Rel> *dest_rel;
            std::vector<Rela> *dest_rela;
            std::vector<Sym*> *dest_symbols;
            if (shdr.sh_flags & Elf::SHF_WRITE) {
                dest = &data;
                dest_rel = &data_rel;
                dest_rela = &data_rela;
                dest_symbols = &data_symbols;
            } else {
                dest = &text;
                dest_rel = &text_rel;
                dest_rela = &text_rela;
                dest_symbols = &text_symbols;
           }
            // Find relocation sections for this section, and save the
            // relocations with updated offsets. The relocations
            // currently are relative to their respective section, we
            // update them to be relative to the start of the
            // text/data blobs, and later update them with the base
            // virtual address.
            Shdr* rel_shdr = input.find_rel_for_section(i);
            if (rel_shdr) {
                for (size_t j = 0; j < rel_shdr->entry_count(); j++) {
                    Rel rel = input.template section_entry<Rel>(*rel_shdr, j);
                    rel.r_offset += dest->size();
                    dest_rel->push_back(rel);
                }
            }
            Shdr* rela_shdr = input.find_rela_for_section(i);
            if (rela_shdr) {
                for (size_t j = 0; j < rela_shdr->entry_count(); j++) {
                    Rela rela = input.template section_entry<Rela>(*rela_shdr, j);
                    rela.r_offset += dest->size();
                    dest_rela->push_back(rela);
                }
            }

            // Update the values of the symbols contained in this section.
            for (Sym& sym : symbols) {
                if (sym.st_shndx == i) {
                    sym.st_value += dest->size();
                    sym.st_shndx = 0;
                    dest_symbols->push_back(&sym);
                }
            }

            push_blob(*dest, &obj[shdr.sh_offset], &obj[shdr.sh_offset + shdr.sh_size]);
            // TODO: Align dest so the next section is aligned?
        }
    }

    align_blob(text, 4096);
    align_blob(data, 4096);

    size_t text_vaddr = base_vaddr;
    size_t data_vaddr = text_vaddr + text.size();

    // Now we know where everything is going to land in the file. Update the offsets.
    for (Rel& i : text_rel) { i.r_offset += text_vaddr; }
    for (Rela& i : text_rela) { i.r_offset += text_vaddr; }
    for (Sym* i : text_symbols) { i->st_value += text_vaddr; }

    for (Rel& i : data_rel) { i.r_offset += data_vaddr; }
    for (Rela& i : data_rela) { i.r_offset += data_vaddr; }
    for (Sym* i : data_symbols) { i->st_value += data_vaddr; }

    // Make the blob containing the information in PT_DYNAMIC
    std::vector<Dyn> dyn;
    std::vector<uint8_t> dynamic;
    size_t dynamic_vaddr = data_vaddr + data.size();

    // Make a trivial hash table with nbucket = 1.
    std::vector<uint32_t> hash_table;
    hash_table.push_back(1);  // nbucket
    hash_table.push_back(symbols.size());  // nchain
    hash_table.push_back(0); // All the symbols are in one chain.
    for (uint32_t i = 0; i < symbols.size(); i++) {
        hash_table.push_back(i);
    }
    hash_table.push_back(0);  // STN_UNDEF

    dyn.push_back(Dyn::make_ptr(Elf::DT_HASH, dynamic_vaddr + dynamic.size()));
    push_blob(dynamic, hash_table.begin(), hash_table.end());

    // Add the string table.
    dyn.push_back(Dyn::make_ptr(Elf::DT_STRTAB, dynamic_vaddr + dynamic.size()));
    dyn.push_back(Dyn::make_val(Elf::DT_STRSZ, strings.size()));
    push_blob(dynamic, strings.begin(), strings.end());

    // Add the symbol table.
    dyn.push_back(Dyn::make_ptr(Elf::DT_SYMTAB, dynamic_vaddr + dynamic.size()));
    dyn.push_back(Dyn::make_val(Elf::DT_SYMENT, sizeof(symbols[0])));
    // TODO: Symbol count?
    push_blob(dynamic, symbols.begin(), symbols.end());

    // Add relocations.
    dyn.push_back(Dyn::make_ptr(Elf::DT_REL, dynamic_vaddr + dynamic.size()));
    dyn.push_back(Dyn::make_val(Elf::DT_RELSZ, sizeof(Rel) * (data_rel.size() + text_rel.size())));
    dyn.push_back(Dyn::make_val(Elf::DT_RELENT, sizeof(Rel)));
    push_blob(dynamic, text_rel.begin(), text_rel.end());
    push_blob(dynamic, data_rel.begin(), data_rel.end());

    dyn.push_back(Dyn::make_ptr(Elf::DT_RELA, dynamic_vaddr + dynamic.size()));
    dyn.push_back(Dyn::make_val(Elf::DT_RELASZ, sizeof(Rela) * (data_rela.size() + text_rela.size())));
    dyn.push_back(Dyn::make_val(Elf::DT_RELAENT, sizeof(Rela)));
    push_blob(dynamic, text_rela.begin(), text_rela.end());
    push_blob(dynamic, data_rela.begin(), data_rela.end());

    // We might have relocations in read only segments.
    dyn.push_back(Dyn::make_val(Elf::DT_TEXTREL, 0));

    // Null terminator.
    dyn.push_back(Dyn::make_val(Elf::DT_NULL, 0));

    align_blob(dynamic, 4096);

    std::vector<uint8_t> dyn_data;
    push_blob(dyn_data, dyn.begin(), dyn.end());

    std::vector<Phdr> ph(4);
    ph[0].p_type = Elf::PT_LOAD;
    ph[0].p_offset = text_vaddr - base_vaddr + 4096;
    ph[0].p_vaddr = text_vaddr;
    ph[0].p_paddr = 0;
    ph[0].p_filesz = text.size();
    ph[0].p_memsz = text.size();
    ph[0].p_flags = Elf::PF_X | Elf::PF_R;
    ph[0].p_align = 4096;

    ph[1].p_type = Elf::PT_LOAD;
    ph[1].p_offset = data_vaddr - base_vaddr + ph[0].p_offset;
    ph[1].p_vaddr = data_vaddr;
    ph[1].p_paddr = 0;
    ph[1].p_filesz = data.size();
    ph[1].p_memsz = data.size();
    ph[1].p_flags = Elf::PF_R | Elf::PF_W;
    ph[1].p_align = 4096;

    ph[2].p_type = Elf::PT_LOAD;
    ph[2].p_offset = dynamic_vaddr - base_vaddr + ph[0].p_offset;
    ph[2].p_vaddr = dynamic_vaddr;
    ph[2].p_paddr = 0;
    ph[2].p_filesz = dynamic.size();
    ph[2].p_memsz = dynamic.size();
    ph[2].p_flags = Elf::PF_R;
    ph[2].p_align = 4096;

    ph[3].p_type = Elf::PT_DYNAMIC;
    ph[3].p_offset = ph[2].p_offset + ph[2].p_filesz;
    ph[3].p_vaddr = ph[2].p_vaddr + ph[2].p_memsz;
    ph[3].p_paddr = 0;
    ph[3].p_filesz = dyn_data.size();
    ph[3].p_memsz = dyn_data.size();
    ph[3].p_flags = Elf::PF_R;
    ph[3].p_align = 1;
/*
    std::vector<Shdr> sh(4);
    sh[0].sh_type = Elf::SHT_PROGBITS;
    sh[0].sh_offset = ph[0].p_offset;
    sh[0].sh_size = ph[0].p_filesz;
    sh[0].sh_name = 1;
    sh[0].sh_
*/
    // We've built a bunch of the things we need to assemble the
    // shared object. Do so now.
    std::vector<uint8_t> so;

    // Add the header.
    header.e_phoff = sizeof(header);
    header.e_phentsize = sizeof(ph[0]);
    header.e_phnum = ph.size();
/*
    header.e_shoff = sizeof(header) + header.e_phentsize*header.e_phnum;
    header.e_shentsize = sizeof(sh[0]);
    header.e_shnum = sh.size();
*/
    push_blob(so, header);

    // Add the program header table.
    push_blob(so, ph.begin(), ph.end());

    // Add the section header table.
//    push_blob(so, sh.begin(), sh.end());

    align_blob(so, 4096);
    internal_assert(so.size() == 4096);

    // Add the text, data, and dynamic blobs.
    so.insert(so.end(), text.begin(), text.end());
    so.insert(so.end(), data.begin(), data.end());
    so.insert(so.end(), dynamic.begin(), dynamic.end());
    so.insert(so.end(), dyn_data.begin(), dyn_data.end());

    obj = so;
}

void ld_shared(std::vector<uint8_t>& obj) {
    Elf::Ident *ident = reinterpret_cast<Elf::Ident *>(obj.data());
    internal_assert(ident->magic == Elf::EI_MAG) << "Cannot make a shared object out of non-ELF object.\n";
    internal_assert(ident->endianness == Elf::ELFDATA2LSB) << "Big-endian ELF not supported.\n";

    if (ident->bitness == Elf::ELFCLASS32) {
        ld_shared_impl<uint32_t>(obj);
    } else if (ident->bitness == Elf::ELFCLASS64) {
        ld_shared_impl<uint64_t>(obj);
    } else {
        internal_assert(false) << "ELF object was not 32 or 64 bit.";
    }
}

}  // namespace Internal

void get_target_options(const llvm::Module &module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs) {
    bool use_soft_float_abi = false;
    Internal::get_md_bool(module.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi);
    Internal::get_md_string(module.getModuleFlag("halide_mcpu"), mcpu);
    Internal::get_md_string(module.getModuleFlag("halide_mattrs"), mattrs);

    options = llvm::TargetOptions();
    options.LessPreciseFPMADOption = true;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    #if LLVM_VERSION < 37
    options.NoFramePointerElim = false;
    options.UseSoftFloat = false;
    #endif
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    #if LLVM_VERSION < 37
    options.DisableTailCalls = false;
    #endif
    options.StackAlignmentOverride = 0;
    #if LLVM_VERSION < 37
    options.TrapFuncName = "";
    #endif
    options.PositionIndependentExecutable = true;
    options.FunctionSections = true;
    #ifdef WITH_NATIVE_CLIENT
    options.UseInitArray = true;
    #else
    options.UseInitArray = false;
    #endif
    options.FloatABIType =
        use_soft_float_abi ? llvm::FloatABI::Soft : llvm::FloatABI::Hard;
}


void clone_target_options(const llvm::Module &from, llvm::Module &to) {
    to.setTargetTriple(from.getTargetTriple());

    llvm::LLVMContext &context = to.getContext();

    bool use_soft_float_abi = false;
    if (Internal::get_md_bool(from.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi))
        to.addModuleFlag(llvm::Module::Warning, "halide_use_soft_float_abi", use_soft_float_abi ? 1 : 0);

    std::string mcpu;
    if (Internal::get_md_string(from.getModuleFlag("halide_mcpu"), mcpu)) {
        #if LLVM_VERSION < 36
        to.addModuleFlag(llvm::Module::Warning, "halide_mcpu", llvm::ConstantDataArray::getString(context, mcpu));
        #else
        to.addModuleFlag(llvm::Module::Warning, "halide_mcpu", llvm::MDString::get(context, mcpu));
        #endif
    }

    std::string mattrs;
    if (Internal::get_md_string(from.getModuleFlag("halide_mattrs"), mattrs)) {
        #if LLVM_VERSION < 36
        to.addModuleFlag(llvm::Module::Warning, "halide_mattrs", llvm::ConstantDataArray::getString(context, mattrs));
        #else
        to.addModuleFlag(llvm::Module::Warning, "halide_mattrs", llvm::MDString::get(context, mattrs));
        #endif
    }
}


llvm::TargetMachine *get_target_machine(const llvm::Module &module) {
    std::string error_string;

    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(module.getTargetTriple(), error_string);
    if (!target) {
        std::cout << error_string << std::endl;
        llvm::TargetRegistry::printRegisteredTargetsForVersion();
    }
    internal_assert(target) << "Could not create target for " << module.getTargetTriple() << "\n";

    llvm::TargetOptions options;
    std::string mcpu = "";
    std::string mattrs = "";
    get_target_options(module, options, mcpu, mattrs);

    return target->createTargetMachine(module.getTargetTriple(),
                                       mcpu, mattrs,
                                       options,
                                       llvm::Reloc::PIC_,
                                       llvm::CodeModel::Default,
                                       llvm::CodeGenOpt::Aggressive);
}

#if LLVM_VERSION < 37
void emit_legacy(llvm::Module &module, llvm::raw_ostream& raw_out, llvm::TargetMachine::CodeGenFileType file_type) {
    Internal::debug(1) << "emit_file_legacy.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module.getTargetTriple() << "\n";

    // Get the target specific parser.
    llvm::TargetMachine *target_machine = get_target_machine(module);
    internal_assert(target_machine) << "Could not allocate target machine!\n";

    // Build up all of the passes that we want to do to the module.
    llvm::PassManager pass_manager;

    // Add an appropriate TargetLibraryInfo pass for the module's triple.
    pass_manager.add(new llvm::TargetLibraryInfo(llvm::Triple(module.getTargetTriple())));

    #if LLVM_VERSION < 33
    pass_manager.add(new llvm::TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                                   target_machine->getVectorTargetTransformInfo()));
    #else
    target_machine->addAnalysisPasses(pass_manager);
    #endif

    #if LLVM_VERSION < 35
    llvm::DataLayout *layout = new llvm::DataLayout(module.get());
    Internal::debug(2) << "Data layout: " << layout->getStringRepresentation();
    pass_manager.add(layout);
    #endif

    // Make sure things marked as always-inline get inlined
    pass_manager.add(llvm::createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    target_machine->setAsmVerbosityDefault(true);

    llvm::formatted_raw_ostream *out = new llvm::formatted_raw_ostream(*raw_out);

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, *out, file_type);

    pass_manager.run(module);

    delete out;

    delete target_machine;
}

void emit_file_legacy(llvm::Module &module, const std::string &filename, llvm::TargetMachine::CodeGenFileType file_type) {
    llvm::raw_fd_ostream *raw_out = new_raw_fd_ostream(filename);

    emit_legacy(module, *raw_out, file_type);

    delete raw_out;
}
#endif

void emit(llvm::Module &module, llvm::raw_pwrite_stream& raw_out, llvm::TargetMachine::CodeGenFileType file_type) {
#if LLVM_VERSION < 37
    emit_legacy(module, ostream, file_type);
#else
    Internal::debug(1) << "emit_file.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module.getTargetTriple() << "\n";

    // Get the target specific parser.
    llvm::TargetMachine *target_machine = get_target_machine(module);
    internal_assert(target_machine) << "Could not allocate target machine!\n";

    #if LLVM_VERSION == 37
        llvm::DataLayout target_data_layout(*(target_machine->getDataLayout()));
    #else
        llvm::DataLayout target_data_layout(target_machine->createDataLayout());
    #endif
    if (!(target_data_layout == module.getDataLayout())) {
        internal_error << "Warning: module's data layout does not match target machine's\n"
                       << target_data_layout.getStringRepresentation() << "\n"
                       << module.getDataLayout().getStringRepresentation() << "\n";
    }

    // Build up all of the passes that we want to do to the module.
    llvm::legacy::PassManager pass_manager;

    pass_manager.add(new llvm::TargetLibraryInfoWrapperPass(llvm::Triple(module.getTargetTriple())));


    // Add internal analysis passes from the target machine.
    pass_manager.add(llvm::createTargetTransformInfoWrapperPass(target_machine->getTargetIRAnalysis()));

    // Make sure things marked as always-inline get inlined
    pass_manager.add(llvm::createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, raw_out, file_type);

    pass_manager.run(module);

    delete target_machine;
#endif
}

void emit_file(llvm::Module &module, const std::string &filename, llvm::TargetMachine::CodeGenFileType file_type) {
    std::unique_ptr<llvm::raw_fd_ostream> out(new_raw_fd_ostream(filename));
    emit(module, *out, file_type);
}

std::unique_ptr<llvm::Module> compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context) {
    return codegen_llvm(module, context);
}

void compile_llvm_module_to_object(llvm::Module &module, const std::string &filename) {
    emit_file(module, filename, llvm::TargetMachine::CGFT_ObjectFile);
}

void compile_llvm_module_to_object(llvm::Module &module, std::vector<uint8_t> &object) {
    llvm::SmallVector<char, 8> sv;
    llvm::raw_svector_ostream buffer_stream(sv);
    emit(module, buffer_stream, llvm::TargetMachine::CGFT_ObjectFile);
    object.resize(sv.size());
    std::copy(sv.begin(), sv.end(), object.begin());
}

void compile_llvm_module_to_shared_object(llvm::Module &module, std::vector<uint8_t> &object) {
    compile_llvm_module_to_object(module, object);
    Internal::ld_shared(object);
}

void compile_llvm_module_to_shared_object(llvm::Module &module, const std::string &filename) {
    std::vector<uint8_t> object;
    compile_llvm_module_to_shared_object(module, object);
    std::unique_ptr<llvm::raw_fd_ostream> out(new_raw_fd_ostream(filename));
    out->pwrite(reinterpret_cast<const char *>(&object[0]), object.size(), 0);
}

void compile_llvm_module_to_assembly(llvm::Module &module, const std::string &filename) {
    emit_file(module, filename, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_native(llvm::Module &module,
                                   const std::string &object_filename,
                                   const std::string &assembly_filename) {
    emit_file(module, object_filename, llvm::TargetMachine::CGFT_ObjectFile);
    emit_file(module, assembly_filename, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_llvm_bitcode(llvm::Module &module, const std::string &filename) {
    llvm::raw_fd_ostream *file = new_raw_fd_ostream(filename);
    WriteBitcodeToFile(&module, *file);
    delete file;
}

void compile_llvm_module_to_llvm_assembly(llvm::Module &module, const std::string &filename) {
    llvm::raw_fd_ostream *file = new_raw_fd_ostream(filename);
    module.print(*file, NULL);
    delete file;
}

}  // namespace Halide
