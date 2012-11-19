#pragma once

#include <QString>

class Vector2i;
class Vector3f;

class Vector3i
{
public:

	Vector3i();
	Vector3i( int i ); // fills all 3 components with i
	Vector3i( int x, int y, int z );
	Vector3i( const Vector2i& xy, int z );
	Vector3i( int x, const Vector2i& yz );

	Vector3i( const Vector3i& rv ); // copy constructor
	Vector3i& operator = ( const Vector3i& rv ); // assignment operator
	// no destructor necessary

	// returns the ith element
	const int& operator [] ( int i ) const;
	int& operator [] ( int i );

	Vector2i xy() const;
	Vector2i yz() const;
	Vector2i zx() const;

	Vector2i yx() const;
	Vector2i zy() const;
	Vector2i xz() const;

	// TODO: all the other combinations

	Vector3i xyz() const;
	Vector3i yzx() const;
	Vector3i zxy() const;
	// TODO: all the other combinations

	float abs() const;
	int absSquared() const;
	Vector3f normalized() const;

	void negate();

	// implicit cast
	operator const int* () const;
	operator int* ();
	QString toString() const;

	static int dot( const Vector3i& v0, const Vector3i& v1 );	

	static Vector3i cross( const Vector3i& v0, const Vector3i& v1 );

	static Vector3f lerp( const Vector3i& v0, const Vector3i& v1, float alpha );

	union
	{
		struct
		{
			int x;
			int y;
			int z;
		};
		int m_elements[ 3 ];
	};

};

bool operator == ( const Vector3i& v0, const Vector3i& v1 );
bool operator != ( const Vector3i& v0, const Vector3i& v1 );

Vector3i operator + ( const Vector3i& v0, const Vector3i& v1 );
Vector3i operator - ( const Vector3i& v0, const Vector3i& v1 );
Vector3i operator * ( const Vector3i& v0, const Vector3i& v1 );
Vector3i operator / ( const Vector3i& v0, const Vector3i& v1 );

Vector3i operator - ( const Vector3i& v );
Vector3i operator * ( int c, const Vector3i& v );
Vector3i operator * ( const Vector3i& v, int c );

Vector3f operator * ( float f, const Vector3i& v );
Vector3f operator * ( const Vector3i& v, float f );

Vector3i operator / ( const Vector3i& v, int c );
