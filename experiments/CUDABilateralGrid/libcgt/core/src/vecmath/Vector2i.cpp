#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "vecmath/Vector2i.h"
#include "vecmath/Vector2f.h"
#include "vecmath/Vector3i.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Vector2i::Vector2i()
{
	m_elements[0] = 0;
	m_elements[1] = 0;
}

Vector2i::Vector2i( int i )
{
	m_elements[0] = i;
	m_elements[1] = i;
}

Vector2i::Vector2i( int x, int y )
{
	m_elements[0] = x;
	m_elements[1] = y;
}

Vector2i::Vector2i( const Vector2i& rv )
{
	m_elements[0] = rv.m_elements[0];
	m_elements[1] = rv.m_elements[1];
}

Vector2i& Vector2i::operator = ( const Vector2i& rv )
{
	if( this != &rv )
	{
		m_elements[0] = rv.m_elements[0];
		m_elements[1] = rv.m_elements[1];
	}
	return *this;
}

const int& Vector2i::operator [] ( int i ) const
{
	return m_elements[ i ];
}

int& Vector2i::operator [] ( int i )
{
	return m_elements[ i ];
}

Vector2i Vector2i::xy() const
{
	return Vector2i( m_elements[0], m_elements[1] );
}

Vector2i Vector2i::yx() const
{
	return Vector2i( m_elements[1], m_elements[0] );
}

Vector2i Vector2i::xx() const
{
	return Vector2i( m_elements[0], m_elements[0] );
}

Vector2i Vector2i::yy() const
{
	return Vector2i( m_elements[1], m_elements[1] );
}

float Vector2i::abs() const
{
	return sqrt( static_cast< float >( absSquared() ) );
}

int Vector2i::absSquared() const
{
	return( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] );
}

Vector2f Vector2i::normalized() const
{
	float rLength = 1.f / abs();

	return Vector2f
	(
		rLength * m_elements[ 0 ],
		rLength * m_elements[ 1 ]
	);
}

void Vector2i::negate()
{
	m_elements[0] = -m_elements[0];
	m_elements[1] = -m_elements[1];
}

Vector2i::operator const int* () const
{
	return m_elements;
}

Vector2i::operator int* ()
{
	return m_elements;
}

QString Vector2i::toString() const
{
	QString out;

	out.append( "( " );
	out.append( QString( "%1" ).arg( x, 10 ) );
	out.append( QString( "%1" ).arg( y, 10 ) );
	out.append( " )" );

	return out;
}

// static
int Vector2i::dot( const Vector2i& v0, const Vector2i& v1 )
{
	return v0.x * v1.x + v0.y * v1.y;
}

//static
Vector3i Vector2i::cross( const Vector2i& v0, const Vector2i& v1 )
{
	return Vector3i
	(
		0,
		0,
		v0.x * v1.y - v0.y * v1.x
	);
}

// static
Vector2f Vector2i::lerp( const Vector2i& v0, const Vector2i& v1, float alpha )
{
	return alpha * ( v1 - v0 ) + Vector2f( v0 );
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

bool operator == ( const Vector2i& v0, const Vector2i& v1 )
{
	return
	(
		v0.x == v1.x &&
		v0.y == v1.y
	);
}

Vector2i operator + ( const Vector2i& v0, const Vector2i& v1 )
{
	return Vector2i( v0.x + v1.x, v0.y + v1.y );
}

Vector2i operator - ( const Vector2i& v0, const Vector2i& v1 )
{
	return Vector2i( v0.x - v1.x, v0.y - v1.y );
}

Vector2i operator * ( const Vector2i& v0, const Vector2i& v1 )
{
	return Vector2i( v0.x * v1.x, v0.y * v1.y );
}

Vector2i operator / ( const Vector2i& v0, const Vector2i& v1 )
{
	return Vector2i( v0.x / v1.x, v0.y / v1.y );
}

Vector2i operator - ( const Vector2i& v )
{
	return Vector2i( -v.x, -v.y );
}

Vector2i operator * ( int c, const Vector2i& v )
{
	return Vector2i( c * v.x, c * v.y );
}

Vector2i operator * ( const Vector2i& v, int c )
{
	return Vector2i( c * v.x, c * v.y );
}

Vector2f operator * ( float f, const Vector2i& v )
{
	return Vector2f( f * v.x, f * v.y );
}

Vector2f operator * ( const Vector2i& v, float f )
{
	return Vector2f( f * v.x, f * v.y );
}

Vector2i operator / ( const Vector2i& v, int c )
{
	return Vector2i( v.x / c, v.y / c );
}

uint qHash( const Vector2i& v )
{
	return v.x ^ v.y;
}
