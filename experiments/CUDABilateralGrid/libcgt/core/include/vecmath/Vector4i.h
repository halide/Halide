#pragma once

#include <QString>

class Vector2i;
class Vector3i;
class Vector4f;

class Vector4i
{
public:	

	Vector4i();
	Vector4i( int i ); // fills all 4 components with i
	Vector4i( int x, int y, int z, int w );
	Vector4i( const Vector2i& xy, int z, int w );
	Vector4i( int x, const Vector2i& yz, int w );
	Vector4i( int x, int y, const Vector2i& zw );
	Vector4i( const Vector2i& xy, const Vector2i& zw );
	Vector4i( const Vector3i& xyz, int w );
	Vector4i( int x, const Vector3i& yzw );

	Vector4i( const Vector4i& rv ); // copy constructor	
	Vector4i& operator = ( const Vector4i& rv ); // assignment operator
	// no destructor necessary

	// returns the ith element
	const int& operator [] ( int i ) const;
	int& operator [] ( int i );
	
	Vector2i xy() const;
	Vector2i yz() const;
	Vector2i zw() const;
	Vector2i wx() const;
	// TODO: the other combinations

	Vector3i xyz() const;
	Vector3i yzw() const;
	Vector3i zwx() const;
	Vector3i wxy() const;

	Vector3i xyw() const;
	Vector3i yzx() const;
	Vector3i zwy() const;
	Vector3i wxz() const;
	// TODO: the rest of the vec3 combinations

	// TODO: swizzle all the vec4s

	float abs() const;
	int absSquared() const;
	Vector4f normalized() const;

	// if v.z != 0, v = v / v.w
	void homogenize();
	Vector4i homogenized() const;

	void negate();
	
	// implicit cast
	operator const int* () const;
	operator int* ();
	QString toString() const;

	Vector4i& operator += ( const Vector4i& v );
	Vector4i& operator -= ( const Vector4i& v );
	Vector4i& operator *= ( int i );
	Vector4i& operator /= ( int i );


	static int dot( const Vector4i& v0, const Vector4i& v1 );
	static Vector4f lerp( const Vector4i& v0, const Vector4i& v1, float alpha );

	union
	{
		struct
		{
			int x;
			int y;
			int z;
			int w;
		};
		int m_elements[ 4 ];
	};

};

Vector4i operator + ( const Vector4i& v0, const Vector4i& v1 );
Vector4i operator - ( const Vector4i& v0, const Vector4i& v1 );
Vector4i operator * ( const Vector4i& v0, const Vector4i& v1 );
Vector4i operator / ( const Vector4i& v0, const Vector4i& v1 );

Vector4i operator - ( const Vector4i& v );
Vector4i operator * ( int c, const Vector4i& v );
Vector4i operator * ( const Vector4i& v, int c );

Vector4f operator * ( float f, const Vector4i& v );
Vector4f operator * ( const Vector4i& v, float f );

Vector4i operator / ( const Vector4i& v, int c );
