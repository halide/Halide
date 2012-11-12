#include <cmath>
#include <cstdio>

#include <math/MathUtils.h>

#include "vecmath/Quat4f.h"
#include "vecmath/Vector3f.h"
#include "vecmath/Vector4f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
const Quat4f Quat4f::ZERO = Quat4f( 0, 0, 0, 0 );

// static
const Quat4f Quat4f::IDENTITY = Quat4f( 1, 0, 0, 0 );

Quat4f::Quat4f()
{
	m_elements[ 0 ] = 0;
	m_elements[ 1 ] = 0;
	m_elements[ 2 ] = 0;
	m_elements[ 3 ] = 0;
}

Quat4f::Quat4f( float w, float x, float y, float z )
{
	m_elements[ 0 ] = w;
	m_elements[ 1 ] = x;
	m_elements[ 2 ] = y;
	m_elements[ 3 ] = z;
}

Quat4f::Quat4f( const Quat4f& rq )
{
	m_elements[ 0 ] = rq.m_elements[ 0 ];
	m_elements[ 1 ] = rq.m_elements[ 1 ];
	m_elements[ 2 ] = rq.m_elements[ 2 ];
	m_elements[ 3 ] = rq.m_elements[ 3 ];
}

Quat4f& Quat4f::operator = ( const Quat4f& rq )
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

Quat4f::Quat4f( const Vector3f& v )
{
	m_elements[ 0 ] = 0;
	m_elements[ 1 ] = v[ 0 ];
	m_elements[ 2 ] = v[ 1 ];
	m_elements[ 3 ] = v[ 2 ];
}

Quat4f::Quat4f( const Vector4f& v )
{
	m_elements[ 0 ] = v[ 0 ];
	m_elements[ 1 ] = v[ 1 ];
	m_elements[ 2 ] = v[ 2 ];
	m_elements[ 3 ] = v[ 3 ];
}

const float& Quat4f::operator [] ( int i ) const
{
	return m_elements[ i ];
}

float& Quat4f::operator [] ( int i )
{
	return m_elements[ i ];
}

Vector3f Quat4f::xyz() const
{
	return Vector3f
	(
		m_elements[ 1 ],
		m_elements[ 2 ],
		m_elements[ 3 ]
	);
}

Vector4f Quat4f::wxyz() const
{
	return Vector4f
	(
		m_elements[ 0 ],
		m_elements[ 1 ],
		m_elements[ 2 ],
		m_elements[ 3 ]
	);
}

float Quat4f::abs() const
{
	return sqrt( absSquared() );	
}

float Quat4f::absSquared() const
{
	return
	(
		m_elements[ 0 ] * m_elements[ 0 ] +
		m_elements[ 1 ] * m_elements[ 1 ] +
		m_elements[ 2 ] * m_elements[ 2 ] +
		m_elements[ 3 ] * m_elements[ 3 ]
	);
}

void Quat4f::normalize()
{
	float reciprocalAbs = 1.f / abs();

	m_elements[ 0 ] *= reciprocalAbs;
	m_elements[ 1 ] *= reciprocalAbs;
	m_elements[ 2 ] *= reciprocalAbs;
	m_elements[ 3 ] *= reciprocalAbs;
}

Quat4f Quat4f::normalized() const
{
	Quat4f q( *this );
	q.normalize();
	return q;
}

void Quat4f::conjugate()
{
	m_elements[ 1 ] = -m_elements[ 1 ];
	m_elements[ 2 ] = -m_elements[ 2 ];
	m_elements[ 3 ] = -m_elements[ 3 ];
}

Quat4f Quat4f::conjugated() const
{
	return Quat4f
	(
		 m_elements[ 0 ],
		-m_elements[ 1 ],
		-m_elements[ 2 ],
		-m_elements[ 3 ]
	);
}

void Quat4f::invert()
{
	Quat4f inverse = conjugated() * ( 1.0f / absSquared() );

	m_elements[ 0 ] = inverse.m_elements[ 0 ];
	m_elements[ 1 ] = inverse.m_elements[ 1 ];
	m_elements[ 2 ] = inverse.m_elements[ 2 ];
	m_elements[ 3 ] = inverse.m_elements[ 3 ];
}

Quat4f Quat4f::inverse() const
{
	return conjugated() * ( 1.0f / absSquared() );
}


Quat4f Quat4f::log() const
{
	float len =
		sqrt
		(
			m_elements[ 1 ] * m_elements[ 1 ] +
			m_elements[ 2 ] * m_elements[ 2 ] +
			m_elements[ 3 ] * m_elements[ 3 ]
		);

	if( len < 1e-6 )
	{
		return Quat4f( 0, m_elements[ 1 ], m_elements[ 2 ], m_elements[ 3 ] );
	}
	else
	{
		float coeff = acos( m_elements[ 0 ] ) / len;
		return Quat4f( 0, m_elements[ 1 ] * coeff, m_elements[ 2 ] * coeff, m_elements[ 3 ] * coeff );
	}
}

Quat4f Quat4f::exp() const
{
	float theta =
		sqrt
		(
			m_elements[ 1 ] * m_elements[ 1 ] +
			m_elements[ 2 ] * m_elements[ 2 ] +
			m_elements[ 3 ] * m_elements[ 3 ]
		);

	if( theta < 1e-6 )
	{
		return Quat4f( cos( theta ), m_elements[ 1 ], m_elements[ 2 ], m_elements[ 3 ] );
	}
	else
	{
		float coeff = sin( theta ) / theta;
		return Quat4f( cos( theta ), m_elements[ 1 ] * coeff, m_elements[ 2 ] * coeff, m_elements[ 3 ] * coeff );		
	}
}

Vector3f Quat4f::getAxisAngle( float* radiansOut )
{
	float theta = acos( w ) * 2;
	float vectorNorm = sqrt( x * x + y * y + z * z );
	float reciprocalVectorNorm = 1.f / vectorNorm;

	*radiansOut = theta;
	return Vector3f
	(
		x * reciprocalVectorNorm,
		y * reciprocalVectorNorm,
		z * reciprocalVectorNorm
	);
}

void Quat4f::setAxisAngle( float radians, const Vector3f& axis )
{
	m_elements[ 0 ] = cos( radians / 2 );

	float sinHalfTheta = sin( radians / 2 );
	float vectorNorm = axis.abs();
	float reciprocalVectorNorm = 1.f / vectorNorm;

	m_elements[ 1 ] = axis.x * sinHalfTheta * reciprocalVectorNorm;
	m_elements[ 2 ] = axis.y * sinHalfTheta * reciprocalVectorNorm;
	m_elements[ 3 ] = axis.z * sinHalfTheta * reciprocalVectorNorm;
}

void Quat4f::print()
{
	printf( "< %.2f + %.2f i + %.2f j + %.2f k >\n",
		m_elements[ 0 ], m_elements[ 1 ], m_elements[ 2 ], m_elements[ 3 ] );
}

// static
float Quat4f::dot( const Quat4f& q0, const Quat4f& q1 )
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
Quat4f Quat4f::lerp( const Quat4f& q0, const Quat4f& q1, float alpha )
{
	return( ( q0 + alpha * ( q1 - q0 ) ).normalized() );
}

// static
Quat4f Quat4f::slerp( const Quat4f& a, const Quat4f& b, float t, bool allowFlip )
{
	float cosAngle = Quat4f::dot( a, b );

	float c1;
	float c2;

	// Linear interpolation for close orientations
	if( ( 1.0f - fabs( cosAngle ) ) < 0.01f )
	{
		c1 = 1.0f - t;
		c2 = t;
	}
	else
	{
		// Spherical interpolation
		float angle = acos( fabs( cosAngle ) );
		float sinAngle = sin( angle );
		c1 = sin( angle * ( 1.0f - t ) ) / sinAngle;
		c2 = sin( angle * t ) / sinAngle;
	}

	// Use the shortest path
	if( allowFlip && ( cosAngle < 0.0f ) )
	{
		c1 = -c1;
	}

	return Quat4f( c1 * a[ 0 ] + c2 * b[ 0 ], c1 * a[ 1 ] + c2 * b[ 1 ], c1 * a[ 2 ] + c2 * b[ 2 ], c1 * a[ 3 ] + c2 * b[ 3 ] );
}

// static
Quat4f Quat4f::squad( const Quat4f& a, const Quat4f& tanA, const Quat4f& tanB, const Quat4f& b, float t )
{
	Quat4f ab = Quat4f::slerp( a, b, t );
	Quat4f tangent = Quat4f::slerp( tanA, tanB, t, false );
	return Quat4f::slerp( ab, tangent, 2.0f * t * ( 1.0f - t ), false );
}

// static
Quat4f Quat4f::cubicInterpolate( const Quat4f& q0, const Quat4f& q1, const Quat4f& q2, const Quat4f& q3, float t )
{
	// geometric construction:
	//            t
	//   (t+1)/2     t/2
	// t+1        t	        t-1

	// bottom level
	Quat4f q0q1 = Quat4f::slerp( q0, q1, t + 1 );
	Quat4f q1q2 = Quat4f::slerp( q1, q2, t );
	Quat4f q2q3 = Quat4f::slerp( q2, q3, t - 1 );

	// middle level
	Quat4f q0q1_q1q2 = Quat4f::slerp( q0q1, q1q2, 0.5f * ( t + 1 ) );
	Quat4f q1q2_q2q3 = Quat4f::slerp( q1q2, q2q3, 0.5f * t );

	// top level
	return Quat4f::slerp( q0q1_q1q2, q1q2_q2q3, t );
}

// static
Quat4f Quat4f::logDifference( const Quat4f& a, const Quat4f& b )
{
	Quat4f diff = a.inverse() * b;
	diff.normalize();
	return diff.log();
}

// static
Quat4f Quat4f::squadTangent( const Quat4f& before, const Quat4f& center, const Quat4f& after )
{
	Quat4f l1 = Quat4f::logDifference( center, before );
	Quat4f l2 = Quat4f::logDifference( center, after );
	
	Quat4f e;
	for( int i = 0; i < 4; ++i )
	{
		e[ i ] = -0.25f * ( l1[ i ] + l2[ i ] );
	}
	e = center * ( e.exp() );

	return e;
}

// static
Quat4f Quat4f::fromRotationMatrix( const Matrix3f& m )
{
	float x;
	float y;
	float z;
	float w;

	// Compute one plus the trace of the matrix
	float onePlusTrace = 1.0f + m( 0, 0 ) + m( 1, 1 ) + m( 2, 2 );

	if( onePlusTrace > 1e-5 )
	{
		// Direct computation
		float s = sqrt( onePlusTrace ) * 2.0f;
		x = ( m( 2, 1 ) - m( 1, 2 ) ) / s;
		y = ( m( 0, 2 ) - m( 2, 0 ) ) / s;
		z = ( m( 1, 0 ) - m( 0, 1 ) ) / s;
		w = 0.25f * s;
	}
	else
	{
		// Computation depends on major diagonal term
		if( ( m( 0, 0 ) > m( 1, 1 ) ) & ( m( 0, 0 ) > m( 2, 2 ) ) )
		{
			float s = sqrt( 1.0f + m( 0, 0 ) - m( 1, 1 ) - m( 2, 2 ) ) * 2.0f;
			x = 0.25f * s;
			y = ( m( 0, 1 ) + m( 1, 0 ) ) / s;
			z = ( m( 0, 2 ) + m( 2, 0 ) ) / s;
			w = ( m( 1, 2 ) - m( 2, 1 ) ) / s;
		}
		else if( m( 1, 1 ) > m( 2, 2 ) )
		{
			float s = sqrt( 1.0f + m( 1, 1 ) - m( 0, 0 ) - m( 2, 2 ) ) * 2.0f;
			x = ( m( 0, 1 ) + m( 1, 0 ) ) / s;
			y = 0.25f * s;
			z = ( m( 1, 2 ) + m( 2, 1 ) ) / s;
			w = ( m( 0, 2 ) - m( 2, 0 ) ) / s;
		}
		else
		{
			float s = sqrt( 1.0f + m( 2, 2 ) - m( 0, 0 ) - m( 1, 1 ) ) * 2.0f;
			x = ( m( 0, 2 ) + m( 2, 0 ) ) / s;
			y = ( m( 1, 2 ) + m( 2, 1 ) ) / s;
			z = 0.25f * s;
			w = ( m( 0, 1 ) - m( 1, 0 ) ) / s;
		}
	}

	Quat4f q( w, x, y, z );
	return q.normalized();
}

// static
Quat4f Quat4f::fromRotatedBasis( const Vector3f& x, const Vector3f& y, const Vector3f& z )
{
	return fromRotationMatrix( Matrix3f( x, y, z ) );
}

// static
Quat4f Quat4f::randomRotation( float u0, float u1, float u2 )
{
	float z = u0;
	float theta = static_cast< float >( 2.f * MathUtils::PI * u1 );
	float r = sqrt( 1.f - z * z );
	float w = static_cast< float >( MathUtils::PI * u2 );

	return Quat4f
	(
		cos( w ),
		sin( w ) * cos( theta ) * r,
		sin( w ) * sin( theta ) * r,
		sin( w ) * z
	);
}

Vector3f Quat4f::rotateVector( const Vector3f& v )
{
	// return q * v * q^-1
	return ( ( *this ) * Quat4f( v ) * conjugated() ).xyz();
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Quat4f operator + ( const Quat4f& q0, const Quat4f& q1 )
{
	return Quat4f
	(
		q0.w + q1.w,
		q0.x + q1.x,
		q0.y + q1.y,
		q0.z + q1.z
	);
}

Quat4f operator - ( const Quat4f& q0, const Quat4f& q1 )
{
	return Quat4f
	(
		q0.w - q1.w,
		q0.x - q1.x,
		q0.y - q1.y,
		q0.z - q1.z
	);
}

Quat4f operator * ( const Quat4f& q0, const Quat4f& q1 )
{
	return Quat4f
	(
		q0.w * q1.w - q0.x * q1.x - q0.y * q1.y - q0.z * q1.z,
		q0.w * q1.x + q0.x * q1.w + q0.y * q1.z - q0.z * q1.y,
		q0.w * q1.y - q0.x * q1.z + q0.y * q1.w + q0.z * q1.x,
		q0.w * q1.z + q0.x * q1.y - q0.y * q1.x + q0.z * q1.w
	);
}

Quat4f operator * ( float f, const Quat4f& q )
{
	return Quat4f
	(
		f * q.w,
		f * q.x,
		f * q.y,
		f * q.z
	);
}

Quat4f operator * ( const Quat4f& q, float f )
{
	return Quat4f
	(
		f * q.w,
		f * q.x,
		f * q.y,
		f * q.z
	);
}
