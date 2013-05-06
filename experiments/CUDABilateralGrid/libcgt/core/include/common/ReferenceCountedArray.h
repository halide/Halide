#ifndef REFERENCE_COUNTED_ARRAY_H
#define REFERENCE_COUNTED_ARRAY_H

#include <cassert>

#include "BasicTypes.h"

template< typename T >
class ReferenceCountedArray
{
public:

	// Constructors / Destructors
	ReferenceCountedArray(); // a NULL array

	// initialize an empty array with a length (uses default constructor)
	ReferenceCountedArray( uint arrayLength, const T& fill = T(), uint* refCount = NULL );
	
	// initialize from an array
	ReferenceCountedArray( T* arrayData, uint arrayLength, uint* refCount = NULL );
	ReferenceCountedArray( const ReferenceCountedArray< T >& other );
	virtual ~ReferenceCountedArray();
	ReferenceCountedArray< T >& operator = ( const ReferenceCountedArray< T >& other );

	ReferenceCountedArray< T > copy() const;
	ReferenceCountedArray< T > copy( int start, int count ) const;

	// Explicit cast to C array
	T* data();
	const T* constData() const;

	// Implicit cast to C array
	operator T* ();
	operator const T* () const;

	uint length() const;

private:

	T* m_aData;
	uint m_uiLength;
	mutable uint* m_uiRefCount;

	void destroy();	
};

typedef ReferenceCountedArray< ubyte > UnsignedByteArray;
typedef ReferenceCountedArray< int > IntArray;
typedef ReferenceCountedArray< uint > UnsignedIntArray;
typedef ReferenceCountedArray< float > FloatArray;
typedef ReferenceCountedArray< double > DoubleArray;

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

template< typename T >
ReferenceCountedArray< T >::ReferenceCountedArray() :

m_aData( NULL ),
m_uiLength( 0 ),
m_uiRefCount( NULL )

{

}

template< typename T >
ReferenceCountedArray< T >::ReferenceCountedArray( uint arrayLength, const T& fill, uint* refCount ) :

m_uiLength( arrayLength ),
m_uiRefCount( refCount )

{
	m_aData = new T[ arrayLength ];
	for( uint i = 0; i < arrayLength; ++i )
	{
		m_aData[ i ] = fill;
	}

	if( m_uiRefCount == NULL )
	{
		m_uiRefCount = new uint( 1 );
	}
	else
	{
		++( *m_uiRefCount );
	}
}

template< typename T >
ReferenceCountedArray< T >::ReferenceCountedArray( T* arrayData, uint arrayLength, uint* refCount ) :

m_aData( arrayData ),
m_uiLength( arrayLength ),
m_uiRefCount( refCount )

{
	if( m_uiRefCount == NULL )
	{
		m_uiRefCount = new uint( 1 );
	}
	else
	{
		++( *m_uiRefCount );
	}
}

template< typename T >
ReferenceCountedArray< T >::ReferenceCountedArray( const ReferenceCountedArray< T >& other ) :

m_aData( other.m_aData ),
m_uiLength( other.m_uiLength ),
m_uiRefCount( other.m_uiRefCount )

{
	++( *m_uiRefCount );
}

// virtual
template< typename T >
ReferenceCountedArray< T >::~ReferenceCountedArray()
{
	destroy();
}

template< typename T >
ReferenceCountedArray< T >& ReferenceCountedArray< T >::operator = ( const ReferenceCountedArray< T >& other )
{
	if( &other != this )
	{
		destroy();
		m_aData = other.m_aData;
		m_uiLength = other.m_uiLength;
		m_uiRefCount = other.m_uiRefCount;
		++( *m_uiRefCount );
	}
	return *this;
}

template< typename T >
ReferenceCountedArray< T > ReferenceCountedArray< T >::copy() const
{
	T* data = new T[ m_uiLength ];
	for( uint i = 0; i < m_uiLength; ++i )
	{
		data[ i ] = m_aData[ i ];
	}

	return ReferenceCountedArray< T >( data, m_uiLength );
}

template< typename T >
ReferenceCountedArray< T > ReferenceCountedArray< T >::copy( int start, int count ) const
{
	assert( start >= 0 );
	assert( ( start + count ) <= m_uiLength );

	T* data = new T[ count ];
	for( int i = 0; i < count; ++i )
	{
		data[ i ] = m_aData[ start + i ];
	}

	return ReferenceCountedArray< T >( data, count );
}

template< typename T >
T* ReferenceCountedArray< T >::data()
{
	return m_aData;
}

template< typename T >
const T* ReferenceCountedArray< T >::constData() const
{
	return m_aData;
}

template< typename T >
ReferenceCountedArray< T >::operator T* ()
{
	return m_aData;
}

template< typename T >
ReferenceCountedArray< T >::operator const T* () const
{
	return m_aData;
}

template< typename T >
uint ReferenceCountedArray< T >::length() const
{
	return m_uiLength;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

template< typename T >
void ReferenceCountedArray< T >::destroy()
{
	// if it's valid
	if( m_uiRefCount != NULL )
	{
		--( *m_uiRefCount );
		if( *m_uiRefCount == 0 )
		{
			delete m_uiRefCount;
			delete[] m_aData;
		}
	}	
}

#endif
