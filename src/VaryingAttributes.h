#ifndef __HALIDE_EXPRESSION_GRID__H
#define __HALIDE_EXPRESSION_GRID__H

/** \file
 * Defines an irregular grid class that stores a grid of nodes in a specified
 * number of dimensions where the coordinate dimensions that delimit nodes are
 * given by Exprs.
 */

#include "IR.h"

#include <vector>
#include <initializer_list>

namespace Halide {
namespace Internal {

    /** find_linear_expressions(Stmt s) identifies expressions that may be moved
     * out of the generated fragment shader into a varying attribute
     */
    Stmt find_linear_expressions(Stmt s);

    struct ExpressionMesh {

        // Unsorted coordinate expressions along each spatial dimension
        std::vector<std::vector<Expr>> coords;

        // Attribute names, including the x and y coordinates
        std::vector<std::string> attributes;
        
        // The dimension along which each attribute is defined
        std::vector<int>         attribute_dims;        
    };
    
    void compute_mesh(const For* op, ExpressionMesh& result);
}
}

#endif
