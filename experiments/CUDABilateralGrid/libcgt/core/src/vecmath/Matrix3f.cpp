#include "vecmath/Matrix3f.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <math/MathUtils.h>

#include "vecmath/Matrix2f.h"
#include "vecmath/Quat4f.h"
#include "vecmath/Vector3f.h"

Matrix3f::Matrix3f( float fill )
{
	for( int i = 0; i < 9; ++i )
	{
		m_elements[ i ] = fill;
	}	
}

Matrix3f::Matrix3f( float m00, float m01, float m02,
				   float m10, float m11, float m12,
				   float m20, float m21, float m22 )
{
	m_elements[ 0 ] = m00;
	m_elements[ 1 ] = m10;
	m_elements[ 2 ] = m20;

	m_elements[ 3 ] = m01;
	m_elements[ 4 ] = m11;
	m_elements[ 5 ] = m21;

	m_elements[ 6 ] = m02;
	m_elements[ 7 ] = m12;
	m_elements[ 8 ] = m22;
}

Matrix3f::Matrix3f( const Vector3f& v0, const Vector3f& v1, const Vector3f& v2, bool setColumns )
{
	if( setColumns )
	{
		setCol( 0, v0 );
		setCol( 1, v1 );
		setCol( 2, v2 );
	}
	else
	{
		setRow( 0, v0 );
		setRow( 1, v1 );
		setRow( 2, v2 );
	}
}

Matrix3f::Matrix3f( const Matrix3f& rm )
{
	memcpy( m_elements, rm.m_elements, 9 * sizeof( float ) );
}

Matrix3f& Matrix3f::operator = ( const Matrix3f& rm )
{
	if( this != &rm )
	{
		memcpy( m_elements, rm.m_elements, 9 * sizeof( float ) );
	}
	return *this;
}

const float& Matrix3f::operator () ( int i, int j ) const
{
	return m_elements[ j * 3 + i ];
}

float& Matrix3f::operator () ( int i, int j )
{
	return m_elements[ j * 3 + i ];
}

Vector3f Matrix3f::getRow( int i ) const
{
	return Vector3f
	(
		m_elements[ i ],
		m_elements[ i + 3 ],
		m_elements[ i + 6 ]
	);
}

void Matrix3f::setRow( int i, const Vector3f& v )
{
	m_elements[ i ] = v.x;
	m_elements[ i + 3 ] = v.y;
	m_elements[ i + 6 ] = v.z;
}

Vector3f Matrix3f::getCol( int j ) const
{
	int colStart = 3 * j;

	return Vector3f
	(
		m_elements[ colStart ],
		m_elements[ colStart + 1 ],
		m_elements[ colStart + 2 ]			
	);
}

void Matrix3f::setCol( int j, const Vector3f& v )
{
	int colStart = 3 * j;

	m_elements[ colStart ] = v.x;
	m_elements[ colStart + 1 ] = v.y;
	m_elements[ colStart + 2 ] = v.z;
}

Matrix2f Matrix3f::getSubmatrix2x2( int i0, int j0 ) const
{
	Matrix2f out;

	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			out( i, j ) = ( *this )( i + i0, j + j0 );
		}
	}

	return out;
}

void Matrix3f::setSubmatrix2x2( int i0, int j0, const Matrix2f& m )
{
	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			( *this )( i + i0, j + j0 ) = m( i, j );
		}
	}
}

float Matrix3f::determinant() const
{
	return Matrix3f::determinant3x3
	(
		m_elements[ 0 ], m_elements[ 3 ], m_elements[ 6 ],
		m_elements[ 1 ], m_elements[ 4 ], m_elements[ 7 ],
		m_elements[ 2 ], m_elements[ 5 ], m_elements[ 8 ]
	);
}

Matrix3f Matrix3f::inverse( bool* pbIsSingular, float epsilon ) const
{
	float m00 = m_elements[ 0 ];
	float m10 = m_elements[ 1 ];
	float m20 = m_elements[ 2 ];

	float m01 = m_elements[ 3 ];
	float m11 = m_elements[ 4 ];
	float m21 = m_elements[ 5 ];

	float m02 = m_elements[ 6 ];
	float m12 = m_elements[ 7 ];
	float m22 = m_elements[ 8 ];

	float cofactor00 =  Matrix2f::determinant2x2( m11, m12, m21, m22 );
	float cofactor01 = -Matrix2f::determinant2x2( m10, m12, m20, m22 );
	float cofactor02 =  Matrix2f::determinant2x2( m10, m11, m20, m21 );

	float cofactor10 = -Matrix2f::determinant2x2( m01, m02, m21, m22 );
	float cofactor11 =  Matrix2f::determinant2x2( m00, m02, m20, m22 );
	float cofactor12 = -Matrix2f::determinant2x2( m00, m01, m20, m21 );

	float cofactor20 =  Matrix2f::determinant2x2( m01, m02, m11, m12 );
	float cofactor21 = -Matrix2f::determinant2x2( m00, m02, m10, m12 );
	float cofactor22 =  Matrix2f::determinant2x2( m00, m01, m10, m11 );

	float determinant = m00 * cofactor00 + m01 * cofactor01 + m02 * cofactor02;
	
	bool isSingular = ( abs( determinant ) < epsilon );
	if( isSingular )
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = true;
		}
		return Matrix3f();
	}
	else
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = false;
		}

		float reciprocalDeterminant = 1.0f / determinant;

		return Matrix3f
		(
			cofactor00 * reciprocalDeterminant, cofactor10 * reciprocalDeterminant, cofactor20 * reciprocalDeterminant,
			cofactor01 * reciprocalDeterminant, cofactor11 * reciprocalDeterminant, cofactor21 * reciprocalDeterminant,
			cofactor02 * reciprocalDeterminant, cofactor12 * reciprocalDeterminant, cofactor22 * reciprocalDeterminant
		);
	}
}

void Matrix3f::transpose()
{
	float temp;

	for( int i = 0; i < 2; ++i )
	{
		for( int j = i + 1; j < 3; ++j )
		{
			temp = ( *this )( i, j );
			( *this )( i, j ) = ( *this )( j, i );
			( *this )( j, i ) = temp;
		}
	}
}

Matrix3f Matrix3f::transposed() const
{
	Matrix3f out;
	for( int i = 0; i < 3; ++i )
	{
		for( int j = 0; j < 3; ++j )
		{
			out( j, i ) = ( *this )( i, j );
		}
	}

	return out;
}

// ---- Utility ----

Matrix3f::operator float* ()
{
	return m_elements;
}

void Matrix3f::print()
{
	printf( "[ %.2f %.2f %.2f ]\n[ %.2f %.2f %.2f ]\n[ %.2f %.2f %.2f ]\n",
		m_elements[ 0 ], m_elements[ 3 ], m_elements[ 6 ],
		m_elements[ 1 ], m_elements[ 4 ], m_elements[ 7 ],
		m_elements[ 2 ], m_elements[ 5 ], m_elements[ 8 ] );
}

// static
float Matrix3f::determinant3x3( float m00, float m01, float m02,
							   float m10, float m11, float m12,
							   float m20, float m21, float m22 )
{
	return
		(
			  m00 * ( m11 * m22 - m12 * m21 )
			- m01 * ( m10 * m22 - m12 * m20 )
			+ m02 * ( m10 * m21 - m11 * m20 )
		);
}

// static
Matrix3f Matrix3f::ones()
{
	Matrix3f m;
	for( int i = 0; i < 9; ++i )
	{
		m.m_elements[ i ] = 1;
	}

	return m;
}

// static
Matrix3f Matrix3f::identity()
{
	Matrix3f m;

	m( 0, 0 ) = 1;
	m( 1, 1 ) = 1;
	m( 2, 2 ) = 1;

	return m;
}


// static
Matrix3f Matrix3f::rotateX( float radians )
{
	float c = cos( radians );
	float s = sin( radians );

	return Matrix3f
	(
		1, 0, 0,
		0, c, -s,
		0, s, c
	);
}

// static
Matrix3f Matrix3f::rotateY( float radians )
{
	float c = cos( radians );
	float s = sin( radians );

	return Matrix3f
	(
		c, 0, s,
		0, 1, 0,
		-s, 0, c
	);
}

// static
Matrix3f Matrix3f::rotateZ( float radians )
{
	float c = cos( radians );
	float s = sin( radians );

	return Matrix3f
	(
		c, -s, 0,
		s, c, 0,
		0, 0, 1
	);
}

// static
Matrix3f Matrix3f::scaling( float sx, float sy, float sz )
{
	return Matrix3f
	(
		sx, 0, 0,
		0, sy, 0,
		0, 0, sz
	);
}

// static
Matrix3f Matrix3f::uniformScaling( float s )
{
	return Matrix3f
	(
		s, 0, 0,
		0, s, 0,
		0, 0, s
	);
}

// static
Matrix3f Matrix3f::rotation( const Vector3f& rDirection, float degrees )
{
	Vector3f normalizedDirection = rDirection.normalized();
	
	float theta = MathUtils::degreesToRadians( degrees );
	float cosTheta = cos( theta );
	float sinTheta = sin( theta );

	float x = normalizedDirection.x;
	float y = normalizedDirection.y;
	float z = normalizedDirection.z;

	return Matrix3f
		(
			x * x * ( 1.0f - cosTheta ) + cosTheta,			y * x * ( 1.0f - cosTheta ) - z * sinTheta,		z * x * ( 1.0f - cosTheta ) + y * sinTheta,
			x * y * ( 1.0f - cosTheta ) + z * sinTheta,		y * y * ( 1.0f - cosTheta ) + cosTheta,			z * y * ( 1.0f - cosTheta ) - x * sinTheta,
			x * z * ( 1.0f - cosTheta ) - y * sinTheta,		y * z * ( 1.0f - cosTheta ) + x * sinTheta,		z * z * ( 1.0f - cosTheta ) + cosTheta
		);
}

// static
Matrix3f Matrix3f::rotation( const Quat4f& rq )
{
	Quat4f q = rq.normalized();

	float xx = q.x * q.x;
	float yy = q.y * q.y;
	float zz = q.z * q.z;

	float xy = q.x * q.y;
	float zw = q.z * q.w;

	float xz = q.x * q.z;
	float yw = q.y * q.w;

	float yz = q.y * q.z;
	float xw = q.x * q.w;

	return Matrix3f
		(
			1.0f - 2.0f * ( yy + zz ),		2.0f * ( xy - zw ),				2.0f * ( xz + yw ),
			2.0f * ( xy + zw ),				1.0f - 2.0f * ( xx + zz ),		2.0f * ( yz - xw ),
			2.0f * ( xz - yw ),				2.0f * ( yz + xw ),				1.0f - 2.0f * ( xx + yy )
		);
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Vector3f operator * ( const Matrix3f& m, const Vector3f& v )
{
	Vector3f output( 0, 0, 0 );

	for( int i = 0; i < 3; ++i )
	{
		for( int j = 0; j < 3; ++j )
		{
			output[ i ] += m( i, j ) * v[ j ];
		}
	}

	return output;
}

Matrix3f operator * ( const Matrix3f& x, const Matrix3f& y )
{
	Matrix3f product; // zeroes

	for( int i = 0; i < 3; ++i )
	{
		for( int j = 0; j < 3; ++j )
		{
			for( int k = 0; k < 3; ++k )
			{
				product( i, k ) += x( i, j ) * y( j, k );
			}
		}
	}

	return product;
}
