#pragma once

#include <QString>

class Vector2f;
class Vector3f;

class Vector4i;
class Vector4d;

class Vector4f
{
public:

	Vector4f();
	Vector4f( float f );
	Vector4f( float fx, float fy, float fz, float fw );
	Vector4f( float buffer[ 4 ] );

	Vector4f( const Vector2f& xy, float z, float w );
	Vector4f( float x, const Vector2f& yz, float w );
	Vector4f( float x, float y, const Vector2f& zw );
	Vector4f( const Vector2f& xy, const Vector2f& zw );

	Vector4f( const Vector3f& xyz, float w );
	Vector4f( float x, const Vector3f& yzw );

	// copy constructors
	Vector4f( const Vector4f& rv );
	Vector4f( const Vector4d& rv );
	Vector4f( const Vector4i& rv );

	// assignment operators
	Vector4f& operator = ( const Vector4f& rv );
	Vector4f& operator = ( const Vector4d& rv );
	Vector4f& operator = ( const Vector4i& rv );

	// no destructor necessary	

	// returns the ith element
	const float& operator [] ( int i ) const;
	float& operator [] ( int i );
	
	Vector2f xy() const;
	Vector2f yz() const;
	Vector2f zw() const;
	Vector2f wx() const;
	// TODO: the other combinations

	Vector3f xyz() const;
	Vector3f yzw() const;
	Vector3f zwx() const;
	Vector3f wxy() const;

	Vector3f xyw() const;
	Vector3f yzx() const;
	Vector3f zwy() const;
	Vector3f wxz() const;
	// TODO: the rest of the vec3 combinations

	// TODO: swizzle all the vec4s

	float abs() const;
	float absSquared() const;
	void normalize();
	Vector4f normalized() const;

	// if v.z != 0, v = v / v.w
	void homogenize();
	Vector4f homogenized() const;

	void negate();

	// implicit cast
	operator const float* () const;
	operator float* ();
	QString toString() const;

	static float dot( const Vector4f& v0, const Vector4f& v1 );
	static Vector4f lerp( const Vector4f& v0, const Vector4f& v1, float alpha );

	inline Vector4f& operator += ( const Vector4f& v );
	inline Vector4f& operator -= ( const Vector4f& v );
    inline Vector4f& operator *= ( float f );
	inline Vector4f& operator /= ( float f );

	union
	{
		struct
		{
			float x;
			float y;
			float z;
			float w;
		};
		float m_elements[ 4 ];
	};
};

inline Vector4f operator + ( const Vector4f& v0, const Vector4f& v1 )
{
	return Vector4f( v0.x + v1.x, v0.y + v1.y, v0.z + v1.z, v0.w + v1.w );
}

inline Vector4f operator - ( const Vector4f& v0, const Vector4f& v1 )
{
	return Vector4f( v0.x - v1.x, v0.y - v1.y, v0.z - v1.z, v0.w - v1.w );
}

inline Vector4f operator * ( const Vector4f& v0, const Vector4f& v1 )
{
	return Vector4f( v0.x * v1.x, v0.y * v1.y, v0.z * v1.z, v0.w * v1.w );
}

inline Vector4f operator - ( const Vector4f& v ) { return Vector4f( -v[0], -v[1], -v[2] , -v[3] ); }
inline Vector4f operator * ( float f, const Vector4f& v ) { return Vector4f( v[0] * f, v[1] * f, v[2] * f, v[3] * f ); }
inline Vector4f operator * ( const Vector4f& v, float f ) { return Vector4f( v[0] * f, v[1] * f, v[2] * f, v[3] * f ); }

inline Vector4f operator / ( const Vector4f& v0, const Vector4f& v1 ) { return Vector4f( v0[0] / v1[0], v0[1] / v1[1], v0[2] / v1[2], v0[3] / v1[3] ); }
inline Vector4f operator / ( const Vector4f& v, float f ) { return Vector4f( v[0] / f, v[1] / f, v[2] / f, v[3] / f ); }

inline Vector4f& Vector4f::operator += ( const Vector4f& v )
{
	m_elements[ 0 ] += v.m_elements[ 0 ];
	m_elements[ 1 ] += v.m_elements[ 1 ];
	m_elements[ 2 ] += v.m_elements[ 2 ];
	m_elements[ 3 ] += v.m_elements[ 3 ];

	return *this;
}

inline Vector4f& Vector4f::operator -= ( const Vector4f& v )
{
	m_elements[ 0 ] -= v.m_elements[ 0 ];
	m_elements[ 1 ] -= v.m_elements[ 1 ];
	m_elements[ 2 ] -= v.m_elements[ 2 ];
	m_elements[ 3 ] -= v.m_elements[ 3 ];

	return *this;
}

inline Vector4f& Vector4f::operator *= ( float f )
{
	m_elements[ 0 ] *= f;
	m_elements[ 1 ] *= f;
	m_elements[ 2 ] *= f;
	m_elements[ 3 ] *= f;

	return *this;
}

inline Vector4f& Vector4f::operator /= ( float f )
{
	m_elements[ 0 ] /= f;
	m_elements[ 1 ] /= f;
	m_elements[ 2 ] /= f;
	m_elements[ 3 ] /= f;

	return *this;
}
