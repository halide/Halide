#include "vecmath/Vector3d.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "vecmath/Vector2d.h"
#include "vecmath/Vector3f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Vector3d::Vector3d()
{
	m_elements[0] = 0;
	m_elements[1] = 0;
	m_elements[2] = 0;
}

Vector3d::Vector3d( double x, double y, double z )
{
	m_elements[0] = x;
	m_elements[1] = y;
	m_elements[2] = z;
}

Vector3d::Vector3d( const Vector2d& xy, double z )
{
	m_elements[0] = xy.x;
	m_elements[1] = xy.y;
	m_elements[2] = z;
}

Vector3d::Vector3d( double x, const Vector2d& yz )
{
	m_elements[0] = x;
	m_elements[1] = yz.x;
	m_elements[2] = yz.y;
}

Vector3d::Vector3d( const Vector3d& rv )
{
	m_elements[0] = rv.m_elements[0];
	m_elements[1] = rv.m_elements[1];
	m_elements[2] = rv.m_elements[2];
}

Vector3d::Vector3d( const Vector3f& rv )
{
	m_elements[0] = rv[0];
	m_elements[1] = rv[1];
	m_elements[2] = rv[2];
}

Vector3d& Vector3d::operator = ( const Vector3d& rv )
{
	if( this != &rv )
	{
		m_elements[0] = rv.m_elements[0];
		m_elements[1] = rv.m_elements[1];
		m_elements[2] = rv.m_elements[2];
	}
	return *this;
}

const double& Vector3d::operator [] ( int i ) const
{
	return m_elements[ i ];
}

double& Vector3d::operator [] ( int i )
{
	return m_elements[ i ];
}

Vector2d Vector3d::xy() const
{
	return Vector2d( m_elements[0], m_elements[1] );
}

Vector2d Vector3d::xz() const
{
	return Vector2d( m_elements[0], m_elements[2] );
}

Vector2d Vector3d::yz() const
{
	return Vector2d( m_elements[1], m_elements[2] );
}

Vector3d Vector3d::xyz() const
{
	return Vector3d( m_elements[0], m_elements[1], m_elements[2] );
}

Vector3d Vector3d::yzx() const
{
	return Vector3d( m_elements[1], m_elements[2], m_elements[0] );
}

Vector3d Vector3d::zxy() const
{
	return Vector3d( m_elements[2], m_elements[0], m_elements[1] );
}

double Vector3d::abs() const
{
	return sqrt( absSquared() );
}

double Vector3d::absSquared() const
{
	return( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] );
}

void Vector3d::normalize()
{
	double norm = sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] );
	m_elements[0] = m_elements[0] / norm;
	m_elements[1] = m_elements[1] / norm;
	m_elements[2] = m_elements[2] / norm;
}

Vector3d Vector3d::normalized() const
{
	double length = abs();
	return Vector3d
		(
			m_elements[0] / length,
			m_elements[1] / length,
			m_elements[2] / length
		);
}

void Vector3d::negate()
{
	m_elements[0] = -m_elements[0];
	m_elements[1] = -m_elements[1];
	m_elements[2] = -m_elements[2];
}

// ---- Utility ----

Vector3d::operator const double* ()
{
	return m_elements;
}

void Vector3d::print() const
{
	printf( "< %1.2lf, %1.2lf, %1.2lf >\n",
		m_elements[0], m_elements[1], m_elements[2] );
}

// static
double Vector3d::dot( const Vector3d& v0, const Vector3d& v1 )
{
	return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
}

// static
Vector3d Vector3d::cross( const Vector3d& v0, const Vector3d& v1 )
{
	return Vector3d
		(
			v0.y * v1.z - v0.z * v1.y,
			v0.z * v1.x - v0.x * v1.z,
			v0.x * v1.y - v0.y * v1.x
		);
}

// static
Vector3d Vector3d::lerp( const Vector3d& v0, const Vector3d& v1, double alpha )
{
	return alpha * ( v1 - v0 ) + v0;
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Vector3d operator + ( const Vector3d& v0, const Vector3d& v1 )
{
	return Vector3d( v0.x + v1.x, v0.y + v1.y, v0.z + v1.z );
}

Vector3d operator - ( const Vector3d& v0, const Vector3d& v1 )
{
	return Vector3d( v0.x - v1.x, v0.y - v1.y, v0.z - v1.z );
}

Vector3d operator * ( const Vector3d& v0, const Vector3d& v1 )
{
	return Vector3d( v0.x * v1.x, v0.y * v1.y, v0.z * v1.z );
}

Vector3d operator / ( const Vector3d& v0, const Vector3d& v1 )
{
	return Vector3d( v0.x / v1.x, v0.y / v1.y, v0.z / v1.z );
}

Vector3d operator - ( const Vector3d& v )
{
	return Vector3d( -v.x, -v.y, -v.z );
}

Vector3d operator * ( double d, const Vector3d& v )
{
	return Vector3d( d * v.x, d * v.y, d * v.z );
}

Vector3d operator * ( const Vector3d& v, double d )
{
	return Vector3d( d * v.x, d * v.y, d * v.z );
}
