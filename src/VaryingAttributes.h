#ifndef __HALIDE_EXPRESSION_GRID__H
#define __HALIDE_EXPRESSION_GRID__H

/** \file
 * This file contains functions that detect expressions in a GLSL scheduled 
 * function that may be evaluated per vertex and interpolated across the domain
 * instead of being evaluated at each pixel location across the image.
 */

#include "IR.h"

#include <vector>

namespace Halide {
namespace Internal {

    /** find_linear_expressions(Stmt s) identifies expressions that may be moved
     * out of the generated fragment shader into a varying attribute. These 
     * expressions are tagged by wrapping them in a let expression with a 
     * variable whose name ends in ".varying"
     */
    Stmt find_linear_expressions(Stmt s);

    /** Compute a set of 2D mesh coordinates based on the behavior of varying
     * attribute expressions contained within a GLSL scheduled for loop. This 
     * method is called during host codegen to extract varying attribute 
     * expressions and generate code to evalue them at each mesh vertex location
     */
    struct ExpressionMesh {

        // Unsorted coordinate expressions along each spatial dimension
        std::vector<std::vector<Expr> > coords;

        // Attribute names, including the x and y coordinates
        std::vector<std::string> attributes;        
    };
    
    Stmt setup_mesh(const For* op, ExpressionMesh& result, std::map<std::string,Expr>& varyings);
}
}

#endif
