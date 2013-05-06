/*! \file

\verbatim

Copyright (c) 2004, Sylvain Paris and Francois Sillion
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

    * Neither the name of ARTIS, GRAVIR-IMAG nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

\endverbatim


 *  This file contains code made by Sylvain Paris under supervision of
 * François Sillion for his PhD work with <a
 * href="http://www-artis.imag.fr">ARTIS project</a>. ARTIS is a
 * research project in the GRAVIR/IMAG laboratory, a joint unit of
 * CNRS, INPG, INRIA and UJF.
 *
 *  Use <a href="http://www.stack.nl/~dimitri/doxygen/">Doxygen</a>
 * with DISTRIBUTE_GROUP_DOC option to produce an nice html
 * documentation.
 *
 *  Most of STL conventions are used -- like std::vector. The main
 * difference is the use the () operator to access the elements. Since
 * the operator [] accepts only one argument, it is kept for the
 * access through a vector variable.
 */

#ifndef __ARRAY__
#define __ARRAY__

  
#ifdef ARRAY_EXCEPTION
#  include <stdexcept>
#else
#  include <iostream>
#endif
  
#include <vector>

#ifndef NO_XML
#include <sstream>
   
#include <qdom.h>
#endif

/*

  ##################
  # class Array_2D #
  ##################

 */



//! Class representing a 2D array.
/*!
  Optimised for an access in order :
  \code
  for(x=...){ for(y=...){...} }
  \endcode
  
  at() and the operator[]() also accept a vector that provides an
  access to its elements through an [] operator.
 */
template<typename T,typename A = std::allocator<T> >
class Array_2D{

private:
  //! Type of the storage structure.
  typedef std::vector<T,A> Storage;
  
public:

  //@{
  //! Standard type.

  typedef typename Storage::value_type             value_type;
  typedef typename Storage::allocator_type         allocator_type;
  
  typedef typename Storage::size_type              size_type;
  typedef typename Storage::difference_type        difference_type;

  typedef typename Storage::iterator               iterator;
  typedef typename Storage::const_iterator         const_iterator;

  typedef typename Storage::reverse_iterator       reverse_iterator;
  typedef typename Storage::const_reverse_iterator const_reverse_iterator;

  typedef typename Storage::reference              reference;
  typedef typename Storage::const_reference        const_reference;
  //@}
  
  //@{
  //! Classical constructor.
  
  explicit inline Array_2D(const A& a = A());
  
  explicit inline Array_2D(const size_type nx,
			   const size_type ny,
			   const T& val = T(),const A& a = A());
  
  template<typename Element_iterator>
  inline Array_2D(Element_iterator begin_elt,
		  Element_iterator end_elt,
		  const size_type nx,
		  const size_type ny,
		  const A& a = A());
    
  inline Array_2D(const Array_2D<T,A>& a);
  //@}

  //! Assignement of a default value.
  void assign(const size_type nx,
	      const size_type ny,
	      const T& val);
  
  //@{
  //! Handle the array dimension.

  inline bool empty() const;
  
  inline size_type x_size() const;
  inline size_type width() const;
  inline size_type y_size() const;
  inline size_type height() const;
  inline size_type size() const;

  inline size_type max_size() const;
  
  inline void resize(const size_type nx,
		     const size_type ny);
  //@}

  //! Efficient swapping of two 2D arrays.
  inline void swap(Array_2D<T,A>& a); /* A reference is used as in the STL. */

  //! Gives the memory allocator.
  inline allocator_type get_allocator() const; 
  
  //@{
  //! Classical operator.
  
  inline Array_2D<T,A>& operator=(const Array_2D<T,A>& a);
  inline bool operator==(const Array_2D<T,A>& a);
  inline bool operator!=(const Array_2D<T,A>& a); 
  //@}

  //@{
  //! Access operator.

  template<typename Vector_position>
  inline reference       operator[](const Vector_position& v);
  
  template<typename Vector_position>
  inline const_reference operator[](const Vector_position& v) const;

  inline reference       operator()(const size_type x,
				    const size_type y);
  
  inline const_reference operator()(const size_type x,
				    const size_type y) const;

  template<typename Vector_position>
  inline reference       at(const Vector_position& v);
  
  template<typename Vector_position>
  inline const_reference at(const Vector_position& v) const;

  inline reference       at(const size_type x,
			    const size_type y);
  
  inline const_reference at(const size_type x,
			    const size_type y) const;
  //@}

  //@{
  //! Points on the (0,0) element.
  /*!
    Goes through the array in the order
    \code
      for(x=...){ for(y=...){...} }.
    \endcode
   */
  
  inline iterator       begin();
  inline const_iterator begin() const;
  //@}

  //@{
  //! Points on the element after (x_size()-1,y_size()-1).
  /*!
    Goes through the array in the order
    \code
      for(x=...){ for(y=...){...} }.
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

#ifndef NO_XML
  inline QDomElement to_DOM_element(const QString& name,
				    QDomDocument& document) const;
  inline void from_DOM_element(const QDomElement& element);
#endif
  
private:
  //@{
  //! Dimension of the array.

  int x_dim,y_dim;
  //@}

  //! Storage structure.
  Storage storage;

  //@{
  //! Computation of the position in the storage structure.
  
  template<typename Vector_position>
  inline size_type offset(const Vector_position& v) const;
  
  inline size_type offset(const size_type& x,
			  const size_type& y) const;
  //@}
};









/*

  ##################
  # class Array_3D #
  ##################

 */









//! Class representing a 3D array.
/*!
  Optimised for an access in order :
  \code
  for(x=...){ for(y=...){for(z=...){...} } }
  \endcode
  
  at() and the operator[]() also accept a vector that provides an
  access to its elements through an [] operator.
 */
template<typename T,typename A = std::allocator<T> >
class Array_3D{

private:
  //! Type of the storage structure.
  typedef std::vector<T,A> Storage;
  
public:

  //@{
  //! Standard type.

  typedef typename Storage::value_type             value_type;
  typedef typename Storage::allocator_type         allocator_type;
  
  typedef typename Storage::size_type              size_type;
  typedef typename Storage::difference_type        difference_type;

  typedef typename Storage::iterator               iterator;
  typedef typename Storage::const_iterator         const_iterator;

  typedef typename Storage::reverse_iterator       reverse_iterator;
  typedef typename Storage::const_reverse_iterator const_reverse_iterator;

  typedef typename Storage::reference              reference;
  typedef typename Storage::const_reference        const_reference;
  //@}

  //@{
  //! Classical constructor.
  
  explicit inline Array_3D(const A& a = A());
  
  explicit inline Array_3D(const size_type nx,
			   const size_type ny,
			   const size_type nz,
			   const T& val = T(),
			   const A& a = A());

  template<typename Element_iterator>
  inline Array_3D(Element_iterator begin_elt,
		  Element_iterator end_elt,
		  const size_type nx,
		  const size_type ny,
		  const size_type nz,
		  const A& a = A());
  
  inline Array_3D(const Array_3D<T,A>& a);
  //@}

  //! Assignement of a default value.
  void assign(const size_type nx,
	      const size_type ny,
	      const size_type nz,
	      const T& val);
  
  //@{
  //! Handle the array dimension.

  inline bool empty() const;
  
  inline size_type x_size() const;
  inline size_type width() const;
  inline size_type y_size() const;
  inline size_type height() const;
  inline size_type z_size() const;
  inline size_type depth() const;
  inline size_type size() const;

  inline size_type max_size() const;
  
  inline void resize(const size_type nx,
		     const size_type ny,
		     const size_type nz);
  //@}

  //! Efficient swapping of two 2D arrays.
  inline void swap(Array_3D<T,A>& a); /* A reference is used as in the STL. */

  //! Renvoie l'allocateur.
  inline allocator_type get_allocator() const; 
  
  //@{
  //! Classical operator.
  
  inline Array_3D<T,A>& operator=(const Array_3D<T,A>& a);
  inline bool operator==(const Array_3D<T,A>& a);
  inline bool operator!=(const Array_3D<T,A>& a); 
  //@}

  //@{
  //! Access operator.

  template<typename Vector_position>
  inline reference       operator[](const Vector_position& v);
  
  template<typename Vector_position>
  inline const_reference operator[](const Vector_position& v) const;

  inline reference       operator()(const size_type x,
				    const size_type y,
				    const size_type z);
  
  inline const_reference operator()(const size_type x,
				    const size_type y,
				    const size_type z) const;

  template<typename Vector_position>
  inline reference       at(const Vector_position& v);
  
  template<typename Vector_position>
  inline const_reference at(const Vector_position& v) const;

  inline reference       at(const size_type x,
			    const size_type y,
			    const size_type z);
  
  inline const_reference at(const size_type x,
			    const size_type y,
			    const size_type z) const;
  //@}

  //@{
  //! Points on the (0,0,0) element.
  /*!
    Goes through the array in the order
    \code
      for(x=...){ for(y=...){ for(z=...){...} } }.
    \endcode
   */
  
  inline iterator       begin();
  inline const_iterator begin() const;
  //@}

  //@{
  //! Points on the element after (x_size()-1,y_size()-1,z_size()-1).
  /*!
    Goes through the array in the order
    \code
      for(x=...){ for(y=...){ for(z=...){...} } }.
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


private:
  //@{
  //! Dimension of the array.

  int x_dim,y_dim,z_dim;
  //@}

  //! Storage structure.
  Storage storage;

  //@{
  //! Computation of the position in the storage structure.

  template<typename Vector_position>
  inline size_type offset(const Vector_position& v) const;
  
  inline size_type offset(const size_type& x,
			  const size_type& y,
			  const size_type& z) const;
  //@}
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







/*

  ##################
  # class Array_2D #
  ##################

 */





template<typename T,typename A>
Array_2D<T,A>::Array_2D(const A& a)
  :x_dim(0),y_dim(0),storage(a){}


template<typename T,typename A>
Array_2D<T,A>::Array_2D(const size_type nx,
			const size_type ny,
			const T& val,
			const A& a)
  :x_dim(nx),y_dim(ny),storage(nx*ny,val,a){}


/*!
  Fills in the array with the elements between \p begin_elt and
  \p \e end_elt.

  Throw the length_error() exception if not enough elements.
 */
template<typename T,typename A>
template<typename Element_iterator>
Array_2D<T,A>::Array_2D(Element_iterator begin_elt,
			Element_iterator end_elt,
			const size_type nx,
			const size_type ny,
			const A& a)
  :x_dim(nx),y_dim(ny),storage(a){

  const size_type s = x_dim*y_dim;
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
    std::cerr<<"[Array_2D<T,A>::Array_2D] Not enough elements to initialize the array"<<std::endl;
    exit(1);
#endif
  }
}


template<typename T,typename A>
Array_2D<T,A>::Array_2D(const Array_2D<T,A>& a)
  :x_dim(a.x_dim),y_dim(a.y_dim),storage(a.storage){}


template<typename T,typename A>
void
Array_2D<T,A>::assign(const size_type nx,
		      const size_type ny,
		      const T& val){
  x_dim = nx;
  y_dim = ny;
  storage.assign(x_dim*y_dim,val);;
}


template<typename T,typename A>
bool
Array_2D<T,A>::empty() const{
  return storage.empty();
}


template<typename T,typename A>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::x_size() const{
  return x_dim;
}

template<typename T,typename A>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::width() const{
  return x_dim;
}


template<typename T,typename A>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::y_size() const{
  return y_dim;
}

template<typename T,typename A>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::height() const{
  return y_dim;
}


template<typename T,typename A>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::size() const{
  return storage.size();
}


template<typename T,typename A>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::max_size() const{
  return storage.max_size();
}


template<typename T,typename A> 
void
Array_2D<T,A>::resize(const size_type nx,
		      const size_type ny){
  x_dim = nx;
  y_dim = ny;
  storage.resize(x_dim * y_dim);
}


template<typename T,typename A> 
void
Array_2D<T,A>::swap(Array_2D<T,A>& a){
  size_type x_buf = x_dim;
  size_type y_buf = y_dim;
  
  x_dim = a.x_dim;
  y_dim = a.y_dim;

  a.x_dim = x_buf;
  a.y_dim = y_buf;

  storage.swap(a.storage);
}


template<typename T,typename A> 
typename Array_2D<T,A>::allocator_type
Array_2D<T,A>::get_allocator() const{
  return storage.get_allocator();
}
  
  
template<typename T,typename A> 
Array_2D<T,A>&
Array_2D<T,A>::operator=(const Array_2D<T,A>& a){

  if (&a!=this){
    x_dim   = a.x_dim;
    y_dim   = a.y_dim;
    storage = a.storage;
  }

  return *this;
}


template<typename T,typename A> 
bool
Array_2D<T,A>::operator==(const Array_2D<T,A>& a){
  return ((x_dim==a.x_dim)&&(y_dim==a.y_dim)&&(storage==a.storage));
}


template<typename T,typename A> 
bool
Array_2D<T,A>::operator!=(const Array_2D<T,A>& a){
  return !(*this==a);
}


template<typename T,typename A>
template<typename Vector_position>
typename Array_2D<T,A>::reference
Array_2D<T,A>::operator[](const Vector_position& v){
  return storage[offset(v)];
}


template<typename T,typename A>
template<typename Vector_position>
typename Array_2D<T,A>::const_reference
Array_2D<T,A>::operator[](const Vector_position& v) const{
  return storage[offset(v)];  
}


template<typename T,typename A> 
typename Array_2D<T,A>::reference
Array_2D<T,A>::operator()(const size_type x,const size_type y){
  return storage[offset(x,y)]; 
}


template<typename T,typename A> 
typename Array_2D<T,A>::const_reference
Array_2D<T,A>::operator()(const size_type x,const size_type y) const{  
  return storage[offset(x,y)]; 
}


template<typename T,typename A>
template<typename Vector_position>
typename Array_2D<T,A>::reference
Array_2D<T,A>::at(const Vector_position& v){

  if ((v[0]>=x_dim)||(v[1]>=y_dim)){
#ifdef ARRAY_EXCEPTION
    if (v[0]>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else{
      throw std::out_of_range("Out of range Y");
    }
#else
    std::cerr<<"[Array_2D<T,A>::at] Out of range"<<std::endl;
    exit(1);
#endif
  }
  
  return storage[offset(v)];
}


template<typename T,typename A>
template<typename Vector_position>
typename Array_2D<T,A>::const_reference
Array_2D<T,A>::at(const Vector_position& v) const{

  if ((v[0]>=x_dim)||(v[1]>=y_dim)){
#ifdef ARRAY_EXCEPTION
    if (v[0]>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else{
      throw std::out_of_range("Out of range Y");
    }
#else
    std::cerr<<"[Array_2D<T,A>::at] Out of range"<<std::endl;
    exit(1);
#endif
  }
  
    return storage[offset(v)];
}


template<typename T,typename A> 
typename Array_2D<T,A>::reference
Array_2D<T,A>::at(const size_type x,const size_type y){

  if ((x>=x_dim)||(y>=y_dim)){
#ifdef ARRAY_EXCEPTION
    if (x>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else{
      throw std::out_of_range("Out of range Y");
    }
#else
    std::cerr<<"[Array_2D<T,A>::at] Out of range"<<std::endl;
    exit(1);
#endif
  }

   return storage[offset(x,y)]; 
}


template<typename T,typename A> 
typename Array_2D<T,A>::const_reference
Array_2D<T,A>::at(const size_type x,const size_type y) const{

  if ((x>=x_dim)||(y>=y_dim)){
#ifdef ARRAY_EXCEPTION
    if (x>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else{
      throw std::out_of_range("Out of range Y");
    }
#else
    std::cerr<<"[Array_2D<T,A>::at] Out of range"<<std::endl;
    exit(1);
#endif
  }

  return storage[offset(x,y)]; 
}

  
template<typename T,typename A> 
typename Array_2D<T,A>::iterator
Array_2D<T,A>::begin(){
  return storage.begin();
}


template<typename T,typename A> 
typename Array_2D<T,A>::const_iterator
Array_2D<T,A>::begin() const{
  return storage.begin();
}


template<typename T,typename A> 
typename Array_2D<T,A>::iterator
Array_2D<T,A>::end(){
  return storage.end();
}


template<typename T,typename A> 
typename Array_2D<T,A>::const_iterator
Array_2D<T,A>::end() const{
  return storage.end();
}


template<typename T,typename A> 
typename Array_2D<T,A>::reverse_iterator
Array_2D<T,A>::rbegin(){
  return storage.rbegin();
}


template<typename T,typename A> 
typename Array_2D<T,A>::const_reverse_iterator
Array_2D<T,A>::rbegin() const{
  return storage.rbegin();
}


template<typename T,typename A> 
typename Array_2D<T,A>::reverse_iterator
Array_2D<T,A>::rend(){
  return storage.rend();
}


template<typename T,typename A> 
typename Array_2D<T,A>::const_reverse_iterator
Array_2D<T,A>::rend() const{
  return storage.rend();
}


template<typename T,typename A> 
template<typename Vector_position>
typename Array_2D<T,A>::size_type
Array_2D<T,A>::offset(const Vector_position& v) const{

#ifdef CHECK_ARRAY_ACCESS
  if ((v[0] >= x_dim) || (v[1] >= y_dim)){
    std::cerr<<"Array_2D: Out of range ("<<v[0]<<","<<v[1]<<"), actual size is "<<x_dim<<"x"<<y_dim<<std::endl;
  }
  exit(1);
#endif
  
  return v[0]*y_dim + v[1];
}


template<typename T,typename A> 
typename Array_2D<T,A>::size_type
Array_2D<T,A>::offset(const size_type& x,const size_type& y) const{
  
#ifdef CHECK_ARRAY_ACCESS
  if ((x >= x_dim) || (y >= y_dim)){
    std::cerr<<"Array_2D: Out of range ("<<x<<","<<y<<"), actual size is "<<x_dim<<"x"<<y_dim<<std::endl;
  }
  exit(1);
#endif
  
    return x*y_dim + y;
}


#ifndef NO_XML

template<typename T,typename A> 
QDomElement Array_2D<T,A>::to_DOM_element(const QString& name,
					  QDomDocument& document) const{

  QDomElement main_element = document.createElement(name);
  main_element.setAttribute("width",QString::number(width()));
  main_element.setAttribute("height",QString::number(height()));

  std::ostringstream out;

  for(const_iterator i=begin(),i_end=end();i!=i_end;i++){
    out<<(*i)<<' ';
  }

  main_element.appendChild(document.createTextNode(out.str()));
  return main_element;
}


template<typename T,typename A> 
void Array_2D<T,A>::from_DOM_element(const QDomElement& element){

  QDomElement& elt = const_cast<QDomElement&>(element);

  const size_type width  = elt.attributeNode("width").value().toUInt();
  const size_type height = elt.attributeNode("height").value().toUInt();

  resize(width,height);

  std::istringstream in(elt.text());
    
  for(iterator i=begin(),i_end=end();i!=i_end;i++){
    in>>(*i);
  }
}

#endif






/*

  ##################
  # class Array_3D #
  ##################

 */










template<typename T,typename A>
Array_3D<T,A>::Array_3D(const A& a)
  :x_dim(0),y_dim(0),z_dim(0),storage(){}


template<typename T,typename A>
Array_3D<T,A>::Array_3D(const size_type nx,
			const size_type ny,
			const size_type nz,
			const T& val,
			const A& a)
  :x_dim(nx),y_dim(ny),z_dim(nz),storage(nx*ny*nz,val,a){}


/*!
  Fills in the array with the elements between \p begin_elt and
  \p \e end_elt.

  Throw the length_error() exception if not enough elements.
 */
template<typename T,typename A>
template<typename Element_iterator>
Array_3D<T,A>::Array_3D(Element_iterator begin_elt,
			Element_iterator end_elt,
			const size_type nx,
			const size_type ny,
			const size_type nz,
			const A& a)
  :x_dim(nx),y_dim(ny),z_dim(nz),storage(a){

  const size_type s = x_dim*y_dim*z_dim;
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
    std::cerr<<"[Array_3D<T,A>::Array_3D] Not enough elements to initialize the array"<<std::endl;
    exit(1);
#endif
  }
}


template<typename T,typename A>
Array_3D<T,A>::Array_3D(const Array_3D<T,A>& a)
  :x_dim(a.x_dim),y_dim(a.y_dim),z_dim(a.z_dim),storage(a.storage){}


template<typename T,typename A>
void
Array_3D<T,A>::assign(const size_type nx,
		      const size_type ny,
		      const size_type nz,
		      const T& val){
  x_dim = nx;
  y_dim = ny;
  z_dim = nz;  
  storage.assign(x_dim*y_dim*z_dim,val);;
}


template<typename T,typename A>
bool
Array_3D<T,A>::empty() const{
  return storage.empty();
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::x_size() const{
  return x_dim;
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::width() const{
  return x_dim;
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::y_size() const{
  return y_dim;
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::height() const{
  return y_dim;
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::z_size() const{
  return z_dim;
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::depth() const{
  return z_dim;
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::size() const{
  return storage.size();
}


template<typename T,typename A>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::max_size() const{
  return storage.max_size();
}


template<typename T,typename A> 
void
Array_3D<T,A>::resize(const size_type nx,
		      const size_type ny,
		      const size_type nz){
  x_dim = nx;
  y_dim = ny;
  z_dim = nz;
  storage.resize(x_dim * y_dim * z_dim);
}


template<typename T,typename A> 
void
Array_3D<T,A>::swap(Array_3D<T,A>& a){
  size_type x_buf = x_dim;
  size_type y_buf = y_dim;
  size_type z_buf = z_dim;
  
  x_dim = a.x_dim;
  y_dim = a.y_dim;
  z_dim = a.z_dim;

  a.x_dim = x_buf;
  a.y_dim = y_buf;
  a.z_dim = z_buf;

  storage.swap(a.storage);
}


template<typename T,typename A> 
typename Array_3D<T,A>::allocator_type
Array_3D<T,A>::get_allocator() const{
  return storage.get_allocator();
}
  
  
template<typename T,typename A> 
Array_3D<T,A>&
Array_3D<T,A>::operator=(const Array_3D<T,A>& a){

  if (&a!=this){
    x_dim   = a.x_dim;
    y_dim   = a.y_dim;
    z_dim   = a.z_dim;
    storage = a.storage;
  }

  return *this;
}


template<typename T,typename A> 
bool
Array_3D<T,A>::operator==(const Array_3D<T,A>& a){
  return ((x_dim==a.x_dim)
	  &&(y_dim==a.y_dim)
	  &&(z_dim==a.z_dim)
	  &&(storage==a.storage));
}


template<typename T,typename A> 
bool
Array_3D<T,A>::operator!=(const Array_3D<T,A>& a){
  return !(*this==a);
}


template<typename T,typename A> 
template<typename Vector_position>
typename Array_3D<T,A>::reference
Array_3D<T,A>::operator[](const Vector_position& v){
  return storage[offset(v)];
}


template<typename T,typename A> 
template<typename Vector_position>
typename Array_3D<T,A>::const_reference
Array_3D<T,A>::operator[](const Vector_position& v) const{
  return storage[offset(v)];  
}


template<typename T,typename A> 
typename Array_3D<T,A>::reference
Array_3D<T,A>::operator()(const size_type x,
			  const size_type y,
			  const size_type z){
  
  return storage[offset(x,y,z)]; 
}


template<typename T,typename A> 
typename Array_3D<T,A>::const_reference
Array_3D<T,A>::operator()(const size_type x,
			  const size_type y,
			  const size_type z) const{
  
  return storage[offset(x,y,z)]; 
}


template<typename T,typename A> 
template<typename Vector_position>
typename Array_3D<T,A>::reference
Array_3D<T,A>::at(const Vector_position& v){

  if ((v[0]>=x_dim)||(v[1]>=y_dim)||(v[2]>=z_dim)){
#ifdef ARRAY_EXCEPTION
    if (v[0]>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else if (v[1]>=y_dim) {
      throw std::out_of_range("Out of range Y");
    }
    else {
      throw std::out_of_range("Out of range Z");
    }
#else
    std::cerr<<"[Array_3D<T,A>::at] Out of range"<<std::endl;
    exit(1);
#endif
  }
  
  return storage[offset(v)];
}


template<typename T,typename A> 
template<typename Vector_position>
typename Array_3D<T,A>::const_reference
Array_3D<T,A>::at(const Vector_position& v) const{

  if ((v[0]>=x_dim)||(v[1]>=y_dim)||(v[2]>=z_dim)){
#ifdef ARRAY_EXCEPTION
    if (v[0]>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else if (v[1]>=y_dim) {
      throw std::out_of_range("Out of range Y");
    }
    else {
      throw std::out_of_range("Out of range Z");
    }
#else
    std::cerr<<"[Array_3D<T,A>::at] Out of range"<<std::endl;
    exit(1);
#endif
  }
  
    return storage[offset(v)];
}


template<typename T,typename A> 
typename Array_3D<T,A>::reference
Array_3D<T,A>::at(const size_type x,
		  const size_type y,
		  const size_type z){

  if ((x>=x_dim)||(y>=y_dim)||(z>=z_dim)){
#ifdef ARRAY_EXCEPTION
    if (x>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else if (y>=y_dim) {
      throw std::out_of_range("Out of range Y");
    }
    else {
      throw std::out_of_range("Out of range Z");
    }
#else
    std::cerr<<"[Array_3D<T,A>::at] Out of range"<<std::endl;
#endif
  }

   return storage[offset(x,y,z)]; 
}


template<typename T,typename A> 
typename Array_3D<T,A>::const_reference
Array_3D<T,A>::at(const size_type x,
		  const size_type y,
		  const size_type z) const{

  if ((x>=x_dim)||(y>=y_dim)||(z>=z_dim)){
#ifdef ARRAY_EXCEPTION
    if (x>=x_dim) {
      throw std::out_of_range("Out of range X");
    }
    else if (y>=y_dim) {
      throw std::out_of_range("Out of range Y");
    }
    else {
      throw std::out_of_range("Out of range Z");
    }
#else
    std::cerr<<"[Array_3D<T,A>::at] Out of range"<<std::endl;
#endif
  }

  return storage[offset(x,y,z)]; 
}

  
template<typename T,typename A> 
typename Array_3D<T,A>::iterator
Array_3D<T,A>::begin(){
  return storage.begin();
}


template<typename T,typename A> 
typename Array_3D<T,A>::const_iterator
Array_3D<T,A>::begin() const{
  return storage.begin();
}


template<typename T,typename A> 
typename Array_3D<T,A>::iterator
Array_3D<T,A>::end(){
  return storage.end();
}


template<typename T,typename A> 
typename Array_3D<T,A>::const_iterator
Array_3D<T,A>::end() const{
  return storage.end();
}


template<typename T,typename A> 
typename Array_3D<T,A>::reverse_iterator
Array_3D<T,A>::rbegin(){
  return storage.rbegin();
}


template<typename T,typename A> 
typename Array_3D<T,A>::const_reverse_iterator
Array_3D<T,A>::rbegin() const{
  return storage.rbegin();
}


template<typename T,typename A> 
typename Array_3D<T,A>::reverse_iterator
Array_3D<T,A>::rend(){
  return storage.rend();
}


template<typename T,typename A> 
typename Array_3D<T,A>::const_reverse_iterator
Array_3D<T,A>::rend() const{
  return storage.rend();
}


template<typename T,typename A> 
template<typename Vector_position>
typename Array_3D<T,A>::size_type
Array_3D<T,A>::offset(const Vector_position& v) const{

#ifdef CHECK_ARRAY_ACCESS
  if ((v[0] >= x_dim) || (v[1] >= y_dim) || (v[2] >= z_dim)){
    std::cerr<<"Array_3D: Out of range ("<<v[0]<<","<<v[1]<<","<<v[2]<<"), actual size is "<<x_dim<<"x"<<y_dim<<"x"<<z_dim<<std::endl;
  }
#endif

  return (v[0]*y_dim + v[1])*z_dim + v[2];
}


template<typename T,typename A> 
typename Array_3D<T,A>::size_type
Array_3D<T,A>::offset(const size_type& x,
		      const size_type& y,
		      const size_type& z) const{

#ifdef CHECK_ARRAY_ACCESS
  if ((x >= x_dim) || (y >= y_dim) || (z >= z_dim)){
    std::cerr<<"Array_3D: Out of range ("<<x<<","<<y<<","<<z<<"), actual size is "<<x_dim<<"x"<<y_dim<<"x"<<z_dim<<std::endl;
  }
#endif

    return (x*y_dim + y)*z_dim + z;
}

#endif
