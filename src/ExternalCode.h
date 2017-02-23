#ifndef HALIDE_EXTERNAL_CODE_H
#define HALIDE_EXTERNAL_CODE_H

#include <vector>

#include "Expr.h"
#include "Target.h"

namespace Halide {

class ExternalCode {
private:
    enum Kind {
        LLVMBitcode,
        DeviceCode,
        CXXSource,
    } kind;

    Target llvm_target; // For LLVMBitcode.
    DeviceAPI device_code_kind;

    std::vector<uint8_t> code;

    // Used for debugging and naming the module to llvm.
    std::string nametag;
    
    ExternalCode(Kind kind, const Target &llvm_target, DeviceAPI device_api, const std::vector<uint8_t> &code, const std::string &name)
        : kind(kind), llvm_target(llvm_target), device_code_kind(device_api), code(code), nametag(name) {
    }

public:

    // TODO: doc strings
    // TODO: Should pass code as R-value reference?
    static ExternalCode bitcode_wrapper(const Target &cpu_type, const std::vector<uint8_t> &code, const std::string &name) {
        return ExternalCode(LLVMBitcode, cpu_type, DeviceAPI::None, code, name);
    }

    static ExternalCode device_code_wrapper(DeviceAPI device_api, const std::vector<uint8_t> &code, const std::string &name) {
        return ExternalCode(DeviceCode, Target(), device_api, code, name);
    }

    static ExternalCode cxx_code_wrapper(const std::vector<uint8_t> &code, const std::string &name) {
        return ExternalCode(CXXSource, Target(), DeviceAPI::None, code, name);
    }

    bool is_for_cpu_target(const Target &host) const {
      // ArchUnknown can be used to insert generitc llvm bitcode.
      // Not sure how to match feature flags, but it isn't too important
      // since generaly ExternalCode will be constructed in a Generator which
      // has access to the feature flags in effect and can select code qppropriately.
      return kind == LLVMBitcode &&
        (llvm_target.arch == Target::ArchUnknown || llvm_target.arch == host.arch) &&
        (llvm_target.os == Target::OSUnknown || llvm_target.os == host.os) &&
        (llvm_target.bits == host.bits);
    }
    bool is_for_device_api(DeviceAPI current_device) const {
        return kind == DeviceCode && device_code_kind == current_device;
    }
    bool is_cxx_source() const { return kind == CXXSource; }

    const std::vector<uint8_t> &contents() const { return code; }

    const std::string &name() const { return nametag; }
};

}

#endif

