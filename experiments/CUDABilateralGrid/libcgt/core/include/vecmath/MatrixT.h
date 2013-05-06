#ifndef MATRIX_T_H
#define MATRIX_T_H

#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <QVector>

#include "common/ReferenceCountedArray.h"

// templatized matrix class
template< typename T, int nDimensions >
class MatrixT
{
public:

	// constructors
	MatrixT( int size0, ... );
	MatrixT( MatrixT< int, 1 > size );
	MatrixT( const MatrixT& rm );
	MatrixT& operator = ( const MatrixT& rm );
	virtual ~MatrixT();

	// TODO: a = 3 for existing a
	// MatrixT& operator = ( const T& fill );

	T& operator () ( int i, ... );
	const T& operator () ( int i, ... ) const;

	ReferenceCountedArray< T > data() const;
	
	int numElements() const;
	int numDimensions() const;	
	MatrixT< int, 1 > size() const;

	void resize( int size0, ... );

private:

	int m_aiSizes[ nDimensions ];
	int m_aiProductSizes[ nDimensions ];
	ReferenceCountedArray< T > m_raData;
};

template< typename T, int nDimensions >
MatrixT< T, nDimensions >::MatrixT( int size0, ... )
{
	assert( size0 > 0 );

	va_list argptr;
	va_start( argptr, size0 );

	// get the 0th size
	m_aiSizes[ 0 ] = size0;

	// accumulate nElements
	int nElements = size0;

	for( int j = 1; j < nDimensions; ++j )
	{
		int arg = va_arg( argptr, int );
		assert( arg > 0 );

		m_aiSizes[ j ] = arg;		
		nElements *= arg;
	}
	va_end( argptr );

	m_aiProductSizes[ 0 ] = 1;
	for( int j = 1; j < nDimensions; ++j )
	{
		m_aiProductSizes[ j ] = m_aiSizes[ j - 1 ] * m_aiProductSizes[ j - 1 ];
	}

	m_raData = ReferenceCountedArray< T >( nElements );
}

#if 0
template< typename T, int nDimensions >
MatrixT< T, nDimensions >::MatrixT( MatrixT< int, 1 > size )
{
	assert( size.numElements() == numDimensions() );
	int size0 = size( 0 );
	assert( size0 > 0 );

	// get the 0th size
	m_aiSizes[ 0 ] = size0;

	// accumulate nElements
	int nElements = size0;

	for( int j = 1; j < nDimensions; ++j )
	{
		int s = size( j );
		assert( s > 0 );

		m_aiSizes[ j ] = s;
		nElements *= s;
	}

	m_aiProductSizes[ 0 ] = 1;
	for( int j = 1; j < nDimensions; ++j )
	{
		m_aiProductSizes[ j ] = m_aiSizes[ j - 1 ] * m_aiProductSizes[ j - 1 ];
	}

	m_raData = ReferenceCountedArray< T >( nElements );
}
#endif //0

template< typename T, int nDimensions >
MatrixT< T, nDimensions >::MatrixT( const MatrixT< T, nDimensions >& rm )	
{
	for( int i = 0; i < nDimensions; ++i )
	{
		m_aiSizes[ i ] = rm.m_aiSizes[ i ];
		m_aiProductSizes[ i ] = rm.m_aiProductSizes[ i ];
	}
	m_raData = rm.m_raData.copy();	
}

template< typename T, int nDimensions >
MatrixT< T, nDimensions >& MatrixT< T, nDimensions >::operator = ( const MatrixT< T, nDimensions >& rm )
{
	m_qvSizes = rm.m_qvSizes;
	m_raData = rm.m_raData.copy();
	return( *this );
}

// virtual
template< typename T, int nDimensions >
MatrixT< T, nDimensions >::~MatrixT()
{
	// data automatically deleted
}

template< typename T, int nDimensions >
T& MatrixT< T, nDimensions >::operator () ( int i, ... )
{
	va_list argptr;
	va_start( argptr, i );

	assert( i >= 0 && i < m_aiSizes[ 0 ] );

	int index = i;

	for( int j = 1; j < nDimensions; ++j )
	{
		int arg = va_arg( argptr, int );
		assert( arg >= 0 && arg < m_aiSizes[ j ] );

		index += arg * m_aiProductSizes[ j ];
	}
	va_end( argptr );

	return m_raData[ index ];
}

template< typename T, int nDimensions >
const T& MatrixT< T, nDimensions >::operator () ( int i, ... ) const
{
	va_list argptr;
	va_start( argptr, i );

	assert( i >= 0 && i < m_aiSizes[ 0 ] );

	int index = i;

	for( int j = 1; j < nDimensions; ++j )
	{
		int arg = va_arg( argptr, int );
		assert( arg >= 0 && arg < m_aiSizes[ j ] );

		index += arg * m_aiProductSizes[ j ];
	}
	va_end( argptr );

	return m_raData[ index ];
}

template< typename T, int nDimensions >
ReferenceCountedArray< T > MatrixT< T, nDimensions >::data() const
{
	return m_raData;
}

template< typename T, int nDimensions >
int MatrixT< T, nDimensions >::numElements() const
{
	return m_raData.length();
}

template< typename T, int nDimensions >
int MatrixT< T, nDimensions >::numDimensions() const
{
	return nDimensions;
}

template< typename T, int nDimensions >
MatrixT< int, 1 > MatrixT< T, nDimensions >::size() const
{
	IntMx1 mSize( nDimensions );

	for( int i = 0; i < nDimensions; ++i )
	{
		mSize( i ) = m_aiSizes[ i ];
	}

	return mSize;
}

template< typename T, int nDimensions >
void MatrixT< T, nDimensions >::resize( int size0, ... )
{
	assert( size0 > 0 );

	int newNumElements;
	int newSizes[ nDimensions ];

	va_list argptr;
	va_start( argptr, size0 );

	// get the 0th size
	newSizes[ 0 ] = size0;

	// accumulate nElements
	int newNumElements = size0;

	for( int j = 1; j < nDimensions; ++j )
	{
		int arg = va_arg( argptr, int );
		assert( arg > 0 );

		newSizes[ j ] = arg;		
		newNumElements *= arg;
	}
	va_end( argptr );

	

	for







	// recompute product sizes
	m_aiProductSizes[ 0 ] = 1;
	for( int j = 1; j < nDimensions; ++j )
	{
		m_aiProductSizes[ j ] = m_aiSizes[ j - 1 ] * m_aiProductSizes[ j - 1 ];
	}

	m_raData = ReferenceCountedArray< T >( nElements );	
}

#endif
