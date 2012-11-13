#include "vecmath/Matrix2f.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "vecmath/Vector2f.h"

Matrix2f::Matrix2f()
{
	memset( m_elements, 0, 4 * sizeof( float ) );
}

Matrix2f::Matrix2f( float _m00, float _m01,
				   float _m10, float _m11 ) :

	m00( _m00 ),
	m01( _m01 ),
	m10( _m10 ),
	m11( _m11 )

{

}

Matrix2f::Matrix2f( const Vector2f& v0, const Vector2f& v1, bool setColumns )
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

Matrix2f::Matrix2f( const Matrix2f& rm )
{
	memcpy( m_elements, rm.m_elements, 4 * sizeof( float ) );
}

Matrix2f& Matrix2f::operator = ( const Matrix2f& rm )
{
	if( this != &rm )
	{
		memcpy( m_elements, rm.m_elements, 4 * sizeof( float ) );
	}
	return *this;
}

const float& Matrix2f::operator () ( int i, int j ) const
{
	return m_elements[ j * 2 + i ];
}

float& Matrix2f::operator () ( int i, int j )
{
	return m_elements[ j * 2 + i ];
}

Vector2f Matrix2f::getRow( int i ) const
{
	return Vector2f
	(
		m_elements[ i ],
		m_elements[ i + 2 ]
	);
}

void Matrix2f::setRow( int i, const Vector2f& v )
{
	m_elements[ i ] = v.x;
	m_elements[ i + 2 ] = v.y;
}

Vector2f Matrix2f::getCol( int j ) const
{
	int colStart = 2 * j;

	return Vector2f
	(
		m_elements[ colStart ],
		m_elements[ colStart + 1 ]
	);
}

void Matrix2f::setCol( int j, const Vector2f& v )
{
	int colStart = 2 * j;

	m_elements[ colStart ] = v.x;
	m_elements[ colStart + 1 ] = v.y;
}

float Matrix2f::determinant()
{
	return Matrix2f::determinant2x2
	(
		m_elements[ 0 ], m_elements[ 2 ],
		m_elements[ 1 ], m_elements[ 3 ]
	);
}

Matrix2f Matrix2f::inverse( bool* pbIsSingular, float epsilon )
{
	float determinant = m_elements[ 0 ] * m_elements[ 3 ] - m_elements[ 2 ] * m_elements[ 1 ];

	bool isSingular = ( abs( determinant ) < epsilon );
	if( isSingular )
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = true;
		}
		return Matrix2f();
	}
	else
	{
		if( pbIsSingular != NULL )
		{
			*pbIsSingular = false;
		}

		float reciprocalDeterminant = 1.0f / determinant;

		return Matrix2f
		(
			m_elements[ 3 ] * reciprocalDeterminant, -m_elements[ 2 ] * reciprocalDeterminant,
			-m_elements[ 1 ] * reciprocalDeterminant, m_elements[ 0 ] * reciprocalDeterminant
		);
	}
}

void Matrix2f::transpose()
{
	float m01 = ( *this )( 0, 1 );
	float m10 = ( *this )( 1, 0 );

	( *this )( 0, 1 ) = m10;
	( *this )( 1, 0 ) = m01;
}

Matrix2f Matrix2f::transposed() const
{
	return Matrix2f
	(
		( *this )( 0, 0 ), ( *this )( 1, 0 ),
		( *this )( 0, 1 ), ( *this )( 1, 1 )
	);

}

// ---- Utility ----

Matrix2f::operator float* ()
{
	return m_elements;
}

void Matrix2f::print()
{
	printf( "[ %.2f %.2f ]\n[ %.2f %.2f ]\n",
		m_elements[ 0 ], m_elements[ 2 ],
		m_elements[ 1 ], m_elements[ 3 ] );
}

// static
float Matrix2f::determinant2x2( float m00, float m01,
							   float m10, float m11 )
{
	return( m00 * m11 - m01 * m10 );
}

// static
Matrix2f Matrix2f::ones()
{
	Matrix2f m;
	for( int i = 0; i < 4; ++i )
	{
		m.m_elements[ i ] = 1;
	}

	return m;
}

// static
Matrix2f Matrix2f::identity()
{
	Matrix2f m;

	m( 0, 0 ) = 1;
	m( 1, 1 ) = 1;

	return m;
}

// static
Matrix2f Matrix2f::rotation( float degrees )
{
	float c = cos( degrees );
	float s = sin( degrees );

	return Matrix2f
	(
		c, -s,
		s, c
	);
}

//////////////////////////////////////////////////////////////////////////
// Operators
//////////////////////////////////////////////////////////////////////////

Matrix2f operator * ( float f, const Matrix2f& m )
{
	Matrix2f output;

	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			output( i, j ) = f * m( i, j );
		}
	}

	return output;
}

Matrix2f operator * ( const Matrix2f& m, float f )
{
	return f * m;
}

Vector2f operator * ( const Matrix2f& m, const Vector2f& v )
{
	Vector2f output( 0, 0 );

	for( int i = 0; i < 2; ++i )
	{
		for( int j = 0; j < 2; ++j )
		{
			output[ i ] += m( i, j ) * v[ j ];
		}
	}

	return output;
}

Matrix2f operator * ( const Matrix2f& x, const Matrix2f& y )
{
	Matrix2f product; // zeroes

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
