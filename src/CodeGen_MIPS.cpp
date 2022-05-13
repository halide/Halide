#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

using std::string;

#if defined(WITH_MIPS)

namespace {

/** A code generator that emits mips code from a given Halide stmt. */
class CodeGen_MIPS : public CodeGen_Posix {
public:
    /** Create a mips code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_MIPS(const Target &);

protected:
    using CodeGen_Posix::visit;

    string mcpu_target() const override;
    string mcpu_tune() const override;
    string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;
};

CodeGen_MIPS::CodeGen_MIPS(const Target &t)
    : CodeGen_Posix(t) {
}

string CodeGen_MIPS::mcpu_target() const {
    if (target.bits == 32) {
        return "";
    } else {
        return "";
    }
}

string CodeGen_MIPS::mcpu_tune() const {
    return mcpu_target();
}

string CodeGen_MIPS::mattrs() const {
    if (target.bits == 32) {
        return "";
    } else {
        return "mips64r6";
    }
}

bool CodeGen_MIPS::use_soft_float_abi() const {
    return false;
}

int CodeGen_MIPS::native_vector_bits() const {
    return 128;
}

}  // namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_MIPS(const Target &target) {
    return std::make_unique<CodeGen_MIPS>(target);
}

#else  // WITH_MIPS

std::unique_ptr<CodeGen_Posix> new_CodeGen_MIPS(const Target &target) {
    user_error << "MIPS not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_MIPS

}  // namespace Internal
}  // namespace Halide
