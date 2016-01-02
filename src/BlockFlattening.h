#ifndef BLOCKFLATTENING_H
#define BLOCKFLATTENING_H

/** \file
 *
 * Defines an IR mutator that flattening all blocks of blocks into a single block.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

Stmt flatten_blocks(Stmt s);

}
}

#endif
