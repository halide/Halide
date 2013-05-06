/*! \file
  \verbatim
  
    Copyright (c) 2006, Sylvain Paris and Frédo Durand

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

  \endverbatim
*/

#ifndef __ARRAY_N__
#define __ARRAY_N__

#ifdef ARRAY_EXCEPTION
#  include <stdexcept>
#else
#  include <iostream>
#endif
  
#include <algorithm>
#include <vector>

#include "geom.h"

//! Class representing a n D array.
/*!
  Optimised for an access in order :
  \code
  for(x0=...){ for(x1=...){...} }
  \endcode
  
  at() and the operator[]() accept a vector that provides an
  access to its elements through an [] operator.
 */

template<unsigned int N,typename T,typename A = std::allocator<T> >
class Array_ND{

private:
  //! Type of the storage structure.
  typedef std::vector<T,A> storage_type;
  
public:

  //@{
  //! Standard type.

  typedef typename storage_type::value_type             value_type;
  typedef typename storage_type::allocator_type         allocator_type;
  
  typedef typename storage_type::size_type              size_type;
  typedef typename storage_type::difference_type        difference_type;

  typedef typename storage_type::iterator               iterator;
  typedef typename storage_type::const_iterator         const_iterator;

  typedef typename storage_type::reverse_iterator       reverse_iterator;
  typedef typename storage_type::const_reverse_iterator const_reverse_iterator;

  typedef typename storage_type::reference              reference;
  typedef typename storage_type::const_reference        const_reference;

  typedef Geometry::Vec<N,unsigned int> key_type;
  //@}

  //! Standard value
  static const size_type dimension = N;
  
  
  //@{
  //! Classical constructor.
  
  explicit inline Array_ND(const A& a = A());

  template<typename Vector_ND>
  explicit inline Array_ND(const Vector_ND& size_vector,
			   const T& val = T(),const A& a = A());
  
  template<typename Element_iterator,typename Vector_ND>
  inline Array_ND(Element_iterator begin_elt,
		  Element_iterator end_elt,
		  const Vector_ND& size_vector,
		  const A& a = A());
    
  inline Array_ND(const Array_ND<N,T,A>& a);
  //@}

  //! Assignement of a default value.
  template<typename Vector_ND>
  void assign(const Vector_ND& size_vector,
	      const T& val);

  //@{
  //! Handle the array dimension.

  inline bool empty() const;
  
  template<typename Vector_ND>
  inline void all_sizes(Vector_ND* const size) const;
  
  inline size_type dimension_size(const size_type dim) const;
  inline size_type size() const;

  inline size_type max_size() const;
  
  template<typename Vector_ND>
  inline void resize(const Vector_ND& size_vector);

#ifdef ARRAY_N_ENABLE_ORIGIN_SHIFT

  template<typename Vector_ND>
  inline void set_origin(const Vector_ND& size_vector);
  
  template<typename Vector_ND>
  inline void get_origin(Vector_ND* const o) const;
  
#endif
  
  //@}

  //! Efficient swapping of two 2D arrays.
  inline void swap(Array_ND<N,T,A>& a); /* A reference is used as in the STL. */

  //! Gives the memory allocator.
  inline allocator_type get_allocator() const; 
  
  //@{
  //! Classical operator.
  
  inline Array_ND<N,T,A>& operator=(const Array_ND<N,T,A>& a);
  inline bool operator==(const Array_ND<N,T,A>& a);
  inline bool operator!=(const Array_ND<N,T,A>& a); 
  //@}


  
  //@{
  //! Access operator.

  template<typename Vector_ND>
  inline reference       operator[](const Vector_ND& v);
  
  template<typename Vector_ND>
  inline const_reference operator[](const Vector_ND& v) const;

  template<typename Vector_ND>
  inline reference       at(const Vector_ND& v);
  
  template<typename Vector_ND>
  inline const_reference at(const Vector_ND& v) const;
  //@}
  
  //@{
  //! Points on the (0,0) element.
  /*!
    Goes through the array in the order
    \code
      for(x0=...){ for(x1=...){...} }.
    \endcode
   */
  
  inline iterator       begin();
  inline const_iterator begin() const;
  //@}

  //@{
  //! Points on the element after the last one.
  /*!
    Goes through the array in the order
    \code
      for(x0=...){ for(x1=...){...} }.
    \endcode
   */
  
  inline iterator       end();
  inline const_iterator end() const;
  //@}

  //@{
  //! Reverse iterator.

  inline reverse_iterator       rbegin();  
  inline const_reverse_iterator rbegin() const;
  inline reverse_iterator       rend();
  inline const_reverse_iterator rend() const;
  //@}

  //! Advance \p index by one step to achieve a scan order
  //! for(x0=start[0];x0<end[0];x0++){for(x1=...){}}
  //! Return \e false when all elements have been scanned.
  template<typename Vector_ND_1,typename Vector_ND_2,typename Vector_ND_3>
  inline bool advance(Vector_ND_1* const index,
		      const Vector_ND_2& start,
		      const Vector_ND_3& end) const;

  //! Advance \p index by one step to achieve a scan order
  //! for(x0=start[0];x0<end[0];x0++){for(x1=...){}}
  //! Set \p start and \p end in order to analyze the entire array.
  //! Return \e false when all elements have been scanned.
  template<typename Vector_ND>
  inline bool advance(Vector_ND* const index) const;

private:

  //! Dimension of the array.
  key_type dim_size;
  
#ifdef ARRAY_N_ENABLE_ORIGIN_SHIFT
  //! The first element is at \e origin.
  key_type origin;
#endif
  
  //! Storage structure.
  storage_type storage;

  //! Computation of the position in the storage structure.
  template<typename Vector_ND>
  inline size_type offset(const Vector_ND& v) const;

  
};







/*
  
  #############################################
  #############################################
  #############################################
  ######                                 ######
  ######   I M P L E M E N T A T I O N   ######
  ######                                 ######
  #############################################
  #############################################
  #############################################
  
*/




template<unsigned int N,typename T,typename A>
Array_ND<N,T,A>::Array_ND(const A& a)
  :dim_size(),storage(a){}


template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
Array_ND<N,T,A>::Array_ND(const Vector_ND& size_vector,
			  const T& val,const A& a):
  storage(a){

  size_type s = 1;
  
  for(size_type n=0;n<N;n++){

    const size_type a = size_vector[n];
    
    dim_size[n] = a;
    s *= a;
  }

  storage.assign(s,val);
}


/*!
  Fills in the array with the elements between \p begin_elt and
  \p \e end_elt.

  Throw the length_error() exception if not enough elements.
 */
template<unsigned int N,typename T,typename A>
template<typename Element_iterator,typename Vector_ND>
Array_ND<N,T,A>::Array_ND(Element_iterator begin_elt,
			  Element_iterator end_elt,
			  const Vector_ND& size_vector,
			  const A& a):
  storage(a){

  size_type s = 1;
  
  for(size_type n=0;n<N;n++){

    const size_type a = size_vector[n];
    
    dim_size[n] = a;
    s *= a;
  }

  storage.reserve(s);
  
  Element_iterator elt;
  size_type index;
  for(elt=begin_elt,index=0;(elt!=end_elt)&&(index<s);elt++,index++){
    storage.push_back(*elt);
  }

  if (index!=s) {
    storage.clear();
    storage.reserve(0);
#ifdef ARRAY_EXCEPTION
    throw std::length_error("Not enough elements to initialize the array");
#else
    std::cerr<<"[Array_ND<T,A>::Array_ND] Not enough elements to initialize the array"<<std::endl;
#endif
  }
}
    

template<unsigned int N,typename T,typename A>
Array_ND<N,T,A>::Array_ND(const Array_ND<N,T,A>& a):
  dim_size(a.dim_size),storage(a.storage){}


template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
void
Array_ND<N,T,A>::assign(const Vector_ND& size_vector,
			const T& val){

  size_type s = 1;
  
  for(size_type n=0;n<N;n++){

    const size_type a = size_vector[n];
    
    dim_size[n] = a;
    s *= a;
  }

  storage.assign(s,val);
}



template<unsigned int N,typename T,typename A>
bool
Array_ND<N,T,A>::empty() const{
  return storage.empty();
}



template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
void
Array_ND<N,T,A>::all_sizes(Vector_ND* const size) const{

  for(size_type n=0;n<N;n++){
    (*size)[n] = dim_size[n];
  }
}


template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::size_type
Array_ND<N,T,A>::dimension_size(const size_type dim) const{

  return dim_size[dim];
}



template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::size_type
Array_ND<N,T,A>::size() const{

  return storage.size();
}


template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::size_type
Array_ND<N,T,A>::max_size() const{
  
  return storage.max_size();
}


template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
void
Array_ND<N,T,A>::resize(const Vector_ND& size_vector){

  size_type s = 1;
  
  for(size_type n=0;n<N;n++){

    const size_type a = size_vector[n];
    
    dim_size[n] = a;
    s *= a;
  }

  storage.resize(s);
}


#ifdef ARRAY_N_ENABLE_ORIGIN_SHIFT

template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
void
Array_ND<N,T,A>::set_origin(const Vector_ND& o){
  origin = o;
}


template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
void
Array_ND<N,T,A>::get_origin(Vector_ND* const o) const{
  for(size_type n = 0;n < N;++n){
    (*o)[n] = origin[n];
  }
}

#endif


template<unsigned int N,typename T,typename A>
void 
Array_ND<N,T,A>::swap(Array_ND<N,T,A>& a){

  const key_type buf = dim_size;
  dim_size           = a.dim_size;
  a.dim_size         = buf;

//   swap(dim_size,a.dim_size);
  
  storage.swap(a.storage);  
}



template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::allocator_type
Array_ND<N,T,A>::get_allocator() const{
  
  return storage.get_allocator();
}



template<unsigned int N,typename T,typename A>
Array_ND<N,T,A>&
Array_ND<N,T,A>::operator=(const Array_ND<N,T,A>& a){

  if (&a!=this){
    dim_size = a.dim_size;
    storage  = a.storage;
  }

  return *this;
}



template<unsigned int N,typename T,typename A>
bool
Array_ND<N,T,A>::operator==(const Array_ND<N,T,A>& a){

  return ((dim_size == a.dim_size) && (storage == a.storage));
}


template<unsigned int N,typename T,typename A>
bool
Array_ND<N,T,A>::operator!=(const Array_ND<N,T,A>& a){

  return !(*this==a);
}


template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
typename Array_ND<N,T,A>::reference
Array_ND<N,T,A>::operator[](const Vector_ND& v){

  return storage[offset(v)];
}


  
template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
typename Array_ND<N,T,A>::const_reference
Array_ND<N,T,A>::operator[](const Vector_ND& v) const{

  return storage[offset(v)];
}





template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
typename Array_ND<N,T,A>::reference
Array_ND<N,T,A>::at(const Vector_ND& v){

  for(size_type n=0;n<N;n++){

    if (v[n] >= dim_size[n]){
#ifdef ARRAY_EXCEPTION
      throw std::out_of_range("Out of range");    
#else
      std::cerr<<"[Array_ND<T,A>::at] Out of range"<<std::endl;
      exit(1);
#endif
    }
  }

  return storage[offset(v)];
}




template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
typename Array_ND<N,T,A>::const_reference
Array_ND<N,T,A>::at(const Vector_ND& v) const{

  for(size_type n=0;n<N;n++){

    if (v[n] >= dim_size[n]){
#ifdef ARRAY_EXCEPTION
      throw std::out_of_range("Out of range");    
#else
      std::cerr<<"[Array_ND<T,A>::at] Out of range"<<std::endl;
      exit(1);
#endif
    }
  }

  return storage[offset(v)];
}




template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::iterator
Array_ND<N,T,A>::begin(){

  return storage.begin();
}




template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::const_iterator
Array_ND<N,T,A>::begin() const{

  return storage.begin();
}



template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::iterator
Array_ND<N,T,A>::end(){

  return storage.end();
}




template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::const_iterator
Array_ND<N,T,A>::end() const{

  return storage.end();
}




template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::reverse_iterator
Array_ND<N,T,A>::rbegin(){

  return storage.rbegin();
}




template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::const_reverse_iterator
Array_ND<N,T,A>::rbegin() const{

  return storage.rbegin();
}



template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::reverse_iterator
Array_ND<N,T,A>::rend(){

  return storage.rend();
}




template<unsigned int N,typename T,typename A>
typename Array_ND<N,T,A>::const_reverse_iterator
Array_ND<N,T,A>::rend() const{

  return storage.rend();
}




template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
typename Array_ND<N,T,A>::size_type
Array_ND<N,T,A>::offset(const Vector_ND& v) const{

  size_type res = v[0];
  
#ifdef ARRAY_N_ENABLE_ORIGIN_SHIFT
  res -= origin[0];
#endif
  
  for(size_type n=1;n<N;n++){

    res *= dim_size[n];
    res += v[n];
    
#ifdef ARRAY_N_ENABLE_ORIGIN_SHIFT
    res -= origin[n];
#endif
  }
  
  return res;
}


/*! There is no test to check that \p index is actually "after" \p start. */
template<unsigned int N,typename T,typename A>
template<typename Vector_ND_1,typename Vector_ND_2,typename Vector_ND_3>
bool
Array_ND<N,T,A>::advance(Vector_ND_1* const index,
			 const Vector_ND_2& start,
			 const Vector_ND_3& end) const{

  bool valid_increase = false;
  
  for(int n = N - 1;
      (!valid_increase) && (n>=0);
      --n){

    typename Vector_ND_1::value_type& i = (*index)[n];
    i++;

    valid_increase =
      (i < static_cast<typename Vector_ND_1::value_type>(end[n]));

    if (!valid_increase){
      i = start[n];
    }
  }

  return valid_increase;
}


template<unsigned int N,typename T,typename A>
template<typename Vector_ND>
bool
Array_ND<N,T,A>::advance(Vector_ND* const index) const{

#ifdef ARRAY_N_ENABLE_ORIGIN_SHIFT
  
  return advance(index,origin,origin+dim_size);

#else

  static const Vector_ND zero_start;
  return advance(index,zero_start,dim_size);
  
#endif
}


#endif
