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
        CPlusPlusSource,
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

    /** Construct an ExternalCode container from llvm bitcode. The
     * result can be passed to Halide::Module::append to have the
     * contained bitcode linked with that module. The Module's target
     * must match the target argument here on architecture, bit width,
     * and operating system. The name is used as a unique identifier
     * for the external code and duplicates will be reduced to a
     * single instance. Halide does not do anything other than to
     * compare names for equality. To guarantee uniqueness in public
     * code, we suggest using a Java style inverted domain name
     * followed by organization specific naming. E.g.:
     *     com.initech.y2k.5d2ac80aaf522eec6cb4b40f39fb923f9902bc7e */
    static ExternalCode bitcode_wrapper(const Target &cpu_type, const std::vector<uint8_t> &code, const std::string &name) {
        return ExternalCode(LLVMBitcode, cpu_type, DeviceAPI::None, code, name);
    }

    /** Construct an ExternalCode container from GPU "source code."
     * This container can be used to insert its code into the GPU code
     * generated for a given DeviceAPI. The specific type of code
     * depends on the device API used as follows:
     *     CUDA: llvm bitcode for PTX
     *     OpenCL: OpenCL source code
     *     GLSL: GLSL source code
     *     OpenGLCompute: GLSL source code
     *     Metal: Metal source code
     *     Hexagon: llvm bitcode for Hexagon
     *
     * At present, this API is not fully working. See Issue:
     *     https://github.com/halide/Halide/issues/1971
     *
     * The name is used as a unique identifier for the external code
     * and duplicates will be reduced to a single instance. Halide
     * does not do anything other than to compare names for
     * equality. To guarantee uniqueness in public code, we suggest
     * using a Java style inverted domain name followed by
     * organization specific naming. E.g.:
     *     com.tyrell.nexus-6.53947db86ba97a9ca5ecd5e60052880945bfeb37 */
    static ExternalCode device_code_wrapper(DeviceAPI device_api, const std::vector<uint8_t> &code, const std::string &name) {
        return ExternalCode(DeviceCode, Target(), device_api, code, name);
    }

    /** Construct an ExternalCode container from C++ source code. This
     * container can be used to insert its code into C++ output from
     * Halide.
     *
     * At present, this API is not fully working. See Issue:
     *     https://github.com/halide/Halide/issues/1971
     *
     * The name is used as a unique identifier for the external code
     * and duplicates will be reduced to a single instance. Halide
     * does not do anything other than to compare names for
     * equality. To guarantee uniqueness in public code, we suggest
     * using a Java style inverted domain name followed by
     * organization specific naming. E.g.:
     *     com.cyberdyne.skynet.78ad6c411d313f050f172cd3d440f23fdd797d0d */
    static ExternalCode c_plus_plus_code_wrapper(const std::vector<uint8_t> &code, const std::string &name) {
        return ExternalCode(CPlusPlusSource, Target(), DeviceAPI::None, code, name);
    }

    /** Return true if this container holds llvm bitcode linkable with
     * code generated for the target argument. The matching is done
     * on the architecture, bit width, and operating system
     * only. Features are ignored. If the container is for
     * Target::ArchUnkonwn, it applies to all architectures -- meaning
     * it is generic llvm bitcode. If the OS is OSUnknown, it applies
     * to all operationg systems. The bit width must match.
     *
     * Ignoring feature flags isn't too important since generally
     * ExternalCode will be constructed in a Generator which has
     * access to the feature flags in effect and can select code
     * appropriately. */
    bool is_for_cpu_target(const Target &host) const {
        return kind == LLVMBitcode &&
            (llvm_target.arch == Target::ArchUnknown || llvm_target.arch == host.arch) &&
            (llvm_target.os == Target::OSUnknown || llvm_target.os == host.os) &&
            (llvm_target.bits == host.bits);
    }

    /** True if this container holds code linkable with a code generated for a GPU. */
    bool is_for_device_api(DeviceAPI current_device) const {
        return kind == DeviceCode && device_code_kind == current_device;
    }

    /** True if this container holds C++ source code for inclusion in
     *  generating C++ output. */
    bool is_c_plus_plus_source() const { return kind == CPlusPlusSource; }

    /** Retrieve the bytes of external code held by this container. */
    const std::vector<uint8_t> &contents() const { return code; }

    /** Retrieve the name of this container. Used to ensure the same
     *  piece of external code is only included once in linkage. */
    const std::string &name() const { return nametag; }
};

}

#endif

