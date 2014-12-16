#include "CodeGen_LLVM.h"
#include "LLVM_Headers.h"
#include "Output.h"

#include <iostream>

using namespace std;
using namespace llvm;

namespace Halide {
namespace Internal {

class NativeOutput : public OutputBase {
public:
    NativeOutput(const std::string& filename, bool assembly = false)
        : filename(filename), assembly(assembly) {}

    void generate(const LoweredFunc &func) {
        CodeGen_LLVM *cg = CodeGen_LLVM::new_for_target(func.target);
        cg->compile(func.body, func.name, func.args, func.images);

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

        // Figure out where we are going to send the output.
#if LLVM_VERSION < 35
        raw_fd_ostream raw_out(filename.c_str(), error_string);
#elif LLVM_VERSION == 35
        raw_fd_ostream raw_out(filename.c_str(), error_string, llvm::sys::fs::F_None);
#else // llvm 3.6
        std::error_code err;
        raw_fd_ostream raw_out(filename.c_str(), err, llvm::sys::fs::F_None);
        if (err) error_string = err.message();
#endif
        internal_assert(error_string.empty())
            << "Error opening output " << filename << ": " << error_string << "\n";

        formatted_raw_ostream out(raw_out);

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

        // Ask the target to add backend passes as necessary.
        TargetMachine::CodeGenFileType file_type =
            assembly ? TargetMachine::CGFT_AssemblyFile :
            TargetMachine::CGFT_ObjectFile;
        target_machine->addPassesToEmitFile(pass_manager, out, file_type);

        pass_manager.run(*module);

        delete target_machine;
        delete cg;
    }

private:
    std::string filename;
    bool assembly;
};

class BitcodeOutput : public OutputBase {
public:
    BitcodeOutput(const std::string &filename, bool assembly)
        : filename(filename), assembly(assembly) {}

    void generate(const LoweredFunc &func) {
        CodeGen_LLVM *cg = CodeGen_LLVM::new_for_target(func.target);
        cg->compile(func.body, func.name, func.args, func.images);

        llvm::Module *module = cg->get_module();

        string error_string;
#if LLVM_VERSION < 35
        raw_fd_ostream out(filename.c_str(), error_string);
#elif LLVM_VERSION == 35
        raw_fd_ostream out(filename.c_str(), error_string, llvm::sys::fs::F_None);
#else // llvm 3.6
        std::error_code err;
        raw_fd_ostream out(filename.c_str(), err, llvm::sys::fs::F_None);
        if (err) error_string = err.message();
#endif
        internal_assert(error_string.empty())
            << "Error opening output " << filename << ": " << error_string << "\n";

        if (assembly) {
            module->print(out, NULL);
        } else {
            WriteBitcodeToFile(module, out);
        }

        delete cg;
    }

private:
    std::string filename;
    bool assembly;
};

}  // namespace Internal

Output Output::bitcode(const std::string &filename) {
    return Output(new Internal::BitcodeOutput(filename, false));
}

Output Output::object(const std::string &filename) {
    return Output(new Internal::NativeOutput(filename, false));
}

Output Output::assembly(const std::string &filename) {
    return Output(new Internal::NativeOutput(filename, true));
}

Output Output::llvm_assembly(const std::string &filename) {
    return Output(new Internal::BitcodeOutput(filename, true));
}

}  // namespace Halide
