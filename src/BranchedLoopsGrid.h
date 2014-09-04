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

template<class T>
class BranchedLoopsGrid {
public:
  BranchedLoopsGrid() : data(1) {}

  size_t dim() const {return dims.size();}
  size_t size(const int dim) const {return points[dim].size() - 1;}
  
  void push_dim(const std::string& name, Expr min, Expr max);
  void split(const std::string& name, const int i, Expr x);

  const std::string& var(const int dim) const {return dims[dim];}
  const std::vector<Expr>& coords(const int dim) const {return points[dim];}

  T& operator() (const int *idx) {return data[this->data_offset(idx)];}
  const T& operator() (const int *idx) const {return data[this->data_offset(idx)];}
private:
  size_t data_offset(const int *idx);

  void split_data(const int split_dim, const int split_idx,
                  std::vector<T>& new_data,
                  const int dim, const int offset,
                  const int slice);

  std::vector<std::string> dims;
  std::vector<std::vector<Expr> > points;
  std::vector<T> data;
};

template<class T>
void BranchedLoopsGrid<T>::push_dim(const std::string& name, Expr min, Expr max) {
  std::vector<Expr> pts(2);
  pts[0] = min;
  pts[1] = max;
  
  dims.push_back(name);
  points.push_back(pts);
}

template<class T>
size_t BranchedLoopsGrid<T>::data_offset(const int *idx) {
  int stride = 1;
  size_t offset = 0;
  for (int i = dims()-1; i >= 0; --i) {
    offset += idx[i] * stride;
    stride *= size(i);
  }
  return offset;
}

template<class T>
void BranchedLoopsGrid<T>::split_data(const int split_dim, const int split_idx,
                                      std::vector<T>& new_data,
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

template<class T>
void BranchedLoopsGrid<T>::split(const std::string& name, const int split_idx, Expr x) {
  int slice_size = 1;
  int dim;
  for (int k = 0; k < dims.size(); ++k) {
    if (dims[k] == name) {
      dim = k;
    } else {
      slice_size *= dims[k].size();
    }
  }
  
  internal_assert(dim < dims.size()) << "Couldn't find dimension " << name << " in BranchedLoopGrid.\n";

  std::vector<T> new_data(data.size() + slice_size);
  split_data(dim, split_idx, new_data, 0, 0, data.size());
  std::swap(data, new_data);

  points[dim].insert(points[dim].begin() + split_idx + 1, x);
}

}
}

#endif
 
