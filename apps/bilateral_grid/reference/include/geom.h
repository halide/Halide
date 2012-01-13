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
 *  Defines matrices and vectors with basic operators: * / + -...
 *
 *  Dimensions and scalar type are templated. Usual types are
 * predefined: Vec3f, Vec2D, Matrix44f, etc.
 *
 *  Visual studio may cause trouble, depending on its version.
 */


#ifndef __GEOM__
#define __GEOM__

#include <cmath>

#include <vector>
#include <iostream>

namespace Geometry {

/*

  ################
  # typedefs ... #
  ################

*/


  template<int N,typename Real> class Vec;
  template<typename Real>       class Vec2;
  template<typename Real>       class Vec3;
  template<typename Real>       class Hvec2;
  template<typename Real>       class Hvec3;

  template<int N_row,int N_col,typename Real> class Matrix;
  template<int N,typename Real>               class Square_matrix;

  typedef Vec2<int>     Vec2i;
  typedef Vec2<float>   Vec2f;
  typedef Vec2<double>  Vec2d;

  typedef Vec3<int>     Vec3i;
  typedef Vec3<float>   Vec3f;
  typedef Vec3<double>  Vec3d;

  typedef Hvec2<int>    Hvec2i;
  typedef Hvec2<float>  Hvec2f;
  typedef Hvec2<double> Hvec2d;

  typedef Hvec3<int>    Hvec3i;
  typedef Hvec3<float>  Hvec3f;
  typedef Hvec3<double> Hvec3d;

  typedef Square_matrix<2,int>    Matrix22i;
  typedef Square_matrix<2,float>  Matrix22f;
  typedef Square_matrix<2,double> Matrix22d;

  typedef Square_matrix<3,int>    Matrix33i;
  typedef Square_matrix<3,float>  Matrix33f;
  typedef Square_matrix<3,double> Matrix33d;

  typedef Square_matrix<4,int>    Matrix44i;
  typedef Square_matrix<4,float>  Matrix44f;
  typedef Square_matrix<4,double> Matrix44d;


  
  
/*

  #############
  # class Vec #
  #############

*/

  //@{
  //! Pre-declaration needed for friend declarations.
  
  template<int N,typename Real>
  inline typename Vec<N,Real>::value_type operator*(const Vec<N,Real>& v1,
						    const Vec<N,Real>& v2);

  
  template<int N,typename Real>
  std::ostream& operator<<(std::ostream& s,
			   const Vec<N,Real>& v);
  //@}


  //! Represents a vector with fixed dimension \e N with coordinates
  //! of type \e Real.
  template<int N,typename Real>
  class Vec{

  public:
    typedef Real value_type;
    static const unsigned int dimension = N;
    
    //@{
    //! Classical constructor.

    inline Vec();
    explicit inline Vec(const value_type tab[N]);
    explicit inline Vec(const std::vector<value_type>& tab);
    inline Vec(const Vec<N,Real>& v);
    //@}

    //@{
    //! Classical access operator.
  
    inline const value_type& operator[](const int i) const;
    inline value_type& operator[](const int i);
    //@}


    //@{
    //! Classical value.
  
    inline value_type norm() const;
    inline value_type square_norm() const;
    //@}

    //! Normalizes the vector in place.
    inline Vec<N,Real>& normalize();

    //! Return the corresponding unit vector.
    inline Vec<N,Real> unit() const;

    //! Return the corresponding column matrix.
    inline Matrix<N,1,Real> column_matrix() const;

    //@{
    //! Classical operator.

    inline Vec<N,Real>& operator=(const Vec<N,Real>& v);
    inline bool operator==(const Vec<N,Real>& v) const;
    inline bool operator!=(const Vec<N,Real>& v) const;
    inline Vec<N,Real>& operator+=(const Vec<N,Real>& v);
    inline Vec<N,Real>& operator-=(const Vec<N,Real>& v);
    inline Vec<N,Real>& operator*=(const value_type r);
    inline Vec<N,Real>& operator/=(const value_type r);
    inline Vec<N,Real> operator-() const;
    //@}
    
    friend value_type operator*<N,Real>(const Vec<N,Real>& v1,
					const Vec<N,Real>& v2);

    template <int Row,int Col,typename Real_t>
    friend inline Vec<Row,Real_t>
    operator*(const Matrix<Row,Col,Real_t>& m,
	      const Vec<Col,Real_t>& v);

  
    friend std::ostream& operator<<<N,Real>(std::ostream& s,
					    const Vec<N,Real>& v);


  private:
    value_type coordinate[N];
  };


  

/*

  ########################
  # Comparison predicate #
  ########################

*/


  //! < on the Nth coordinate.
  template<typename Vector,int N> class Compare_coordinate {
  public:
    inline Compare_coordinate(){}
    
    inline bool operator()(const Vector& v1,const Vector v2) const{
      return (v1[N]<v2[N]);
    }
  };



  template<typename Vector> class Lexicographical_order {
  public:
    inline Lexicographical_order(){}
    
    inline bool operator()(const Vector& v1,const Vector v2) const{

      for(unsigned int n = 0;n < Vector::dimension;n++){
	
	const typename Vector::value_type v1_n = v1[n];
	const typename Vector::value_type v2_n = v2[n];
	  
	if (v1_n < v2_n) return true;
	if (v1_n > v2_n) return false;
      }
      return false;
    }
  };

  

/*

  ##############
  # class Vec2 #
  ##############

*/


  //! Represents a vector of dimension 2 with \e Real coordinates.
  template<typename Real>
  class Vec2:public Vec<2,Real>{
  
  public:

    //! Used type.
    typedef typename Vec<2,Real>::value_type value_type;

    //@{
    //! Classical constructor.

    inline Vec2();
    explicit inline Vec2(const value_type tab[2]);
    explicit inline Vec2(const std::vector<value_type>& tab);
    inline Vec2(const Vec<2,Real>& v);
    //@}
    
    //! 2D constructor.
    inline Vec2(const value_type x,const value_type y);

    //@{
    //! 2D access.
    
    inline const value_type& x() const;
    inline value_type& x();

    inline const value_type& y() const;
    inline value_type& y();
    //@}
  };



/*

  ##############
  # class Vec3 #
  ##############

*/



//! Represents a vector of dimension 3 with \e Real coordinates.
  template<typename Real>
  class Vec3:public Vec<3,Real>{
  
  public:
    
    //! Used type.
    typedef typename Vec<3,Real>::value_type value_type;

    //@{
    //! Classical constructor.

    inline Vec3();
    explicit inline Vec3(const value_type tab[3]);
    explicit inline Vec3(const std::vector<value_type>& tab);
    inline Vec3(const Vec<3,Real>& v);
    //@}

    //@{
    //! 3D constructor.
  
    inline Vec3(const value_type x,const value_type y,const value_type z=0);
    inline Vec3(const Vec2<Real>& v,const value_type z=0);
    inline Vec3(const Hvec3<Real>& v);
    //@}

    //@{
    //! 3D access.
    
    inline const value_type& x() const;
    inline value_type& x();

    inline const value_type& y() const;
    inline value_type& y();

    inline const value_type& z() const;
    inline value_type& z();
    //@}

    //! Produit vectoriel.
    template<typename Real_t>
    friend Vec3<Real_t> operator^(const Vec3<Real_t>& v1,
				  const Vec3<Real_t>& v2);

  };


  
/*

  ###############
  # class Hvec2 #
  ###############

*/

  
  //! Represents a vector of dimension 2 with \e Real homogeneous coordinates.
/*!
  The vector has 3 coordinates: \e (sx,sy,s).
*/
  template<typename Real>
  class Hvec2:public Vec<3,Real>{
  
  public:

    //! Used type.
    typedef typename Vec<3,Real>::value_type value_type;

    //@{
    //! Classical constructor.

    inline Hvec2();
    explicit inline Hvec2(const value_type tab[3]);
    explicit inline Hvec2(const std::vector<value_type>& tab);
    inline Hvec2(const Vec<3,Real>& v);
    //@}

    //@{
    //! 2D homogeneous constructor.
  
    inline Hvec2(const value_type sx,
		 const value_type sy,
		 const value_type s=1);
    
    inline Hvec2(const Vec2<Real>& sv,
		 const value_type s=1);
    
    //@}

    //@{
    //! 2D homogeneous access.
    
    inline const value_type& sx() const;
    inline value_type& sx();

    inline const value_type& sy() const;
    inline value_type& sy();

    inline const value_type& s() const;
    inline value_type& s();
    //@}

    //@{
    //! Access as a 3D entity.

    inline value_type x() const;
    inline value_type y() const;
    inline value_type z() const;
    //@}
  };



/*

  ###############
  # class Hvec3 #
  ###############

*/

//! Represents a vector of dimension 3 with \e Real homogeneous coordinates.
/*!
  The vector has 3 coordinates: \e (sx,sy,sz,s).
*/
  template<typename Real>
  class Hvec3:public Vec<4,Real>{
  
  public:

    //! Used type.
    typedef typename Vec<4,Real>::value_type value_type;

    //@{
    //! Classical constructor.

    inline Hvec3();
    explicit inline Hvec3(const value_type tab[4]);
    explicit inline Hvec3(const std::vector<value_type>& tab);
    inline Hvec3(const Vec<4,Real>& v);
    //@}

    //@{
    //! 3D homogeneous constructor.
  
    inline Hvec3(const value_type sx,
		 const value_type sy,
		 const value_type sz=0,
		 const value_type s=1);
    
    inline Hvec3(const Vec2<Real>& sv,
		 const value_type sz=0,
		 const value_type s=1);
    
    inline Hvec3(const Vec3<Real>& sv,
		 const value_type s=1);  
    //@}

    //@{
    //! 3D homogeneous access.
    
    inline const value_type& sx() const;
    inline value_type& sx();

    inline const value_type& sy() const;
    inline value_type& sy();

    inline const value_type& sz() const;
    inline value_type& sz();

    inline const value_type& s() const;
    inline value_type& s();
    //@}

    //@{
    //! Access as a non-homogenous 3D entity.

    inline value_type x() const;
    inline value_type y() const;
    inline value_type z() const;
    //@}
  };



/*

  ################
  # class Matrix #
  ################

*/

  //@{
  //! Pre-declaration needed for friend declarations.
  
  template<int N_row,int N_col,typename Real>
  inline std::ostream& operator<<(std::ostream& s,
				  const Matrix<N_row,N_col,Real>& m);


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator+(const Matrix<N_row,N_col,Real>& m1,
	    const Matrix<N_row,N_col,Real>& m2);


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator-(const Matrix<N_row,N_col,Real>& m1,
	    const Matrix<N_row,N_col,Real>& m2);


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator*(const Matrix<N_row,N_col,Real>& m,
	    const typename Matrix<N_row,N_col,Real>::value_type lambda);


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator*(const typename Matrix<N_row,N_col,Real>::value_type lambda,
	    const Matrix<N_row,N_col,Real>& m);

  
  template<int N,int P,int Q,typename Real_t>
  inline Matrix<N,Q,Real_t>
  operator*(const Matrix<N,P,Real_t>& m1,
	      const Matrix<P,Q,Real_t>& m2);


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator/(const Matrix<N_row,N_col,Real>& m,
	    const typename Matrix<N_row,N_col,Real>::value_type lambda);
  
  
  template <int Row,int Col,typename Real_t>
  inline Vec<Row,Real_t>
  operator*(const Matrix<Row,Col,Real_t>& m,
	    const Vec<Col,Real_t>& v); 
  //@}

  
  
  template<int N_row,int N_col,typename Real>
  class Matrix{

  public:

    //! Used type.
    typedef Real value_type;
    
    //@{
    //! Classical constructor.
  
    inline Matrix();
    explicit inline Matrix(const value_type tab[N_row][N_col]);
    inline Matrix(const Matrix<N_row,N_col,Real>& m);
    //@}

    inline Matrix<N_col,N_row,Real> transpose() const;

    //@{
    //! Classical operator.
  
    inline Matrix<N_row,N_col,Real>& operator=(const Matrix<N_row,N_col,Real>& m);
    inline Matrix<N_row,N_col,Real>& operator+=(const Matrix<N_row,N_col,Real>& m);
    inline Matrix<N_row,N_col,Real>& operator-=(const Matrix<N_row,N_col,Real>& m);
    inline Matrix<N_row,N_col,Real>& operator*=(const value_type& lambda);
    inline Matrix<N_row,N_col,Real>& operator/=(const value_type& lambda);
    inline Matrix<N_row,N_col,Real> operator-() const;
    //@}

    //@{
    //! Classical access operator.

    inline const value_type& operator()(const int i,const int j) const;
    inline value_type& operator()(const int i,const int j);

    inline const value_type& operator[](const Vec2i& v) const;
    inline value_type& operator[](const Vec2i& v);
    //@}

    //! Creates a single-column vector with all the elements.
    inline Vec<N_row * N_col,Real> unfold_to_vector() const;
    
    //! Fills the matrix from a single-column vector.
    inline Matrix<N_row,N_col,Real>&
    fold_from_vector(const Vec<N_row * N_col,Real>& v);
  
    //@{
    //! Classical operator.

    friend Matrix<N_row,N_col,Real>
    operator+<N_row,N_col,Real>(const Matrix<N_row,N_col,Real>& m1,
				const Matrix<N_row,N_col,Real>& m2);

//     friend Matrix<N_row,N_col,Real>
//     operator-<N_row,N_col,Real>(const Matrix<N_row,N_col,Real>& m1,
// 				const Matrix<N_row,N_col,Real>& m2);
    
    friend Matrix<N_row,N_col,Real>
    operator*<N_row,N_col,Real>(const Matrix<N_row,N_col,Real>& m,
				const value_type lambda);

    friend Matrix<N_row,N_col,Real>
    operator*<N_row,N_col,Real>(const value_type lambda,
				const Matrix<N_row,N_col,Real>& m);

    template<int N,int P,int Q,typename Real_t>
    friend Matrix<N,Q,Real_t>
    operator*(const Matrix<N,P,Real_t>& m1,
	      const Matrix<P,Q,Real_t>& m2);

    friend Matrix<N_row,N_col,Real>
    operator/<N_row,N_col,Real>(const Matrix<N_row,N_col,Real>& m,
				const value_type lambda);
 
    friend Vec<N_row,Real>
    operator*<N_row,N_col,Real>(const Matrix<N_row,N_col,Real>& m,
				const Vec<N_col,Real>& v);  


    friend std::ostream& operator<<<N_row,N_col,Real>(std::ostream& s,
						      const Matrix<N_row,N_col,Real>& m);

    //@}


    //@{
    //! Row and column operators

    inline void swap_rows(const int row1,const int row2);
    inline void multiply_row(const int row,const value_type lambda);
    inline Vec<N_col,Real> get_vector_from_row(const int row) const;
    inline void add_vector_to_row(const int row,const Vec<N_col,Real>& vec);

    //@}
    
  private:
    value_type component[N_row][N_col];

  };




/*

  #######################
  # class Square_matrix #
  #######################

*/



  template<int N,typename Real>
  class Square_matrix:public Matrix<N,N,Real>{

  public:
    //! Used type.
    typedef Real value_type;
    
    //@{
    //! Classical constructor.
  
    inline Square_matrix();
    explicit inline Square_matrix(const value_type tab[N][N]);
    inline Square_matrix(const Matrix<N,N,Real>& m);
    //@}

    inline value_type trace() const;
    
    //! Identity matrix.
    static inline Square_matrix<N,Real> identity();
  };




/*

  ##########################
  # Out of class functions #
  ##########################

*/

//@{
//! Classical operator.

  template<int N,typename Real>
  inline Vec<N,Real> operator+(const Vec<N,Real>& v1,
			       const Vec<N,Real>& v2);

  template<int N,typename Real>
  inline Vec<N,Real> operator-(const Vec<N,Real>& v1,
			       const Vec<N,Real>& v2);

  template<int N,typename Real>
  inline Vec<N,Real> operator*(const Vec<N,Real>& v,
			       const typename Vec<N,Real>::value_type r);

  template<int N,typename Real>
  inline Vec<N,Real> operator*(const typename Vec<N,Real>::value_type r,
			       const Vec<N,Real>& v);

  template<int N,typename Real>
  inline Vec<N,Real> operator/(const Vec<N,Real>& v,
			       const typename Vec<N,Real>::value_type r);
//@}




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

  #############
  # class Vec #
  #############

*/

  template<int N,typename Real>
  Vec<N,Real>::Vec(){
    for(int i=0;i<N;i++){
      coordinate[i] = Real();
    }
  }


  template<int N,typename Real>
  Vec<N,Real>::Vec(const value_type tab[N]){
  
    for(int i=0;i<N;i++){
      coordinate[i] = tab[i];
    }
  }

/*!
  No size check.
*/
  template<int N,typename Real>
  Vec<N,Real>::Vec(const std::vector<value_type>& tab){
  
    for(int i=0;i<N;i++){
      coordinate[i] = tab[i];
    }
  }


  template<int N,typename Real>
  Vec<N,Real>::Vec(const Vec<N,Real>& v){

    for(int i=0;i<N;i++){
      coordinate[i] = v.coordinate[i];
    }
  }


  template<int N,typename Real>
  const typename Vec<N,Real>::value_type&
  Vec<N,Real>::operator[](const int i) const{
    return coordinate[i];
  }


  template<int N,typename Real>
  typename Vec<N,Real>::value_type&
  Vec<N,Real>::operator[](const int i){
    return coordinate[i];
  }


  template<int N,typename Real>
  typename Vec<N,Real>::value_type
  Vec<N,Real>::norm() const{

    return sqrt(square_norm());
  }


  template<int N,typename Real>
  typename Vec<N,Real>::value_type
  Vec<N,Real>::square_norm() const{

    return (*this)*(*this);
  }


  template<int N,typename Real>
  Vec<N,Real>& Vec<N,Real>::normalize(){

    const value_type n = norm();
    if (n != 0){
      for(int i = 0;i < N;++i){
	coordinate[i] /= n;
      }
    }
    
    return *this;
  }

  
  template<int N,typename Real>
  inline Vec<N,Real> Vec<N,Real>::unit() const{
    
    return Vec<N,Real>(*this).normalize();
  }


  
  template<int N,typename Real>
  inline Matrix<N,1,Real> Vec<N,Real>::column_matrix() const{

    Matrix<N,1,Real> m;

    for(int i = 0;i < N;++i){
      m(i,0) = coordinate[i];
    }

    return m;
  }


  
  template<int N,typename Real>
  Vec<N,Real>&
  Vec<N,Real>::operator=(const Vec<N,Real>& v){

    if (this!=&v) {
      for(int i=0;i<N;i++){
	coordinate[i] = v.coordinate[i];
      }
    }

    return *this;
  }




  template<int N,typename Real>
  bool
  Vec<N,Real>::operator==(const Vec<N,Real>& v) const{
    
    for(int i=0;i<N;i++){
      if (coordinate[i]!=v.coordinate[i]){
	return false;
      }
    }
    
    return true;
  }
  
  template<int N,typename Real>
  bool
  Vec<N,Real>::operator!=(const Vec<N,Real>& v) const{
    
    return !(*this==v);
  }

  template<int N,typename Real>
  Vec<N,Real>&
  Vec<N,Real>::operator+=(const Vec<N,Real>& v){

    for(int i=0;i<N;i++){
      coordinate[i] += v.coordinate[i];
    }

    return *this;
  }


  template<int N,typename Real>
  Vec<N,Real>& Vec<N,Real>::operator-=(const Vec<N,Real>& v){

    for(int i=0;i<N;i++){
      coordinate[i] -= v.coordinate[i];
    }

    return *this;
  }


  template<int N,typename Real>
  Vec<N,Real>& Vec<N,Real>::operator*=(const value_type r){

    for(int i=0;i<N;i++){
      coordinate[i] *= r;
    }

    return *this;
  }


/*!
  No check for r<>0 .
*/
  template<int N,typename Real>
  Vec<N,Real>& Vec<N,Real>::operator/=(const value_type r){
  
    for(int i=0;i<N;i++){
      coordinate[i] /= r;
    }

    return *this;
  }


  template<int N,typename Real>
  Vec<N,Real> Vec<N,Real>::operator-() const{
    Vec<N,Real> res;
    
    for(int i=0;i<N;i++){
      res.coordinate[i] = -coordinate[i];
    }

    return res;
  }


/*

  ##############
  # class Vec2 #
  ##############

*/

  template<typename Real>
  Vec2<Real>::Vec2()
    :Vec<2,Real>(){}

  
  template<typename Real>
  Vec2<Real>::Vec2(const value_type tab[2])
    :Vec<2,Real>(tab){}

  
  template<typename Real>
  Vec2<Real>::Vec2(const std::vector<value_type>& tab)
    :Vec<2,Real>(tab){}

  
  template<typename Real>
  Vec2<Real>::Vec2(const Vec<2,Real>& v)
    :Vec<2,Real>(v){}


  template<typename Real>
  Vec2<Real>::Vec2(const value_type x,const value_type y)
    :Vec<2,Real>(){

    (*this)[0] = x;
    (*this)[1] = y;
  }


  template<typename Real>
  const typename Vec2<Real>::value_type&
  Vec2<Real>::x() const{
    return (*this)[0];
  }


  template<typename Real>
  typename Vec2<Real>::value_type&
  Vec2<Real>::x(){
    return (*this)[0];
  }


  template<typename Real>
  const typename Vec2<Real>::value_type&
  Vec2<Real>::y() const{
    return (*this)[1];
  }


  template<typename Real>
  typename Vec2<Real>::value_type&
  Vec2<Real>::y(){
    return (*this)[1];
  }



/*

  ##############
  # class Vec3 #
  ##############

*/

  template<typename Real>
  Vec3<Real>::Vec3()
    :Vec<3,Real>(){}

  
  template<typename Real>
  Vec3<Real>::Vec3(const value_type tab[3])
    :Vec<3,Real>(tab){}

  
  template<typename Real>
  Vec3<Real>::Vec3(const std::vector<value_type>& tab)
    :Vec<3,Real>(tab){}

  
  template<typename Real>
  Vec3<Real>::Vec3(const Vec<3,Real>& v)
    :Vec<3,Real>(v){}


  
  template<typename Real>
  Vec3<Real>::Vec3(const value_type x,const value_type y,const value_type z)
    :Vec<3,Real>(){

    (*this)[0] = x;
    (*this)[1] = y;
    (*this)[2] = z;
  }


  template<typename Real>
  Vec3<Real>::Vec3(const Vec2<Real>& v,const value_type z)
    :Vec<3,Real>(){

    (*this)[0] = v.x();
    (*this)[1] = v.y();
    (*this)[2] = z;
  }


  template<typename Real>
  Vec3<Real>::Vec3(const Hvec3<Real>& v)
    :Vec<3,Real>(){
  
    (*this)[0] = v.x();
    (*this)[1] = v.y();
    (*this)[2] = v.z();
  }



  template<typename Real>
  const typename Vec3<Real>::value_type&
  Vec3<Real>::x() const{
    return (*this)[0];
  }


  template<typename Real>
  typename Vec3<Real>::value_type&
  Vec3<Real>::x(){
    return (*this)[0];
  }


  template<typename Real>
  const typename Vec3<Real>::value_type&
  Vec3<Real>::y() const{
    return (*this)[1];
  }


  template<typename Real>
  typename Vec3<Real>::value_type&
  Vec3<Real>::y(){
    return (*this)[1];
  }


  template<typename Real>
  const typename Vec3<Real>::value_type&
  Vec3<Real>::z() const{
    return (*this)[2];
  }


  template<typename Real>
  typename Vec3<Real>::value_type&
  Vec3<Real>::z(){
    return (*this)[2];
  }




/*

  ###############
  # class Hvec2 #
  ###############

*/


  template<typename Real>
  Hvec2<Real>::Hvec2()
    :Vec<3,Real>(){}
  
    
  template<typename Real>
  Hvec2<Real>::Hvec2(const value_type tab[3])
    :Vec<3,Real>(tab){}

  
  template<typename Real>
  Hvec2<Real>::Hvec2(const std::vector<value_type>& tab)
    :Vec<3,Real>(tab){}

    
  template<typename Real>
  Hvec2<Real>::Hvec2(const Vec<3,Real>& v)
    :Vec<3,Real>(v){}

  
  
//@{
/*!
  \e (sx,sy,sz,s) are directly given and not \e (x,y,z).
*/
  template<typename Real>
  Hvec2<Real>::Hvec2(const value_type sx,
		     const value_type sy,
		     const value_type s)
    :Vec<4,Real>(){

    (*this)[0] = sx;
    (*this)[1] = sy;
    (*this)[2] = s; 
  }


  template<typename Real>
  Hvec2<Real>::Hvec2(const Vec2<Real>& sv,const value_type s)
    :Vec<3,Real>(){

    (*this)[0] = sv.x();
    (*this)[1] = sv.y();
    (*this)[2] = s;    
  }

//@}


  template<typename Real>
  const typename Hvec2<Real>::value_type&
  Hvec2<Real>::sx() const{
    return (*this)[0];
  }


  template<typename Real>
  typename Hvec2<Real>::value_type&
  Hvec2<Real>::sx(){
    return (*this)[0];
  }


  template<typename Real>
  const typename Hvec2<Real>::value_type&
  Hvec2<Real>::sy() const{
    return (*this)[1];
  }


  template<typename Real>
  typename Hvec2<Real>::value_type&
  Hvec2<Real>::sy(){
    return (*this)[1];
  }


  template<typename Real>
  const typename Hvec2<Real>::value_type&
  Hvec2<Real>::s() const{
    return (*this)[2];
  }


  template<typename Real>
  typename Hvec2<Real>::value_type&
  Hvec2<Real>::s(){
    return (*this)[2];
  }


  template<typename Real>
  typename Hvec2<Real>::value_type
  Hvec2<Real>::x() const{
    return ((*this)[0]/(*this)[2]);
  }


  template<typename Real>
  typename Hvec2<Real>::value_type
  Hvec2<Real>::y() const{
    return ((*this)[1]/(*this)[2]);
  }





/*

  ###############
  # class Hvec3 #
  ###############

*/


  template<typename Real>
  Hvec3<Real>::Hvec3()
    :Vec<4,Real>(){}
  
    
  template<typename Real>
  Hvec3<Real>::Hvec3(const value_type tab[4])
    :Vec<4,Real>(tab){}

  
  template<typename Real>
  Hvec3<Real>::Hvec3(const std::vector<value_type>& tab)
    :Vec<4,Real>(tab){}

    
  template<typename Real>
  Hvec3<Real>::Hvec3(const Vec<4,Real>& v)
    :Vec<4,Real>(v){}

  
  
//@{
/*!
  \e (sx,sy,sz,s) are directly goven and not \e (x,y,z).
*/
  template<typename Real>
  Hvec3<Real>::Hvec3(const value_type sx,
		     const value_type sy,
		     const value_type sz,
		     const value_type s)
    :Vec<4,Real>(){

    (*this)[0] = sx;
    (*this)[1] = sy;
    (*this)[2] = sz;
    (*this)[3] = s; 
  }


  template<typename Real>
  Hvec3<Real>::Hvec3(const Vec2<Real>& sv,const value_type sz,const value_type s)
    :Vec<4,Real>(){

    (*this)[0] = sv.x();
    (*this)[1] = sv.y();
    (*this)[2] = sz;
    (*this)[3] = s;    
  }


  template<typename Real>
  Hvec3<Real>::Hvec3(const Vec3<Real>& sv,const value_type s)
    :Vec<4,Real>(){
  
    (*this)[0] = sv.x();
    (*this)[1] = sv.y();
    (*this)[2] = sv.z();
    (*this)[3] = s;      
  }
//@}


  template<typename Real>
  const typename Hvec3<Real>::value_type&
  Hvec3<Real>::sx() const{
    return (*this)[0];
  }


  template<typename Real>
  typename Hvec3<Real>::value_type&
  Hvec3<Real>::sx(){
    return (*this)[0];
  }


  template<typename Real>
  const typename Hvec3<Real>::value_type&
  Hvec3<Real>::sy() const{
    return (*this)[1];
  }


  template<typename Real>
  typename Hvec3<Real>::value_type&
  Hvec3<Real>::sy(){
    return (*this)[1];
  }


  template<typename Real>
  const typename Hvec3<Real>::value_type&
  Hvec3<Real>::sz() const{
    return (*this)[2];
  }


  template<typename Real>
  typename Hvec3<Real>::value_type&
  Hvec3<Real>::sz(){
    return (*this)[2];
  }


  template<typename Real>
  const typename Hvec3<Real>::value_type&
  Hvec3<Real>::s() const{
    return (*this)[3];
  }


  template<typename Real>
  typename Hvec3<Real>::value_type&
  Hvec3<Real>::s(){
    return (*this)[3];
  }


  template<typename Real>
  typename Hvec3<Real>::value_type
  Hvec3<Real>::x() const{
    return ((*this)[0]/(*this)[3]);
  }


  template<typename Real>
  typename Hvec3<Real>::value_type
  Hvec3<Real>::y() const{
    return ((*this)[1]/(*this)[3]);
  }


  template<typename Real>
  typename Hvec3<Real>::value_type
  Hvec3<Real>::z() const{
    return ((*this)[2]/(*this)[3]);
  }



/*

  ################
  # class Matrix #
  ################

*/



  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>::Matrix(){
    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] = 0;
      }
    }
  }


  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>::Matrix(const value_type tab[N_row][N_col]){

    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] = tab[i][j];
      }
    }
  }


  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>::Matrix(const Matrix<N_row,N_col,Real>& m){
    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] = m.component[i][j];
      }
    }
  }


  template<int N_row,int N_col,typename Real>
  Matrix<N_col,N_row,Real>
  Matrix<N_row,N_col,Real>::
  transpose() const{
  
    Matrix<N_col,N_row,Real> res;

    for(int i = 0;i < N_row;++i){
      for(int j = 0;j < N_col;++j){
	res(j,i) = component[i][j];
      }
    }

    return res;
  }


  template<int N_row,int N_col,typename Real>
  const typename Matrix<N_row,N_col,Real>::value_type&
  Matrix<N_row,N_col,Real>::
  operator()(const int i,const int j) const{
    return component[i][j];
  }


  template<int N_row,int N_col,typename Real>
  typename Matrix<N_row,N_col,Real>::value_type&
  Matrix<N_row,N_col,Real>::
  operator()(const int i,const int j){
    return component[i][j];
  }


  template<int N_row,int N_col,typename Real>
  const typename Matrix<N_row,N_col,Real>::value_type&
  Matrix<N_row,N_col,Real>::
  operator[](const Vec2i& v) const{
    return component[v.x()][v.y()];
  }


  template<int N_row,int N_col,typename Real>
  typename Matrix<N_row,N_col,Real>::value_type&
  Matrix<N_row,N_col,Real>::
  operator[](const Vec2i& v){
    return component[v.x()][v.y()];
  }

  
  template<int N_row,int N_col,typename Real>
  Vec<N_row * N_col,Real>
  Matrix<N_row,N_col,Real>::unfold_to_vector() const{
    Vec<N_row * N_col,Real> v;

    for(int i = 0;i < N_row;++i){
      for(int j = 0;j < N_col;++j){
	v[i * N_col + j] = component[i][j];
      }
    }
 
    return v;
  }

  
  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>&
  Matrix<N_row,N_col,Real>::fold_from_vector(const Vec<N_row * N_col,Real>& v){
    
    for(int i = 0;i < N_row;++i){
      for(int j = 0;j < N_col;++j){
	component[i][j] = v[i * N_col + j];
      }
    }
    return *this;
  }
  

  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>&
  Matrix<N_row,N_col,Real>::
  operator=(const Matrix<N_row,N_col,Real>& m){

    if (this!=&m){
      for(int i=0;i<N_row;i++){
	for(int j=0;j<N_col;j++){
	  component[i][j] = m.component[i][j];
	}
      }
    }

    return *this;
  }


  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>&
  Matrix<N_row,N_col,Real>::
  operator+=(const Matrix<N_row,N_col,Real>& m){

    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] += m.component[i][j];
      }
    }

    return *this;
  }


  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>&
  Matrix<N_row,N_col,Real>::
  operator-=(const Matrix<N_row,N_col,Real>& m){

    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] -= m.component[i][j];
      }
    }

    return *this;
  }


  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>&
  Matrix<N_row,N_col,Real>::
  operator*=(const value_type& lambda){

    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] *= lambda;
      }
    }

    return *this;
  }


/*!
  No check for division by 0.
*/
  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>&
  Matrix<N_row,N_col,Real>::
  operator/=(const value_type& lambda){
  
    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	component[i][j] /= lambda;
      }
    }

    return *this;
  }

  
  template<int N_row,int N_col,typename Real>
  Matrix<N_row,N_col,Real>
  Matrix<N_row,N_col,Real>::
  operator-() const{
    Matrix<N_row,N_col,Real> res;
    
    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	res.component[i][j] = -component[i][j];
      }
    }

    return res;
  }


  template<int N_row,int N_col,typename Real>
  void
  Matrix<N_row,N_col,Real>::
  swap_rows(const int row1,const int row2){
    
    for(int j=0;j<N_col;j++){
      std::swap(component[row1][j],component[row2][j]);
    }    
  }

  template<int N_row,int N_col,typename Real>
  void
  Matrix<N_row,N_col,Real>::
  multiply_row(const int row,const value_type lambda){
    
    for(int j=0;j<N_col;j++){
      component[row][j] *= lambda;
    }    
  }
  
  template<int N_row,int N_col,typename Real>
  Vec<N_col,Real>
  Matrix<N_row,N_col,Real>::
  get_vector_from_row(const int row) const{

    Vec<N_col,Real> res;
    
    for(int j=0;j<N_col;j++){
      res[j] = component[row][j];
    }

    return res;
  }

  
  template<int N_row,int N_col,typename Real>
  void
  Matrix<N_row,N_col,Real>::
  add_vector_to_row(const int row,const Vec<N_col,Real>& vec){
    for(int j=0;j<N_col;j++){
      component[row][j] += vec[j];
    }
  }



/*

  #######################
  # class Square_matrix #
  #######################

*/



  template<int N,typename Real>
  Square_matrix<N,Real>::Square_matrix()
    :Matrix<N,N,Real>(){}


  template<int N,typename Real>
  Square_matrix<N,Real>::Square_matrix(const value_type tab[N][N])
    :Matrix<N,N,Real>(tab){}

  
  template<int N,typename Real>
  Square_matrix<N,Real>::Square_matrix(const Matrix<N,N,Real>& m)
    :Matrix<N,N,Real>(m){}


  template<int N,typename Real>
  typename Square_matrix<N,Real>::value_type
  Square_matrix<N,Real>::trace() const{

    value_type res = 0;

    for(int i=0;i<N;i++) res += (*this)(i,i);

    return res;
  }
  

  template<int N,typename Real>
  Square_matrix<N,Real>
  Square_matrix<N,Real>::
  identity(){
  
    Square_matrix<N,Real> res;

    for(int i=0;i<N;i++){
//       res.component[i][i] = 1;
      res(i,i) = 1;
    }

    return res;
  }




/*

  #########################
  # Fonctions hors classe #
  #########################

*/


  template<int N,typename Real>
  inline typename Vec<N,Real>::value_type operator*(const Vec<N,Real>& v1,
						    const Vec<N,Real>& v2){

    typename Vec<N,Real>::value_type sum = 0;
  
    for(int i=0;i<N;i++){
      sum += v1.coordinate[i] * v2.coordinate[i];
    }

    return sum;
  }


  template<int N,typename Real>
  inline Vec<N,Real> operator+(const Vec<N,Real>& v1,
			       const Vec<N,Real>& v2){

    Vec<N,Real> res(v1);

    res += v2;

    return res;
  }


  template<int N,typename Real>
  inline Vec<N,Real> operator-(const Vec<N,Real>& v1,
			       const Vec<N,Real>& v2){
  
    Vec<N,Real> res(v1);
  
    res -= v2;
  
    return res;
  }


  template<int N,typename Real>
  inline Vec<N,Real> operator*(const Vec<N,Real>& v,
			       const typename Vec<N,Real>::value_type r){

    Vec<N,Real> res(v);
  
    res *= r;
  
    return res;
  }


  template<int N,typename Real>
  inline Vec<N,Real> operator*(const typename Vec<N,Real>::value_type r,
			       const Vec<N,Real>& v){

    Vec<N,Real> res(v);
  
    res *= r;
  
    return res;
  }


  template<int N,typename Real>
  inline Vec<N,Real> operator/(const Vec<N,Real>& v,
			       const typename Vec<N,Real>::value_type r){

    Vec<N,Real> res(v);
  
    res /= r;
  
    return res;
  }


  template<typename Real>
  inline Vec3<Real> operator^(const Vec3<Real>& v1,
			      const Vec3<Real>& v2){

    return Vec3<Real>(v1.y()*v2.z() - v1.z()*v2.y(),
		      v1.z()*v2.x() - v1.x()*v2.z(),
		      v1.x()*v2.y() - v1.y()*v2.x());
  }


  template<int N,typename Real>
  inline std::ostream& operator<<(std::ostream& s,
				  const Vec<N,Real>& v){

    for(int i=0;i<N;i++){
      s<<v.coordinate[i]<<"\t";
    }

    return s;
  }


  template<int N_row,int N_col,typename Real>
  inline std::ostream& operator<<(std::ostream& s,
				  const Matrix<N_row,N_col,Real>& m){
    
    for(int i=0;i<N_row;i++){
      for(int j=0;j<N_col;j++){
	s<<m.component[i][j]<<"\t";
      }
      s<<"\n";
    }

    return s;
  }


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator+(const Matrix<N_row,N_col,Real>& m1,
	    const Matrix<N_row,N_col,Real>& m2){
  
    Matrix<N_row,N_col,Real> res = m1;

    res += m2;

    return res;
  }


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator-(const Matrix<N_row,N_col,Real>& m1,
	    const Matrix<N_row,N_col,Real>& m2){
  
    Matrix<N_row,N_col,Real> res = m1;

    res -= m2;

    return res;
  }


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator*(const Matrix<N_row,N_col,Real>& m,
	    const typename Matrix<N_row,N_col,Real>::value_type lambda){

    Matrix<N_row,N_col,Real> res = m;

    res *= lambda;

    return res;
  }


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator*(const typename Matrix<N_row,N_col,Real>::value_type lambda,
	    const Matrix<N_row,N_col,Real>& m){

    Matrix<N_row,N_col,Real> res = m;

    res *= lambda;

    return res;
  }


  template<int N,int P,int Q,typename Real>
  inline Matrix<N,Q,Real>
  operator*(const Matrix<N,P,Real>& m1,
	    const Matrix<P,Q,Real>& m2){
    int i,j,k;

    Matrix<N,Q,Real> res;

    for(i=0;i<N;i++){
      for(j=0;j<Q;j++){
	res.component[i][j] = 0;
      }
    }

    typename  Matrix<N,P,Real>::value_type scale;
  
    for(j=0;j<Q;j++){
      for(k=0;k<P;k++){
      
	scale = m2.component[k][j];

	for(i=0;i<N;i++){
	  res.component[i][j] += m1.component[i][k] * scale;
	}
      }
    }

    return res;
  }


  template<int N_row,int N_col,typename Real>
  inline Matrix<N_row,N_col,Real>
  operator/(const Matrix<N_row,N_col,Real>& m,
	    const typename Matrix<N_row,N_col,Real>::value_type lambda){

    Matrix<N_row,N_col,Real> res = m;

    res /= lambda;

    return res;
  }


  template <int Row,int Col,typename Real_t>
  inline Vec<Row,Real_t>
  operator*(const Matrix<Row,Col,Real_t>& m,
	    const Vec<Col,Real_t>& v) {
  
    Vec<Row,Real_t> res;

    typename Matrix<Row,Col,Real_t>::value_type scale;
  
    for(int j=0;j<Col;j++){

      scale = v.coordinate[j];

      for(int i=0;i<Row;i++){
	res.coordinate[i] += m.component[i][j] * scale;
      }
    }

    return res;
  }


} // END OF namespace Geometry.

#endif
