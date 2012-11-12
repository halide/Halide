#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "vecmath/Vector4d.h"
#include "vecmath/Vector2d.h"
#include "vecmath/Vector3d.h"

Vector4d::Vector4d()
{
	m_elements[0] = 0;
	m_elements[1] = 0;
	m_elements[2] = 0;
	m_elements[3] = 0;
}

Vector4d::Vector4d( double x, double y, double z, double w )
{
	m_elements[0] = x;
	m_elements[1] = y;
	m_elements[2] = z;
	m_elements[3] = w;
}

Vector4d::Vector4d( const Vector2d& xy, double z, double w )
{
	m_elements[0] = xy.x;
	m_elements[1] = xy.y;
	m_elements[2] = z;
	m_elements[3] = w;
}

Vector4d::Vector4d( double x, const Vector2d& yz, double w )
{
	m_elements[0] = x;
	m_elements[1] = yz.x;
	m_elements[2] = yz.y;
	m_elements[3] = w;
}

Vector4d::Vector4d( double x, double y, const Vector2d& zw )
{
	m_elements[0] = x;
	m_elements[1] = y;
	m_elements[2] = zw.x;
	m_elements[3] = zw.y;
}

Vector4d::Vector4d( const Vector2d& xy, const Vector2d& zw )
{
	m_elements[0] = xy.x;
	m_elements[1] = xy.y;
	m_elements[2] = zw.x;
	m_elements[3] = zw.y;
}

Vector4d::Vector4d( const Vector3d& xyz, double w )
{
	m_elements[0] = xyz.x;
	m_elements[1] = xyz.y;
	m_elements[2] = xyz.z;
	m_elements[3] = w;
}

Vector4d::Vector4d( double x, const Vector3d& yzw )
{
	m_elements[0] = x;
	m_elements[1] = yzw.x;
	m_elements[2] = yzw.y;
	m_elements[3] = yzw.z;
}

Vector4d::Vector4d( const Vector4d& rv )
{
	m_elements[0] = rv.m_elements[0];
	m_elements[1] = rv.m_elements[1];
	m_elements[2] = rv.m_elements[2];
	m_elements[3] = rv.m_elements[3];
}

Vector4d& Vector4d::operator = ( const Vector4d& rv )
{
	if( this != &rv )
	{
		m_elements[0] = rv.m_elements[0];
		m_elements[1] = rv.m_elements[1];
		m_elements[2] = rv.m_elements[2];
		m_elements[3] = rv.m_elements[3];
	}
	return *this;
}

const double& Vector4d::operator [] ( int i ) const
{
	return m_elements[ i % 4 ];
}

double& Vector4d::operator [] ( int i )
{
	return m_elements[ i % 4 ];
}

Vector2d Vector4d::xy() const
{
	return Vector2d( m_elements[0], m_elements[1] );
}

Vector2d Vector4d::yz() const
{
	return Vector2d( m_elements[1], m_elements[2] );
}

Vector2d Vector4d::zw() const
{
	return Vector2d( m_elements[2], m_elements[3] );
}

Vector2d Vector4d::wx() const
{
	return Vector2d( m_elements[3], m_elements[0] );
}

Vector3d Vector4d::xyz() const
{
	return Vector3d( m_elements[0], m_elements[1], m_elements[2] );
}

Vector3d Vector4d::yzw() const
{
	return Vector3d( m_elements[1], m_elements[2], m_elements[3] );
}

Vector3d Vector4d::zwx() const
{
	return Vector3d( m_elements[2], m_elements[3], m_elements[0] );
}

Vector3d Vector4d::wxy() const
{
	return Vector3d( m_elements[3], m_elements[0], m_elements[1] );
}

Vector3d Vector4d::xyw() const
{
	return Vector3d( m_elements[0], m_elements[1], m_elements[3] );
}

Vector3d Vector4d::yzx() const
{
	return Vector3d( m_elements[1], m_elements[2], m_elements[0] );
}

Vector3d Vector4d::zwy() const
{
	return Vector3d( m_elements[2], m_elements[3], m_elements[1] );
}

Vector3d Vector4d::wxz() const
{
	return Vector3d( m_elements[3], m_elements[0], m_elements[2] );
}

double Vector4d::abs() const
{
	return sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] + m_elements[3] * m_elements[3] );
}

double Vector4d::absSquared() const
{
	return( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] + m_elements[3] * m_elements[3] );
}

void Vector4d::normalize()
{
	double norm = sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] + m_elements[3] * m_elements[3] );
	m_elements[0] = m_elements[0] / norm;
	m_elements[1] = m_elements[1] / norm;
	m_elements[2] = m_elements[2] / norm;
	m_elements[3] = m_elements[3] / norm;
}

Vector4d Vector4d::normalized() const
{
	double length = abs();
	return Vector4d
		(
			m_elements[0] / length,
			m_elements[1] / length,
			m_elements[2] / length,
			m_elements[3] / length
		);
}

void Vector4d::homogenize()
{
	if( m_elements[3] != 0 )
	{
		m_elements[0] /= m_elements[3];
		m_elements[1] /= m_elements[3];
		m_elements[2] /= m_elements[3];
		m_elements[3] = 1;
	}
}

Vector4d Vector4d::homogenized() const
{
	if( m_elements[3] != 0 )
	{
		return Vector4d
			(
				m_elements[0] / m_elements[3],
				m_elements[1] / m_elements[3],
				m_elements[2] / m_elements[3],
				1
			);
	}
	else
	{
		return Vector4d
			(
				m_elements[0],
				m_elements[1],
				m_elements[2],
				m_elements[3]
			);
	}
}

void Vector4d::negate()
{
	m_elements[0] = -m_elements[0];
	m_elements[1] = -m_elements[1];
	m_elements[2] = -m_elements[2];
	m_elements[3] = -m_elements[3];
}

// ---- Utility ----

Vector4d::operator const double* ()
{
	return m_elements;
}

void Vector4d::print() const
{
	printf( "< %1.2lf, %1.2lf, %1.2lf, %1.2lf >\n",
		m_elements[0], m_elements[1], m_elements[2], m_elements[3] );
}

// static
double Vector4d::dot( const Vector4d& v0, const Vector4d& v1 )
{
	return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z + v0.w * v1.w;
}

// static
Vector4d Vector4d::lerp( const Vector4d& v0, const Vector4d& v1, double alpha )
{
	return alpha * ( v1 - v0 ) + v0;
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Vector4d operator + ( const Vector4d& v0, const Vector4d& v1 )
{
	return Vector4d( v0.x + v1.x, v0.y + v1.y, v0.z + v1.z, v0.w + v1.w );
}

Vector4d operator - ( const Vector4d& v0, const Vector4d& v1 )
{
	return Vector4d( v0.x - v1.x, v0.y - v1.y, v0.z - v1.z, v0.w - v1.w );
}

Vector4d operator * ( const Vector4d& v0, const Vector4d& v1 )
{
	return Vector4d( v0.x * v1.x, v0.y * v1.y, v0.z * v1.z, v0.w * v1.w );
}

Vector4d operator / ( const Vector4d& v0, const Vector4d& v1 )
{
	return Vector4d( v0.x / v1.x, v0.y / v1.y, v0.z / v1.z, v0.w / v1.w );
}

Vector4d operator - ( const Vector4d& v )
{
	return Vector4d( -v.x, -v.y, -v.z, -v.w );
}

Vector4d operator * ( double d, const Vector4d& v )
{
	return Vector4d( d * v.x, d * v.y, d * v.z, d * v.w );
}

Vector4d operator * ( const Vector4d& v, double d )
{
	return Vector4d( d * v.x, d * v.y, d * v.z, d * v.w );
}
