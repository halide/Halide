#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "math/Arithmetic.h"
#include "vecmath/Vector2d.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector3d.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Vector2d::Vector2d()
{
	m_elements[0] = 0;
	m_elements[1] = 0;
}

Vector2d::Vector2d( double x, double y )
{
	m_elements[0] = x;
	m_elements[1] = y;
}

Vector2d::Vector2d( const Vector2d& rv )
{
	m_elements[0] = rv.m_elements[0];
	m_elements[1] = rv.m_elements[1];
}

Vector2d& Vector2d::operator = ( const Vector2d& rv )
{
	if( this != &rv )
	{
		m_elements[0] = rv.m_elements[0];
		m_elements[1] = rv.m_elements[1];
	}
	return *this;
}

const double& Vector2d::operator [] ( int i ) const
{
	return m_elements[ i ];
}

double& Vector2d::operator [] ( int i )
{
	return m_elements[ i ];
}

Vector2d Vector2d::xy() const
{
	return Vector2d( m_elements[0], m_elements[1] );
}

Vector2d Vector2d::yx() const
{
	return Vector2d( m_elements[1], m_elements[0] );
}

Vector2d Vector2d::xx() const
{
	return Vector2d( m_elements[0], m_elements[0] );
}

Vector2d Vector2d::yy() const
{
	return Vector2d( m_elements[1], m_elements[1] );
}

double Vector2d::abs() const
{
	return sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] );
}

double Vector2d::absSquared() const
{
	return( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] );
}

void Vector2d::normalize()
{
	double norm = sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] );
	m_elements[0] = m_elements[0] / norm;
	m_elements[1] = m_elements[1] / norm;
}

Vector2d Vector2d::normalized() const
{
	double length = abs();
	return Vector2d( m_elements[0] / length, m_elements[1] / length );
}

void Vector2d::negate()
{
	m_elements[0] = -m_elements[0];
	m_elements[1] = -m_elements[1];
}

Vector2i Vector2d::floored() const
{
	return Vector2i( Arithmetic::floorToInt( m_elements[ 0 ] ), Arithmetic::floorToInt( m_elements[ 1 ] ) );
}

// ---- Utility ----
Vector2d::operator const double* ()
{
	return m_elements;
}

void Vector2d::print() const
{
	printf( "< %1.2lf, %1.2lf >\n",
		m_elements[0], m_elements[1] );
}

// static
double Vector2d::dot( const Vector2d& v0, const Vector2d& v1 )
{
	return v0.x * v1.x + v0.y * v1.y;
}

//static
Vector3d Vector2d::cross( const Vector2d& v0, const Vector2d& v1 )
{
	return Vector3d
		(
			0,
			0,
			v0.x * v1.y - v0.y * v1.x
		);
}

// static
Vector2d Vector2d::lerp( const Vector2d& v0, const Vector2d& v1, double alpha )
{
	return alpha * ( v1 - v0 ) + v0;
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Vector2d operator + ( const Vector2d& v0, const Vector2d& v1 )
{
	return Vector2d( v0.x + v1.x, v0.y + v1.y );
}

Vector2d operator - ( const Vector2d& v0, const Vector2d& v1 )
{
	return Vector2d( v0.x - v1.x, v0.y - v1.y );
}

Vector2d operator * ( const Vector2d& v0, const Vector2d& v1 )
{
	return Vector2d( v0.x * v1.x, v0.y * v1.y );
}

Vector2d operator / ( const Vector2d& v0, const Vector2d& v1 )
{
	return Vector2d( v0.x / v1.x, v0.y / v1.y );
}

Vector2d operator - ( const Vector2d& v )
{
	return Vector2d( -v.x, -v.y );
}

Vector2d operator * ( double d, const Vector2d& v )
{
	return Vector2d( d * v.x, d * v.y );
}

Vector2d operator * ( const Vector2d& v, double d )
{
	return Vector2d( d * v.x, d * v.y );
}
