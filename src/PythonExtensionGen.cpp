#include <iostream>
#include <string>

#include "Module.h"
#include "PythonExtensionGen.h"
#include "Util.h"

namespace Halide {
namespace Internal {

PythonExtensionGen::PythonExtensionGen(std::ostream &dest, const std::string &header_name, Target target)
    : dest(dest), header_name(header_name), target(target) {
}

void PythonExtensionGen::compile(const Module &module) {
    dest << "#include \"" << header_name << "\"\n\n";
    dest << "extern \"C\" {\n";
    dest << "}\n";
}

}}
