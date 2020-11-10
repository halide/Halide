#ifndef HALIDE_INJECT_DMA_TRANSFER_H
#define HALIDE_INJECT_DMA_TRANSFER_H

/** \file
 * Defines the lowering pass that injects Xtensa's DMA transfers.
 */
#include <map>
#include <string>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

Stmt inject_dma_transfer(Stmt s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
