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

#ifndef __MIXED_VECTOR__
#define __MIXED_VECTOR__


template <typename T1,typename T2>
class Mixed_vector{

public:
  typedef T1 first_type;
  typedef T2 second_type;
  
  inline Mixed_vector(const first_type&  f = first_type(),
		      const second_type& s = second_type());
    
  //@{
  //! Classical operator.

  inline Mixed_vector<T1,T2>& operator=(const Mixed_vector<T1,T2>& v);
  inline bool operator==(const Mixed_vector<T1,T2>& v) const;
  inline bool operator!=(const Mixed_vector<T1,T2>& v) const;
  inline Mixed_vector<T1,T2>& operator+=(const Mixed_vector<T1,T2>& v);
  inline Mixed_vector<T1,T2>& operator-=(const Mixed_vector<T1,T2>& v);
  
  template <typename Real>
  inline Mixed_vector<T1,T2>& operator*=(const Real r);

  template <typename Real>
  inline Mixed_vector<T1,T2>& operator/=(const Real r);
  
  inline Mixed_vector<T1,T2> operator-() const;
  //@}

  first_type  first;
  second_type second;

};


/*

##########################
# Out of class functions #
##########################

*/

//@{
//! Classical operator.

template <typename T1,typename T2>
inline Mixed_vector<T1,T2> operator+(const Mixed_vector<T1,T2>& v1,
				     const Mixed_vector<T1,T2>& v2);

template <typename T1,typename T2>
inline Mixed_vector<T1,T2> operator-(const Mixed_vector<T1,T2>& v1,
				     const Mixed_vector<T1,T2>& v2);

template <typename T1,typename T2,typename Real>
inline Mixed_vector<T1,T2> operator*(const Mixed_vector<T1,T2>& v,
				     const Real r);

template <typename T1,typename T2,typename Real>
inline Mixed_vector<T1,T2> operator*(const Real r,
				     const Mixed_vector<T1,T2>& v);

template <typename T1,typename T2,typename Real>
inline Mixed_vector<T1,T2> operator/(const Mixed_vector<T1,T2>& v,
				     const Real r);
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



template <typename T1,typename T2>
Mixed_vector<T1,T2>::Mixed_vector(const first_type& f,const second_type& s)
  :first(f),second(s){}


template <typename T1,typename T2>
Mixed_vector<T1,T2>&
Mixed_vector<T1,T2>::operator=(const Mixed_vector<T1,T2>& v){
  first  = v.first;
  second = v.second;
  return *this;
}


template <typename T1,typename T2>
bool Mixed_vector<T1,T2>::operator==(const Mixed_vector<T1,T2>& v) const{

  return (first == v.first) && (second == v.second);
}


template <typename T1,typename T2>
bool Mixed_vector<T1,T2>::operator!=(const Mixed_vector<T1,T2>& v) const{
  return !(*this == v);
}


template <typename T1,typename T2>
Mixed_vector<T1,T2>& Mixed_vector<T1,T2>::operator+=(const Mixed_vector<T1,T2>& v){
  first  += v.first;
  second += v.second;
  return *this;
}


template <typename T1,typename T2>
Mixed_vector<T1,T2>& Mixed_vector<T1,T2>::operator-=(const Mixed_vector<T1,T2>& v){
  first  -= v.first;
  second -= v.second;
  return *this;  
}


template <typename T1,typename T2>
template <typename Real>
Mixed_vector<T1,T2>& Mixed_vector<T1,T2>::operator*=(const Real r){
  first  *= r;
  second *= r;
  return *this;  
}


template <typename T1,typename T2>
template <typename Real>
Mixed_vector<T1,T2>& Mixed_vector<T1,T2>::operator/=(const Real r){
  first  /= r;
  second /= r;
  return *this;  
}


template <typename T1,typename T2>
Mixed_vector<T1,T2> Mixed_vector<T1,T2>::operator-() const{
  Mixed_vector<T1,T2> m(-first,-second);
  return m;
}


template <typename T1,typename T2>
Mixed_vector<T1,T2> operator+(const Mixed_vector<T1,T2>& v1,
			      const Mixed_vector<T1,T2>& v2){
  Mixed_vector<T1,T2> m(v1.first + v2.first,v1.second + v2.second);
  return m;
}


template <typename T1,typename T2>
Mixed_vector<T1,T2> operator-(const Mixed_vector<T1,T2>& v1,
			      const Mixed_vector<T1,T2>& v2){
  Mixed_vector<T1,T2> m(v1.first - v2.first,v1.second - v2.second);
  return m;
}

template <typename T1,typename T2,typename Real>
Mixed_vector<T1,T2> operator*(const Mixed_vector<T1,T2>& v,
			      const Real r){
  Mixed_vector<T1,T2> m(v.first * r,v.second * r);
  return m;  
}

template <typename T1,typename T2,typename Real>
Mixed_vector<T1,T2> operator*(const Real r,
			      const Mixed_vector<T1,T2>& v){
  Mixed_vector<T1,T2> m(r * v.first,r * v.second);
  return m;  
}

template <typename T1,typename T2,typename Real>
Mixed_vector<T1,T2> operator/(const Mixed_vector<T1,T2>& v,
			      const Real r){
  Mixed_vector<T1,T2> m(v.first / r,v.second / r);
  return m;    
}


#endif
