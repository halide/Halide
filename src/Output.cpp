#include "CodeGen_LLVM.h"
#include "CodeGen_C.h"
#include "LLVM_Headers.h"
#include "StmtToHtml.h"
#include "Output.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace llvm;

namespace Halide {
namespace Internal {

raw_fd_ostream *new_raw_fd_ostream(const std::string &filename) {
    std::string error_string;
#if LLVM_VERSION < 35
    raw_fd_ostream *raw_out = new raw_fd_ostream(filename.c_str(), error_string);
#elif LLVM_VERSION == 35
    raw_fd_ostream *raw_out = new raw_fd_ostream(filename.c_str(), error_string, llvm::sys::fs::F_None);
#else // llvm 3.6
    std::error_code err;
    raw_fd_ostream *raw_out = new raw_fd_ostream(filename.c_str(), err, llvm::sys::fs::F_None);
    if (err) error_string = err.message();
#endif
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    return raw_out;
}

class LLVMOutput : public OutputBase {
public:
    LLVMOutput(const std::string &bitcode_filename,
                 const std::string &llvm_assembly_filename,
                 const std::string& object_filename,
                 const std::string &assembly_filename)
        : bitcode_filename(bitcode_filename),
          llvm_assembly_filename(llvm_assembly_filename),
          object_filename(object_filename),
          assembly_filename(assembly_filename) {}

    static void generate(CodeGen_LLVM *cg, const std::string &filename,
                         TargetMachine::CodeGenFileType file_type) {
        llvm::Module *module = cg->get_module();

        // Get the target specific parser.
        string error_string;
        debug(1) << "Compiling to native code...\n";
        debug(2) << "Target triple: " << module->getTargetTriple() << "\n";

        const llvm::Target *target = TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
        if (!target) {
            cout << error_string << endl;
            TargetRegistry::printRegisteredTargetsForVersion();
        }
        internal_assert(target) << "Could not create target\n";

        debug(2) << "Selected target: " << target->getName() << "\n";

        TargetOptions options;
        options.LessPreciseFPMADOption = true;
        options.NoFramePointerElim = false;
        options.AllowFPOpFusion = FPOpFusion::Fast;
        options.UnsafeFPMath = true;
        options.NoInfsFPMath = true;
        options.NoNaNsFPMath = true;
        options.HonorSignDependentRoundingFPMathOption = false;
        options.UseSoftFloat = false;
        options.FloatABIType =
            cg->use_soft_float_abi() ? FloatABI::Soft : FloatABI::Hard;
        options.NoZerosInBSS = false;
        options.GuaranteedTailCallOpt = false;
        options.DisableTailCalls = false;
        options.StackAlignmentOverride = 0;
        options.TrapFuncName = "";
        options.PositionIndependentExecutable = true;
        options.UseInitArray = false;

        TargetMachine *target_machine =
            target->createTargetMachine(module->getTargetTriple(),
                                        cg->mcpu(), cg->mattrs(),
                                        options,
                                        Reloc::PIC_,
                                        CodeModel::Default,
                                        CodeGenOpt::Aggressive);

        internal_assert(target_machine) << "Could not allocate target machine!\n";

        // Build up all of the passes that we want to do to the module.
        PassManager pass_manager;

        // Add an appropriate TargetLibraryInfo pass for the module's triple.
        pass_manager.add(new TargetLibraryInfo(Triple(module->getTargetTriple())));

#if LLVM_VERSION < 33
        pass_manager.add(new TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                                 target_machine->getVectorTargetTransformInfo()));
#else
        target_machine->addAnalysisPasses(pass_manager);
#endif

#if LLVM_VERSION < 35
        DataLayout *layout = new DataLayout(module);
        debug(2) << "Data layout: " << layout->getStringRepresentation();
        pass_manager.add(layout);
#endif

        // Make sure things marked as always-inline get inlined
        pass_manager.add(createAlwaysInlinerPass());

        // Override default to generate verbose assembly.
        target_machine->setAsmVerbosityDefault(true);

        raw_fd_ostream *raw_out = new_raw_fd_ostream(filename);
        formatted_raw_ostream *out = new formatted_raw_ostream(*raw_out);

        // Ask the target to add backend passes as necessary.
        target_machine->addPassesToEmitFile(pass_manager, *out, file_type);

        pass_manager.run(*module);

        delete out;
        delete raw_out;

        delete target_machine;
    }


    void generate(const LoweredFunc &func) {
        CodeGen_LLVM *cg = CodeGen_LLVM::new_for_target(func.target);
        cg->compile(func.body, func.name, func.args, func.images);

        if (!bitcode_filename.empty()) {
            raw_fd_ostream *out = new_raw_fd_ostream(bitcode_filename);
            WriteBitcodeToFile(cg->get_module(), *out);
            delete out;
        }
        if (!llvm_assembly_filename.empty()) {
            raw_fd_ostream *out = new_raw_fd_ostream(llvm_assembly_filename);
            cg->get_module()->print(*out, NULL);
            delete out;
        }
        if (!object_filename.empty()) {
            generate(cg, object_filename, TargetMachine::CGFT_ObjectFile);
        }
        if (!assembly_filename.empty()) {
            generate(cg, assembly_filename, TargetMachine::CGFT_AssemblyFile);
        }

        delete cg;
    }

private:
    std::string bitcode_filename;
    std::string llvm_assembly_filename;
    std::string object_filename;
    std::string assembly_filename;
};

class StmtHtmlOutput : public OutputBase {
public:
    StmtHtmlOutput(const std::string &filename) : filename(filename) {}

    void generate(const LoweredFunc &func) {
        print_to_html(filename, func.body);
    }

private:
    std::string filename;
};

class StmtTextOutput : public OutputBase {
public:
    StmtTextOutput(const std::string &filename) : filename(filename) {}

    void generate(const LoweredFunc &func) {
        ofstream stmt_output(filename.c_str());
        stmt_output << func.body;
    }

private:
    std::string filename;
};

class COutput : public OutputBase {
public:
    COutput(const std::string &h_filename,
            const std::string &c_filename)
        : h_filename(h_filename), c_filename(c_filename) {}

    void generate(const LoweredFunc &func) {
        if (!h_filename.empty()) {
            ofstream header(h_filename.c_str());
            CodeGen_C cg(header);
            cg.compile_header(func.name, func.args);
        }
        if (!c_filename.empty()) {
            ofstream src(c_filename.c_str());
            CodeGen_C cg(src);
            cg.compile(func.body, func.name, func.args, func.images);
        }
    }

private:
    std::string h_filename;
    std::string c_filename;
};



}  // namespace Internal

Output Output::bitcode(const std::string &filename) {
    return Output(new Internal::LLVMOutput(filename, "", "", ""));
}

Output Output::llvm_assembly(const std::string &filename) {
    return Output(new Internal::LLVMOutput("", filename, "", ""));
}

Output Output::llvm(const std::string &bitcode_filename,
                    const std::string &llvm_assembly_filename) {
    return Output(new Internal::LLVMOutput(bitcode_filename, llvm_assembly_filename, "", ""));
}

Output Output::object(const std::string &filename) {
    return Output(new Internal::LLVMOutput("", "", filename, ""));
}

Output Output::assembly(const std::string &filename) {
    return Output(new Internal::LLVMOutput("", "", "", filename));
}

Output Output::native(const std::string &object_filename,
                      const std::string &assembly_filename) {
    return Output(new Internal::LLVMOutput("", "", object_filename, assembly_filename));
}

Output Output::stmt_html(const std::string &filename) {
    return Output(new Internal::StmtHtmlOutput(filename));
}

Output Output::stmt_text(const std::string &filename) {
    return Output(new Internal::StmtTextOutput(filename));
}

Output Output::c_header(const std::string &filename) {
    return Output(new Internal::COutput(filename, ""));
}

Output Output::c_source(const std::string &filename) {
    return Output(new Internal::COutput(filename, ""));
}

Output Output::c(const std::string &h_filename,
                 const std::string &c_filename) {
    return Output(new Internal::COutput(h_filename, c_filename));
}

}  // namespace Halide
