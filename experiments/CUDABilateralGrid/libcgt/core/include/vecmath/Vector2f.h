#ifndef VECTOR_2F_H
#define VECTOR_2F_H

#include <cmath>
#include <QString>

class Vector2d;
class Vector2i;
class Vector3f;

class Vector2f
{
public:

	// TODO: conversion operators for double <--> float on Vector3f and Vector4f

    Vector2f() { m_elements[0] = 0; m_elements[1] = 0; }
    Vector2f( float f ) { m_elements[0] = f; m_elements[1] = f; }
    Vector2f( float x, float y ) { m_elements[0] = x; m_elements[1] = y; }

	// copy constructors
    Vector2f( const Vector2f& rv )
	{
		x = rv.x;
		y = rv.y;
	}

	Vector2f( const Vector2d& rv );
	Vector2f( const Vector2i& rv );

	// assignment operators
	Vector2f& operator = ( const Vector2f& rv )
	{
		if( this != &rv )
		{
			x = rv.x;
			y = rv.y;
		}
		return *this;
	}
	Vector2f& operator = ( const Vector2d& rv );
	Vector2f& operator = ( const Vector2i& rv );

	// no destructor necessary

	// returns the ith element
    const float& operator [] ( int i ) const { return m_elements[i]; }
	float& operator [] ( int i ) { return m_elements[i]; }

    Vector2f xy() const { return *this; }
	Vector2f yx() const { return Vector2f( y, x ); }
	Vector2f xx() const { return Vector2f( x, x ); }
	Vector2f yy() const { return Vector2f( y, y ); }

	// returns ( -y, x )
	Vector2f normal() const { return Vector2f( -y, x ); }

    float abs() const { return sqrt(absSquared()); }
    float absSquared() const { return m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1]; }
    void normalize() { float norm = abs(); m_elements[0] /= norm; m_elements[1] /= norm; }
    Vector2f normalized() const { float norm = abs(); return Vector2f( m_elements[0] / norm, m_elements[1] / norm ); }

    void negate() { m_elements[0] = -m_elements[0]; m_elements[1] = -m_elements[1]; }

	Vector2i floored() const;

	// ---- Utility ----
	// TODO: make the rest const correct
    operator const float* () const { return m_elements; } // automatic type conversion for GL
    operator float* () { return m_elements; } // automatic type conversion for Direct3D
	void print() const;
	QString toString() const;

    static float dot( const Vector2f& v0, const Vector2f& v1 ) { return v0[0] * v1[0] + v0[1] * v1[1]; }

	static Vector3f cross( const Vector2f& v0, const Vector2f& v1 );

	// returns v0 * ( 1 - alpha ) * v1 * alpha
	static Vector2f lerp( const Vector2f& v0, const Vector2f& v1, float alpha );

	inline Vector2f& operator+=( const Vector2f& );

	union
	{
		struct
		{
			float x;
			float y;
		};
		float m_elements[ 2 ];
	};
};

inline Vector2f operator + ( const Vector2f& v0, const Vector2f& v1 ) { return Vector2f( v0.x + v1.x, v0.y + v1.y ); }
inline Vector2f operator - ( const Vector2f& v0, const Vector2f& v1 ) { return Vector2f( v0.x - v1.x, v0.y - v1.y ); }
inline Vector2f operator * ( const Vector2f& v0, const Vector2f& v1 ) { return Vector2f( v0.x * v1.x, v0.y * v1.y ); }

inline Vector2f& Vector2f::operator+=( const Vector2f& addend )
{
	m_elements[ 0 ] += addend.m_elements[ 0 ];
	m_elements[ 1 ] += addend.m_elements[ 1 ];
	return *this;
}

// component-wise division
inline Vector2f operator / ( const Vector2f& v0, const Vector2f& v1 ) { return Vector2f( v0.x * v1.x, v0.y * v1.y ); }

inline Vector2f operator - ( const Vector2f& v ) { return Vector2f(-v.x, -v.y); }
inline Vector2f operator * ( float f, const Vector2f& v ) { return Vector2f(f * v.x, f * v.y); }
inline Vector2f operator * ( const Vector2f& v, float f ) { return Vector2f(f * v.x, f * v.y); }

inline bool operator == ( const Vector2f& v0, const Vector2f& v1 ) { return( v0.x == v1.x && v0.y == v1.y ); }
inline bool operator != ( const Vector2f& v0, const Vector2f& v1 ) { return !( v0 == v1 ); }

#endif // VECTOR_2F_H
