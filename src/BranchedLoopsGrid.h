#ifndef HALIDE_BRANCHED_LOOPS_GRID_H
#define HALIDE_BRANCHED_LOOPS_GRID_H

#include <vector>
#include <list>
#include <string>

/** \file
 * Defines a data structure that stores an irregular grid representing
 * the branches of a group of nested loops.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/* A class that stores an irregular grid of Stmt's, which represent
 * the branches of aset of nested loops.
 */
class BranchedLoopsGrid {
public:
  BranchedLoopsGrid() : data(1) {}

  /* Returns the number of dimensions in the grid, which is equivalent
   * to the number of nested loops we are representing.
   */
  size_t dims() const {return vars.size();}

  /* Returns the total number of cells in the grid.
   */
  size_t num_cells() const {return data.size();}

  /* Returns the number of grid nodes in a particular dimension.
   */
  size_t size(const int dim) const {return points[dim].size() - 1;}

  /* Return the index of the named variable.
   */
  int dim(const std::string& var) const;

  /* Return the name of the variable in the given dimension.
   */
  const std::string& var(const int dim) const {return vars[dim];}

  /* Return the vector of coordinates in the given dimension.
   */
  const std::vector<Expr>& coords(const int dim) const {return points[dim];}

  /* Returns an Expr for the min coord of a particular cell and in a
   * particular dimenion.
   */
  Expr min(const int dim, const int cell) const {return points[dim][cell];}

  /* Returns an Expr for the extent of a particular cell in a
   * particular dimenion.
   */
  Expr extent(const int dim, const int cell) const {return points[dim][cell+1];}

  /* Returns an Expr for the max coord of a particular cell and in a
   * particular dimenion.
   */
  Expr max(const int dim, const int cell) const {return min(dim, cell) + extent(dim, cell) - 1;}

  /* Add a new dimension to the grid, with a given min and
   * extent. This becomes the innermost dimension.
   */
  void push_dim(const std::string& var, Expr min, Expr extent);

  /* Split the grid along the dimension given by the named
   * var. Splitting occurs in the given cell at the coordinate
   * specified by the provided Expr.
   */
  void split(const std::string& var, const int cell, Expr x);

  /* Split the grid along the given dimension in the cell at the
   * coordinate specified by the provided Expr.
   */
  void split(const int dim, const int cell, Expr x);

  /* Return the Stmt stored in the specified grid cell.
   */
  Stmt& operator() (const int *idx) {return data[this->data_offset(idx)];}

  /* Return the Stmt stored in the specified grid cell.
   */
  const Stmt& operator() (const int *idx) const {return data[this->data_offset(idx)];}
private:
  size_t data_offset(const int *idx);

  void split_data(const int split_dim, const int split_idx,
                  std::vector<Stmt>& new_data,
                  const int dim, const int offset,
                  const int slice);

  std::vector<std::string> vars;
  std::vector<std::vector<Expr> > points;
  std::vector<Stmt> data;
};

inline int dim(const std::string& var) const {
  int d;
  for (int k = 0; k < vars.size(); ++k) {
    if (vars[k] == var) {
      d = k;
    }
  }

  internal_assert(d < vars.size()) << "Couldn't find dimension " << var << " in BranchedLoopGrid.\n";

  return d;
}

inline void BranchedLoopsGrid::push_dim(const std::string& name, Expr min, Expr extent) {
  std::vector<Expr> pts(2);
  pts[0] = min;
  pts[1] = min + extent;

  vars.push_back(name);
  points.push_back(pts);
}

inline size_t BranchedLoopsGrid::data_offset(const int *idx) {
  int stride = 1;
  size_t offset = 0;
  for (int i = vars()-1; i >= 0; --i) {
    offset += idx[i] * stride;
    stride *= size(i);
  }
  return offset;
}

inline void BranchedLoopsGrid::split_data(const int split_dim, const int split_idx,
                                   std::vector<Stmt>& new_data,
                                   const int dim, const int offset,
                                   const int slice) {
  const int stride = slice / size(dim);

  if (dim < split_dim) {
    for (int i = 0; i < size(dim); ++i) {
      const int new_offset = offset + i * stride;

      split_data(split_dim, split_idx, new_data,
                 dim+1, new_offset, stride);
    }
  } else {
    const int end1 = offset + split_idx * stride;
    std::copy(data.begin() + offset, data.begin() + end1, new_data.begin() + offset);
    std::copy(data.begin() + end1 - stride, data.begin() + end1, new_data.begin() + end1);
    std::copy(data.begin() + end1, data.end(), new_data.begin() + end1 + stride);
  }
}

inline void BranchedLoopsGrid::split(const std::string& var, const int cell, Expr x) {
  split(dim(var), cell, x);
}

inline void BranchedLoopsGrid::split(const int dim, const int cell, Expr x) {
  int slice_size = 1;
  for (int k = 0; k < vars.size(); ++k) {
    if (vars[k] != name) {
      slice_size *= vars[k].size();
    }
  }

  std::vector<Stmt> new_data(data.size() + slice_size);
  split_data(dim, cell, new_data, 0, 0, data.size());
  std::swap(data, new_data);

  points[dim].insert(points[dim].begin() + cell + 1, x);
}

}
}

#endif
