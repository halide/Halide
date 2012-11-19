#pragma once

#include <QString>

class Vector2f;
class Vector3d;
class Vector3i;

class Vector3f
{
public:

	static const Vector3f ZERO;
	static const Vector3f UP;
	static const Vector3f RIGHT;
	static const Vector3f FORWARD;

    Vector3f() { m_elements[0] = 0; m_elements[1] = 0; m_elements[2] = 0; }
    Vector3f( float f ) { m_elements[0] = m_elements[1] = m_elements[2] = f; }
    Vector3f( float x, float y, float z ) { m_elements[0] = x; m_elements[1] = y; m_elements[2] = z; }

	Vector3f( const Vector2f& xy, float z );
	Vector3f( float x, const Vector2f& yz );

	// copy constructors
    Vector3f( const Vector3f& rv ) { m_elements[0] = rv[0]; m_elements[1] = rv[1]; m_elements[2] = rv[2]; }
	Vector3f( const Vector3d& rv );
	Vector3f( const Vector3i& rv );

	// assignment operators
    Vector3f& operator = ( const Vector3f& rv ) { m_elements[0] = rv[0]; m_elements[1] = rv[1]; m_elements[2] = rv[2]; return *this; }
	Vector3f& operator = ( const Vector3d& rv );
	Vector3f& operator = ( const Vector3i& rv );

	// no destructor necessary

	// returns the ith element
    const float& operator [] ( int i ) const { return m_elements[i]; }
    float& operator [] ( int i ) { return m_elements[i]; }
	
	Vector2f xy() const;
	Vector2f xz() const;
	Vector2f yz() const;
	// TODO: all the other combinations

	Vector3f xyz() const;
	Vector3f yzx() const;
	Vector3f zxy() const;
	// TODO: all the other combinations

	float abs() const;
    float absSquared() const { return( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] ); }

	void normalize();
	Vector3f normalized() const;

	Vector2f homogenized() const;

	void negate();

    operator const float* () const { return m_elements; }
    operator float* () { return m_elements; }
	QString toString() const;

    static float dot( const Vector3f& v0, const Vector3f& v1 ) { return v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2]; }

	static Vector3f cross( const Vector3f& v0, const Vector3f& v1 )
    {
        return Vector3f
        (
			v0.y * v1.z - v0.z * v1.y,
			v0.z * v1.x - v0.x * v1.z,
			v0.x * v1.y - v0.y * v1.x
        );
    }


	// returns v0 * ( 1 - alpha ) * v1 * alpha
	static Vector3f lerp( const Vector3f& v0, const Vector3f& v1, float alpha );

	// catmull-rom interpolation
	static Vector3f cubicInterpolate( const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, const Vector3f& p3, float t );

	inline Vector3f& operator += ( const Vector3f& v );
	inline Vector3f& operator -= ( const Vector3f& v );
    inline Vector3f& operator *= ( float f );
	inline Vector3f& operator /= ( float f );

	union
	{
		struct
		{
			float x;
			float y;
			float z;
		};
		float m_elements[3];
	};

};

inline Vector3f operator + ( const Vector3f& v0, const Vector3f& v1 ) { return Vector3f( v0[0] + v1[0], v0[1] + v1[1], v0[2] + v1[2] ); }
inline Vector3f operator - ( const Vector3f& v0, const Vector3f& v1 ) { return Vector3f( v0[0] - v1[0], v0[1] - v1[1], v0[2] - v1[2] ); }
inline Vector3f operator * ( const Vector3f& v0, const Vector3f& v1 ) { return Vector3f( v0[0] * v1[0], v0[1] * v1[1], v0[2] * v1[2] ); }

// component-wise division
// TODO: do it for the other classes
inline Vector3f operator / ( const Vector3f& v0, const Vector3f& v1 ) { return Vector3f( v0[0] / v1[0], v0[1] / v1[1], v0[2] / v1[2] ); }
inline Vector3f operator / ( const Vector3f& v, float f ) { return Vector3f( v[0] / f, v[1] / f, v[2] / f ); }

inline Vector3f operator - ( const Vector3f& v ) { return Vector3f( -v[0], -v[1], -v[2] ); }
inline Vector3f operator * ( float f, const Vector3f& v ) { return Vector3f( v[0] * f, v[1] * f, v[2] * f ); }
inline Vector3f operator * ( const Vector3f& v, float f ) { return Vector3f( v[0] * f, v[1] * f, v[2] * f ); }

inline Vector3f& Vector3f::operator += ( const Vector3f& v )
{
	m_elements[ 0 ] += v.m_elements[ 0 ];
	m_elements[ 1 ] += v.m_elements[ 1 ];
	m_elements[ 2 ] += v.m_elements[ 2 ];

	return *this;
}

inline Vector3f& Vector3f::operator -= ( const Vector3f& v )
{
	m_elements[ 0 ] -= v.m_elements[ 0 ];
	m_elements[ 1 ] -= v.m_elements[ 1 ];
	m_elements[ 2 ] -= v.m_elements[ 2 ];

	return *this;
}

inline Vector3f& Vector3f::operator *= ( float f )
{
	m_elements[ 0 ] *= f;
	m_elements[ 1 ] *= f;
	m_elements[ 2 ] *= f;

	return *this;
}

inline Vector3f& Vector3f::operator /= ( float f )
{
	m_elements[ 0 ] /= f;
	m_elements[ 1 ] /= f;
	m_elements[ 2 ] /= f;

	return *this;
}
