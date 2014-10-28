#include <iostream>
#include <sstream>

#include "CodeGen_Hexagon.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Debug.h"
#include "Util.h"
#include "Simplify.h"
#include "IntegerDivisionTable.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"

// Native client llvm relies on global flags to control sandboxing on
// arm, because they expect you to be coming from the command line.
#ifdef WITH_NATIVE_CLIENT
#if LLVM_VERSION < 34
#include <llvm/Support/CommandLine.h>
namespace llvm {
extern cl::opt<bool> FlagSfiData,
    FlagSfiLoad,
    FlagSfiStore,
    FlagSfiStack,
    FlagSfiBranch,
    FlagSfiDisableCP,
    FlagSfiZeroMask;
}
extern llvm::cl::opt<bool> ReserveR9;
#endif
#endif

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;
using std::pair;
using std::make_pair;

using namespace llvm;

CodeGen_Hexagon::CodeGen_Hexagon(Target t) : CodeGen_Posix(t) {

  user_warning << "Hexagon not fully setup yet\n";
}

void CodeGen_Hexagon::compile(Stmt stmt, string name,
                          const vector<Argument> &args,
                          const vector<Buffer> &images_to_embed) {

    init_module();

    module = get_initial_module_for_target(target, context);

    // Fix the target triple.
    user_warning << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    llvm::Triple triple = get_target_triple();
    module->setTargetTriple(triple.str());

    user_warning << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args, images_to_embed);

    // Optimize
    CodeGen::optimize_module();
}

llvm::Triple CodeGen_Hexagon::get_target_triple() const {
    llvm::Triple triple;
    triple.setVendor(llvm::Triple::UnknownVendor);
    triple.setArch(llvm::Triple::hexagon);
    triple.setObjectFormat(llvm::Triple::ELF);
    return triple;
}

string CodeGen_Hexagon::mcpu() const {
  return "hexagonv60";
}

string CodeGen_Hexagon::mattrs() const {
  return "+hvx";
}

bool CodeGen_Hexagon::use_soft_float_abi() const {
  return false;
}
void CodeGen_Hexagon::visit(const Load *op) {
  const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
  if (!ramp) {
    debug(1) << "Dealing with a ramp\n";
    CodeGen::visit(op);
  }

  CodeGen::visit(op);
  return;
}

void CodeGen_Hexagon::visit(const Store *op) {
      const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (ramp) {
      debug(1) << "Dealing with a ramp " << op << "\n";
    }
    CodeGen::visit(op);
    return;
}
static bool canUseVadd(const Add *op) {
  const Ramp *RampA = op->a.as<Ramp>();
  const Ramp *RampB = op->b.as<Ramp>();
  if (RampA && RampB)
    return true;
  if (!RampA && RampB) {
    const Broadcast *BroadcastA = op->a.as<Broadcast>();
    return BroadcastA != NULL;
  } else  if (RampA && !RampB) {
    const Broadcast *BroadcastB = op->b.as<Broadcast>();
    return BroadcastB != NULL;
  } else {
    const Broadcast *BroadcastA = op->a.as<Broadcast>();
    const Broadcast *BroadcastB = op->b.as<Broadcast>();
    if (BroadcastA && BroadcastB)
      return true;
  }
  return false;
}

void CodeGen_Hexagon::visit(const Add *op) {
  if (canUseVadd(op)) {
    Intrinsic::ID ID = Intrinsic::hexagon_V6_vaddw;
    llvm::Function *F = Intrinsic::getDeclaration(module, ID);
    Value *Op1 = codegen(op->a);
    Value *Op2 = codegen(op->b);
    value = builder->CreateCall2(F, Op1, Op2);
  }
  else
    CodeGen::visit(op);
  return;
}

  void CodeGen_Hexagon::visit(const Broadcast *op) {
    int Width = op->width;
    if (Width == 16) {
      Intrinsic::ID ID = Intrinsic::hexagon_V6_lvsplatw;
      llvm::Function *F = Intrinsic::getDeclaration(module, ID);
      Value *Op1 = codegen(op->value);
      value = builder->CreateCall(F, Op1);

    }
  }
}}

