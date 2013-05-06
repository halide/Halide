#include "vecmath/Matrix2d.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "vecmath/Matrix2f.h"
#include "vecmath/Vector2f.h"
#include "vecmath/Vector2d.h"

Matrix2d::Matrix2d()
{
	memset( m_elements, 0, 4 * sizeof( double ) );
}

Matrix2d::Matrix2d( double m00, double m01,
				   double m10, double m11 )
{
	m_elements[ 0 ] = m00;
	m_elements[ 1 ] = m10;

	m_elements[ 2 ] = m01;
	m_elements[ 3 ] = m11;
}

Matrix2d::Matrix2d( const Vector2d& v0, const Vector2d& v1, bool setColumns )
{
	if( setColumns )
	{
		setCol( 0, v0 );
		setCol( 1, v1 );
	}
	else
	{
		setRow( 0, v0 );
		setRow( 1, v1 );
	}
}

Matrix2d::Matrix2d( const Matrix2d& rm )
{
	memcpy( m_elements, rm.m_elements, 4 * sizeof( double ) );
}

Matrix2d::Matrix2d( const Matrix2f& rm )
{
	m_elements[ 0 ] = rm( 0, 0 );
	m_elements[ 1 ] = rm( 1, 0 );
	m_elements[ 2 ] = rm( 0, 1 );
	m_elements[ 3 ] = rm( 1, 1 );
}

Matrix2d& Matrix2d::operator = ( const Matrix2d& rm )
{
	if( this != &rm )
	{
		memcpy( m_elements, rm.m_elements, 4 * sizeof( double ) );
	}
	return *this;
}

const double& Matrix2d::operator () ( int i, int j ) const
{
	return m_elements[ j * 2 + i ];
}

double& Matrix2d::operator () ( int i, int j )
{
	return m_elements[ j * 2 + i ];
}

Vector2d Matrix2d::getRow( int i ) const
{
	return Vector2d
	(
		m_elements[ i ],
		m_elements[ i + 2 ]
	);
}

void Matrix2d::setRow( int i, const Vector2d& v )
{
	m_elements[ i ] = v.x;
	m_elements[ i + 2 ] = v.y;
}

Vector2d Matrix2d::getCol( int j ) const
{
	int colStart = 2 * j;

	return Vector2d
	(
		m_elements[ colStart ],
		m_elements[ colStart + 1 ]
	);
}

void Matrix2d::setCol( int j, const Vector2d& v )
{
	int colStart = 2 * j;

	m_elements[ colStart ] = v.x;
	m_elements[ colStart + 1 ] = v.y;
}

double Matrix2d::determinant()
{
	return Matrix2d::determinant2x2
	(
		m_elements[ 0 ], m_elements[ 2 ],
		m_elements[ 1 ], m_elements[ 3 ]
	);
}

Matrix2d Matrix2d::inverse( bool* pbIsSingular, double epsilon )
{
	double determinant = m_elements[ 0 ] * m_elements[ 3 ] - m_elements[ 2 ] * m_elements[ 1 ];

	bool isSingular = ( abs( determinant ) < epsilon );
	if( isSingular )
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = true;
		}
		return Matrix2d();
	}
	else
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = false;
		}

		double reciprocalDeterminant = 1.0 / determinant;

		return Matrix2d
		(
			m_elements[ 3 ] * reciprocalDeterminant, -m_elements[ 2 ] * reciprocalDeterminant,
			-m_elements[ 1 ] * reciprocalDeterminant, m_elements[ 0 ] * reciprocalDeterminant
		);
	}
}

// ---- Utility ----

Matrix2d::operator double* ()
{
	return m_elements;
}

void Matrix2d::print()
{
	printf( "[ %.2lf %.2lf ]\n[ %.2lf %.2lf ]\n",
		m_elements[ 0 ], m_elements[ 2 ],
		m_elements[ 1 ], m_elements[ 3 ] );
}

// static
double Matrix2d::determinant2x2( double m00, double m01,
							   double m10, double m11 )
{
	return( m00 * m11 - m01 * m10 );
}

// static
Matrix2d Matrix2d::ones()
{
	Matrix2d m;
	for( int i = 0; i < 4; ++i )
	{
		m.m_elements[ i ] = 1;
	}

	return m;
}

// static
Matrix2d Matrix2d::identity()
{
	Matrix2d m;

	m( 0, 0 ) = 1;
	m( 1, 1 ) = 1;

	return m;
}

// static
Matrix2d Matrix2d::rotation( double degrees )
{
	double c = cos( degrees );
	double s = sin( degrees );

	return Matrix2d
	(
		c, -s,
		s, c
	);
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Vector2f operator * ( const Matrix2d& m, const Vector2f& v )
{
	Vector2f output( 0, 0 );

	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			output[ i ] += static_cast< float >( m( i, j ) ) * v[ j ];
		}
	}

	return output;
}

Matrix2d operator * ( const Matrix2d& x, const Matrix2d& y )
{
	Matrix2d product; // zeroes

	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			for( int k = 0; k < 2; ++k )
			{
				product( i, k ) += x( i, j ) * y( j, k );
			}
		}
	}

	return product;
}
