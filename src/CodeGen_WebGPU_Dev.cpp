#include <sstream>
#include <utility>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_WebGPU_Dev.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

namespace {

class CodeGen_WebGPU_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_WebGPU_Dev(const Target &target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const string &name,
                    const vector<DeviceArgument> &args) override;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module() override;

    vector<char> compile_to_src() override;

    string get_current_kernel_name() override;

    void dump() override;

    string print_gpu_name(const string &name) override;

    string api_unique_name() override {
        return "webgpu";
    }

protected:
    class CodeGen_WGSL : public CodeGen_C {
    public:
        CodeGen_WGSL(std::ostream &s, Target t)
            : CodeGen_C(s, t) {
        }
        void add_kernel(Stmt stmt,
                        const string &name,
                        const vector<DeviceArgument> &args);

    protected:
    };

    std::ostringstream src_stream;
    string cur_kernel_name;
    CodeGen_WGSL wgsl;
};

CodeGen_WebGPU_Dev::CodeGen_WebGPU_Dev(const Target &t)
    : wgsl(src_stream, t) {
}

void CodeGen_WebGPU_Dev::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_WebGPU_Dev::add_kernel " << name << "\n";
    debug(2) << "CodeGen_WebGPU_Dev:\n"
             << s;

    cur_kernel_name = name;
    wgsl.add_kernel(s, name, args);
}

void CodeGen_WebGPU_Dev::init_module() {
    debug(2) << "WebGPU device codegen init_module\n";

    // Wipe the internal shader source.
    src_stream.str("");
    src_stream.clear();
}

vector<char> CodeGen_WebGPU_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "WGSL shader:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_WebGPU_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_WebGPU_Dev::dump() {
    std::cerr << src_stream.str() << "\n";
}

string CodeGen_WebGPU_Dev::print_gpu_name(const string &name) {
    return name;
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::add_kernel(Stmt s,
                                                  const string &name,
                                                  const vector<DeviceArgument> &args) {
    debug(2) << "Adding WGSL shader " << name << "\n";

    // Emit the function prototype.
    // TODO: Emit the correct workgroup size.
    stream << "[[stage(compute), workgroup_size(1, 1, 1)]]\n";
    stream << "fn " << name << "()\n";

    open_scope();

    // TODO: Generate function body.

    close_scope("shader " + name);
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_WebGPU_Dev(const Target &target) {
    return std::make_unique<CodeGen_WebGPU_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide
