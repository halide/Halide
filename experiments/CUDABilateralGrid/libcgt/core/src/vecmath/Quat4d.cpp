#include "vecmath/Quat4d.h"

#include <cmath>
#include <cstdio>

#include <math/MathUtils.h>

#include "vecmath/Quat4f.h"
#include "vecmath/Vector3d.h"
#include "vecmath/Vector4d.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Quat4d::Quat4d()
{
	m_elements[ 0 ] = 0;
	m_elements[ 1 ] = 0;
	m_elements[ 2 ] = 0;
	m_elements[ 3 ] = 0;
}

Quat4d::Quat4d( double w, double x, double y, double z )
{
	m_elements[ 0 ] = w;
	m_elements[ 1 ] = x;
	m_elements[ 2 ] = y;
	m_elements[ 3 ] = z;
}

Quat4d::Quat4d( const Quat4d& rq )
{
	m_elements[ 0 ] = rq.m_elements[ 0 ];
	m_elements[ 1 ] = rq.m_elements[ 1 ];
	m_elements[ 2 ] = rq.m_elements[ 2 ];
	m_elements[ 3 ] = rq.m_elements[ 3 ];
}

Quat4d::Quat4d( const Quat4f& rq )
{
	m_elements[ 0 ] = rq[ 0 ];
	m_elements[ 1 ] = rq[ 1 ];
	m_elements[ 2 ] = rq[ 2 ];
	m_elements[ 3 ] = rq[ 3 ];
}

Quat4d& Quat4d::operator = ( const Quat4d& rq )
{
	if( this != ( &rq ) )
	{
		m_elements[ 0 ] = rq.m_elements[ 0 ];
		m_elements[ 1 ] = rq.m_elements[ 1 ];
		m_elements[ 2 ] = rq.m_elements[ 2 ];
		m_elements[ 3 ] = rq.m_elements[ 3 ];
	}
	return( *this );
}

Quat4d::Quat4d( const Vector3d& v )
{
	m_elements[ 0 ] = 0;
	m_elements[ 1 ] = v[ 0 ];
	m_elements[ 2 ] = v[ 1 ];
	m_elements[ 3 ] = v[ 2 ];
}

Quat4d::Quat4d( const Vector4d& v )
{
	m_elements[ 0 ] = v[ 0 ];
	m_elements[ 1 ] = v[ 1 ];
	m_elements[ 2 ] = v[ 2 ];
	m_elements[ 3 ] = v[ 3 ];
}

const double& Quat4d::operator [] ( int i ) const
{
	return m_elements[ i ];
}

double& Quat4d::operator [] ( int i )
{
	return m_elements[ i ];
}

Vector3d Quat4d::xyz() const
{
	return Vector3d
		(
		m_elements[ 1 ],
		m_elements[ 2 ],
		m_elements[ 3 ]
	);
}

Vector4d Quat4d::wxyz() const
{
	return Vector4d
		(
		m_elements[ 0 ],
		m_elements[ 1 ],
		m_elements[ 2 ],
		m_elements[ 3 ]
	);
}

double Quat4d::abs() const
{
	return sqrt( absSquared() );	
}

double Quat4d::absSquared() const
{
	return
		(
		m_elements[ 0 ] * m_elements[ 0 ] +
		m_elements[ 1 ] * m_elements[ 1 ] +
		m_elements[ 2 ] * m_elements[ 2 ] +
		m_elements[ 3 ] * m_elements[ 3 ]
	);
}

void Quat4d::normalize()
{
	double reciprocalAbs = 1.0 / abs();

	m_elements[ 0 ] *= reciprocalAbs;
	m_elements[ 1 ] *= reciprocalAbs;
	m_elements[ 2 ] *= reciprocalAbs;
	m_elements[ 3 ] *= reciprocalAbs;
}

Quat4d Quat4d::normalized() const
{
	Quat4d q( *this );
	q.normalize();
	return q;
}

void Quat4d::conjugate()
{
	m_elements[ 1 ] = -m_elements[ 1 ];
	m_elements[ 2 ] = -m_elements[ 2 ];
	m_elements[ 3 ] = -m_elements[ 3 ];
}

Quat4d Quat4d::conjugated() const
{
	return Quat4d
		(
		m_elements[ 0 ],
		-m_elements[ 1 ],
		-m_elements[ 2 ],
		-m_elements[ 3 ]
	);
}

void Quat4d::invert()
{
	Quat4d inverse = conjugated() * ( 1.0 / absSquared() );

	m_elements[ 0 ] = inverse.m_elements[ 0 ];
	m_elements[ 1 ] = inverse.m_elements[ 1 ];
	m_elements[ 2 ] = inverse.m_elements[ 2 ];
	m_elements[ 3 ] = inverse.m_elements[ 3 ];
}

Quat4d Quat4d::inverse() const
{
	return conjugated() * ( 1.0 / absSquared() );
}

Vector3d Quat4d::getAxisAngle( double* radiansOut )
{
	double theta = acos( w ) * 2;
	double vectorNorm = sqrt( x * x + y * y + z * z );
	double reciprocalVectorNorm = 1.0 / vectorNorm;

	*radiansOut = theta;
	return Vector3d
	(
		x * reciprocalVectorNorm,
		y * reciprocalVectorNorm,
		z * reciprocalVectorNorm
	);
}

void Quat4d::setAxisAngle( double radians, const Vector3d& axis )
{
	m_elements[ 0 ] = cos( radians / 2 );

	double sinHalfTheta = sin( radians / 2 );
	double vectorNorm = axis.abs();
	double reciprocalVectorNorm = 1.0 / vectorNorm;

	m_elements[ 1 ] = axis.x * sinHalfTheta * reciprocalVectorNorm;
	m_elements[ 2 ] = axis.y * sinHalfTheta * reciprocalVectorNorm;
	m_elements[ 3 ] = axis.z * sinHalfTheta * reciprocalVectorNorm;
}

void Quat4d::print()
{
	printf( "< %.2lf + %.2lf i + %.2lf j + %.2lf k >\n",
		m_elements[ 0 ], m_elements[ 1 ], m_elements[ 2 ], m_elements[ 3 ] );
}

// static
double Quat4d::dot( const Quat4d& q0, const Quat4d& q1 )
{
	return
	(
		q0.w * q1.w +
		q0.x * q1.x +
		q0.y * q1.y +
		q0.z * q1.z
	);
}

// static
Quat4d Quat4d::lerp( const Quat4d& q0, const Quat4d& q1, double alpha )
{
	return( ( q0 + alpha * ( q1 - q0 ) ).normalized() );
}

// static
Quat4d Quat4d::slerp( const Quat4d& q0, const Quat4d& q1, double alpha, double cosOmegaThreshold )
{
	double cosOmega = Quat4d::dot( q0, q1 );

	// if they're too close, then just lerp them
	if( cosOmega > cosOmegaThreshold )
	{
		return Quat4d::lerp( q0, q1, alpha );
	}
	else
	{
		cosOmega = MathUtils::clampToRangeDouble( cosOmega, -1, 1 );
		if( cosOmega < -1 )
		{
			cosOmega = -1;
		}
		if( cosOmega > 1 )
		{
			cosOmega = 1;
		}

		double omega0 = acos( cosOmega ); // original angle between q0 and q1
		double omega = omega0 * alpha; // new angle

		Quat4d q2 = q1 - q0 * cosOmega;
		q2.normalize();

		return( q0 * cos( omega ) + q2 * sin( omega ) );
	}
}

// static
Quat4d Quat4d::randomRotation( double u0, double u1, double u2 )
{
	double z = u0;
	double theta = 2.0 * MathUtils::PI * u1;
	double r = sqrt( 1.0 - z * z );
	double w = MathUtils::PI * u2;

	return Quat4d
	(
		cos( w ),
		sin( w ) * cos( theta ) * r,
		sin( w ) * sin( theta ) * r,
		sin( w ) * z
	);
}

Vector3d Quat4d::rotateVector( const Vector3d& v )
{
	// return q * v * q^-1
	return ( ( *this ) * Quat4d( v ) * conjugated() ).xyz();
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Quat4d operator + ( const Quat4d& q0, const Quat4d& q1 )
{
	return Quat4d
	(
		q0.w + q1.w,
		q0.x + q1.x,
		q0.y + q1.y,
		q0.z + q1.z
	);
}

Quat4d operator - ( const Quat4d& q0, const Quat4d& q1 )
{
	return Quat4d
	(
		q0.w - q1.w,
		q0.x - q1.x,
		q0.y - q1.y,
		q0.z - q1.z
	);
}

Quat4d operator * ( const Quat4d& q0, const Quat4d& q1 )
{
	return Quat4d
	(
		q0.w * q1.w - q0.x * q1.x - q0.y * q1.y - q0.z * q1.z,
		q0.w * q1.x + q0.x * q1.w + q0.y * q1.z - q0.z * q1.y,
		q0.w * q1.y - q0.x * q1.z + q0.y * q1.w + q0.z * q1.x,
		q0.w * q1.z + q0.x * q1.y - q0.y * q1.x + q0.z * q1.w
	);
}

Quat4d operator * ( double d, const Quat4d& q )
{
	return Quat4d
	(
		d * q.w,
		d * q.x,
		d * q.y,
		d * q.z
	);
}

Quat4d operator * ( const Quat4d& q, double d )
{
	return Quat4d
	(
		d * q.w,
		d * q.x,
		d * q.y,
		d * q.z
	);
}
