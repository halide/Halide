#include "vecmath/Matrix3d.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <math/MathUtils.h>

#include "vecmath/Matrix2d.h"
#include "vecmath/Matrix3f.h"
#include "vecmath/Quat4f.h"
#include "vecmath/Vector3d.h"

Matrix3d::Matrix3d()
{
	memset( m_elements, 0, 9 * sizeof( double ) );
}

Matrix3d::Matrix3d( double m00, double m01, double m02,
				   double m10, double m11, double m12,
				   double m20, double m21, double m22 )
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

Matrix3d::Matrix3d( const Vector3d& v0, const Vector3d& v1, const Vector3d& v2, bool setColumns )
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

Matrix3d::Matrix3d( const Matrix3d& rm )
{
	memcpy( m_elements, rm.m_elements, 9 * sizeof( double ) );
}

Matrix3d::Matrix3d( const Matrix3f& rm )
{
	m_elements[ 0 ] = rm( 0, 0 );
	m_elements[ 1 ] = rm( 1, 0 );
	m_elements[ 2 ] = rm( 2, 0 );
	
	m_elements[ 3 ] = rm( 0, 1 );
	m_elements[ 4 ] = rm( 1, 1 );
	m_elements[ 5 ] = rm( 2, 1 );

	m_elements[ 6 ] = rm( 0, 2 );
	m_elements[ 7 ] = rm( 1, 2 );
	m_elements[ 8 ] = rm( 2, 2 );	
}

Matrix3d& Matrix3d::operator = ( const Matrix3d& rm )
{
	if( this != &rm )
	{
		memcpy( m_elements, rm.m_elements, 9 * sizeof( double ) );
	}
	return *this;
}

const double& Matrix3d::operator () ( int i, int j ) const
{
	return m_elements[ j * 3 + i ];
}

double& Matrix3d::operator () ( int i, int j )
{
	return m_elements[ j * 3 + i ];
}

Vector3d Matrix3d::getRow( int i ) const
{
	return Vector3d
	(
		m_elements[ i ],
		m_elements[ i + 3 ],
		m_elements[ i + 6 ]
	);
}

void Matrix3d::setRow( int i, const Vector3d& v )
{
	m_elements[ i ] = v.x;
	m_elements[ i + 3 ] = v.y;
	m_elements[ i + 6 ] = v.z;
}

Vector3d Matrix3d::getCol( int j ) const
{
	int colStart = 3 * j;

	return Vector3d
	(
		m_elements[ colStart ],
		m_elements[ colStart + 1 ],
		m_elements[ colStart + 2 ]			
	);
}

void Matrix3d::setCol( int j, const Vector3d& v )
{
	int colStart = 3 * j;

	m_elements[ colStart ] = v.x;
	m_elements[ colStart + 1 ] = v.y;
	m_elements[ colStart + 2 ] = v.z;
}

Matrix2d Matrix3d::getSubmatrix2x2( int i0, int j0 ) const
{
	Matrix2d out;

	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			out( i, j ) = ( *this )( i + i0, j + j0 );
		}
	}

	return out;
}

void Matrix3d::setSubmatrix2x2( int i0, int j0, const Matrix2d& m )
{
	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			( *this )( i + i0, j + j0 ) = m( i, j );
		}
	}
}

double Matrix3d::determinant() const
{
	return Matrix3d::determinant3x3
	(
		m_elements[ 0 ], m_elements[ 3 ], m_elements[ 6 ],
		m_elements[ 1 ], m_elements[ 4 ], m_elements[ 7 ],
		m_elements[ 2 ], m_elements[ 5 ], m_elements[ 8 ]
	);
}

Matrix3d Matrix3d::inverse( bool* pbIsSingular, double epsilon ) const
{
	double m00 = m_elements[ 0 ];
	double m10 = m_elements[ 1 ];
	double m20 = m_elements[ 2 ];

	double m01 = m_elements[ 3 ];
	double m11 = m_elements[ 4 ];
	double m21 = m_elements[ 5 ];

	double m02 = m_elements[ 6 ];
	double m12 = m_elements[ 7 ];
	double m22 = m_elements[ 8 ];

	double cofactor00 =  Matrix2d::determinant2x2( m11, m12, m21, m22 );
	double cofactor01 = -Matrix2d::determinant2x2( m10, m12, m20, m22 );
	double cofactor02 =  Matrix2d::determinant2x2( m10, m11, m20, m21 );

	double cofactor10 = -Matrix2d::determinant2x2( m01, m02, m21, m22 );
	double cofactor11 =  Matrix2d::determinant2x2( m00, m02, m20, m22 );
	double cofactor12 = -Matrix2d::determinant2x2( m00, m01, m20, m21 );

	double cofactor20 =  Matrix2d::determinant2x2( m01, m02, m11, m12 );
	double cofactor21 = -Matrix2d::determinant2x2( m00, m02, m10, m12 );
	double cofactor22 =  Matrix2d::determinant2x2( m00, m01, m10, m11 );

	double determinant = m00 * cofactor00 + m01 * cofactor01 + m02 * cofactor02;

	bool isSingular = ( abs( determinant ) < epsilon );
	if( isSingular )
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = true;
		}
		return Matrix3d();
	}
	else
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = false;
		}

		double reciprocalDeterminant = 1.0 / determinant;

		return Matrix3d
		(
			cofactor00 * reciprocalDeterminant, cofactor10 * reciprocalDeterminant, cofactor20 * reciprocalDeterminant,
			cofactor01 * reciprocalDeterminant, cofactor11 * reciprocalDeterminant, cofactor21 * reciprocalDeterminant,
			cofactor02 * reciprocalDeterminant, cofactor12 * reciprocalDeterminant, cofactor22 * reciprocalDeterminant
		);
	}
}

void Matrix3d::transpose()
{
	double temp;

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

Matrix3d Matrix3d::transposed() const
{
	Matrix3d out;
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

Matrix3d::operator double* ()
{
	return m_elements;
}

void Matrix3d::print()
{
	printf( "[ %.2lf %.2lf %.2lf ]\n[ %.2lf %.2lf %.2lf ]\n[ %.2lf %.2lf %.2lf ]\n",
		m_elements[ 0 ], m_elements[ 3 ], m_elements[ 6 ],
		m_elements[ 1 ], m_elements[ 4 ], m_elements[ 7 ],
		m_elements[ 2 ], m_elements[ 5 ], m_elements[ 8 ] );
}

// static
double Matrix3d::determinant3x3( double m00, double m01, double m02,
							   double m10, double m11, double m12,
							   double m20, double m21, double m22 )
{
	return
	(
		m00 * ( m11 * m22 - m12 * m21 )
		- m01 * ( m10 * m22 - m12 * m20 )
		+ m02 * ( m10 * m21 - m11 * m20 )
	);
}

// static
Matrix3d Matrix3d::ones()
{
	Matrix3d m;
	for( int i = 0; i < 9; ++i )
	{
		m.m_elements[ i ] = 1;
	}

	return m;
}

// static
Matrix3d Matrix3d::identity()
{
	Matrix3d m;

	m( 0, 0 ) = 1;
	m( 1, 1 ) = 1;
	m( 2, 2 ) = 1;

	return m;
}

// static
Matrix3d Matrix3d::rotation( const Vector3d& rDirection, double degrees )
{
	Vector3d normalizedDirection = rDirection.normalized();

	double theta = MathUtils::degreesToRadians( degrees );
	double cosTheta = cos( theta );
	double sinTheta = sin( theta );

	double x = normalizedDirection.x;
	double y = normalizedDirection.y;
	double z = normalizedDirection.z;

	return Matrix3d
	(
		x * x * ( 1.0 - cosTheta ) + cosTheta,			y * x * ( 1.0 - cosTheta ) - z * sinTheta,		z * x * ( 1.0 - cosTheta ) + y * sinTheta,
		x * y * ( 1.0 - cosTheta ) + z * sinTheta,		y * y * ( 1.0 - cosTheta ) + cosTheta,			z * y * ( 1.0 - cosTheta ) - x * sinTheta,
		x * z * ( 1.0 - cosTheta ) - y * sinTheta,		y * z * ( 1.0 - cosTheta ) + x * sinTheta,		z * z * ( 1.0 - cosTheta ) + cosTheta
	);
}

// static
Matrix3d Matrix3d::rotation( const Quat4f& rq )
{
	Quat4f q = rq.normalized();

	double xx = q.x * q.x;
	double yy = q.y * q.y;
	double zz = q.z * q.z;

	double xy = q.x * q.y;
	double zw = q.z * q.w;

	double xz = q.x * q.z;
	double yw = q.y * q.w;

	double yz = q.y * q.z;
	double xw = q.x * q.w;

	return Matrix3d
	(
		1.0 - 2.0 * ( yy + zz ),		2.0 * ( xy - zw ),				2.0 * ( xz + yw ),
		2.0 * ( xy + zw ),				1.0 - 2.0 * ( xx + zz ),		2.0 * ( yz - xw ),
		2.0 * ( xz - yw ),				2.0 * ( yz + xw ),				1.0 - 2.0 * ( xx + yy )
	);
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Vector3d operator * ( const Matrix3d& m, const Vector3d& v )
{
	Vector3d output( 0, 0, 0 );

	for( int i = 0; i < 3; ++i )
	{
		for( int j = 0; j < 3; ++j )
		{
			output[ i ] += m( i, j ) * v[ j ];
		}
	}

	return output;
}

Matrix3d operator * ( const Matrix3d& x, const Matrix3d& y )
{
	Matrix3d product; // zeroes

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
