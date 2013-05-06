#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "vecmath/Vector3f.h"
#include "vecmath/Vector3d.h"
#include "vecmath/Vector3i.h"
#include "vecmath/Vector2f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
const Vector3f Vector3f::ZERO = Vector3f( 0, 0, 0 );

// static
const Vector3f Vector3f::UP = Vector3f( 0, 1, 0 );

// static
const Vector3f Vector3f::RIGHT = Vector3f( 1, 0, 0 );

// static
const Vector3f Vector3f::FORWARD = Vector3f( 0, 0, -1 );

Vector3f::Vector3f( const Vector2f& xy, float z )
{
	m_elements[0] = xy.x;
	m_elements[1] = xy.y;
	m_elements[2] = z;
}

Vector3f::Vector3f( float x, const Vector2f& yz )
{
	m_elements[0] = x;
	m_elements[1] = yz.x;
	m_elements[2] = yz.y;
}

Vector3f::Vector3f( const Vector3d& rv )
{
	m_elements[0] = static_cast< float >( rv.x );
	m_elements[1] = static_cast< float >( rv.y );
	m_elements[2] = static_cast< float >( rv.z );
}

Vector3f::Vector3f( const Vector3i& rv )
{
	m_elements[0] = static_cast< float >( rv.x );
	m_elements[1] = static_cast< float >( rv.y );
	m_elements[2] = static_cast< float >( rv.z );
}

Vector3f& Vector3f::operator = ( const Vector3d& rv )
{
	m_elements[ 0 ] = static_cast< float >( rv.x );
	m_elements[ 1 ] = static_cast< float >( rv.y );
	m_elements[ 2 ] = static_cast< float >( rv.z );

	return *this;
}

Vector3f& Vector3f::operator = ( const Vector3i& rv )
{
	m_elements[ 0 ] = static_cast< float >( rv.x );
	m_elements[ 1 ] = static_cast< float >( rv.y );
	m_elements[ 2 ] = static_cast< float >( rv.z );

	return *this;
}

Vector2f Vector3f::xy() const
{
	return Vector2f( m_elements[0], m_elements[1] );
}

Vector2f Vector3f::xz() const
{
	return Vector2f( m_elements[0], m_elements[2] );
}

Vector2f Vector3f::yz() const
{
	return Vector2f( m_elements[1], m_elements[2] );
}

Vector3f Vector3f::xyz() const
{
	return Vector3f( m_elements[0], m_elements[1], m_elements[2] );
}

Vector3f Vector3f::yzx() const
{
	return Vector3f( m_elements[1], m_elements[2], m_elements[0] );
}

Vector3f Vector3f::zxy() const
{
	return Vector3f( m_elements[2], m_elements[0], m_elements[1] );
}

float Vector3f::abs() const
{
	return sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] );
}

void Vector3f::normalize()
{
	float norm = sqrt( m_elements[0] * m_elements[0] + m_elements[1] * m_elements[1] + m_elements[2] * m_elements[2] );
	m_elements[0] = m_elements[0] / norm;
	m_elements[1] = m_elements[1] / norm;
	m_elements[2] = m_elements[2] / norm;
}

Vector3f Vector3f::normalized() const
{
	float length = abs();
	return Vector3f
		(
			m_elements[0] / length,
			m_elements[1] / length,
			m_elements[2] / length
		);
}

Vector2f Vector3f::homogenized() const
{
	return Vector2f
		(
			m_elements[ 0 ] / m_elements[ 2 ],
			m_elements[ 1 ] / m_elements[ 2 ]
		);
}

void Vector3f::negate()
{
	m_elements[0] = -m_elements[0];
	m_elements[1] = -m_elements[1];
	m_elements[2] = -m_elements[2];
}

QString Vector3f::toString() const
{
	QString out;

	out.append( "( " );
	out.append( QString( "%1" ).arg( x, 10, 'g', 4 ) );
	out.append( QString( "%1" ).arg( y, 10, 'g', 4 ) );
	out.append( QString( "%1" ).arg( z, 10, 'g', 4 ) );
	out.append( " )" );

	return out;
}

// static
Vector3f Vector3f::lerp( const Vector3f& v0, const Vector3f& v1, float alpha )
{
	return alpha * ( v1 - v0 ) + v0;
}

// static
Vector3f Vector3f::cubicInterpolate( const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, const Vector3f& p3, float t )
{
	// geometric construction:
	//            t
	//   (t+1)/2     t/2
	// t+1        t	        t-1

	// bottom level
	Vector3f p0p1 = Vector3f::lerp( p0, p1, t + 1 );
	Vector3f p1p2 = Vector3f::lerp( p1, p2, t );
	Vector3f p2p3 = Vector3f::lerp( p2, p3, t - 1 );

	// middle level
	Vector3f p0p1_p1p2 = Vector3f::lerp( p0p1, p1p2, 0.5f * ( t + 1 ) );
	Vector3f p1p2_p2p3 = Vector3f::lerp( p1p2, p2p3, 0.5f * t );

	// top level
	return Vector3f::lerp( p0p1_p1p2, p1p2_p2p3, t );
}

