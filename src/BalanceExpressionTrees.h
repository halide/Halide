#ifndef BALANCE_EXRESSION_TREES_H
#define BALANCE_EXRESSION_TREES_H
/** \file
 * Balance expression trees using Huffman Tree Height Reduction.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/* We can generate better code if imbalanced tress of associative and commutative 
   operations are balanced.
   For example, the expression tree for a + 4*b + 6*c + 4*d + e is

                        +
                      /   \
                    /      e
                  +
                /   \
              /       \
            +          *
          /   \       /  \
        /       \    d    4
      +          *
    /   \      /   \
  a      *    c     6
       /   \
      b     4

  This pass converts this tree to
                 +
               /   \
             /       \  
           /           \
         +               +
       /   \           /   \
     *      *         *      +
   /   \   / \      /   \   /  \
  b     4 c   6    d     4 a    e

  On targets such as HVX, the balanced tree above leads to very good code generation by the use
  of multiply-accumulate instructions.
*/

/** balance expression trees */
EXPORT Stmt balance_expression_trees(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
