#ifndef ARRAY_WITH_LENGTH_H
#define ARRAY_WITH_LENGTH_H

#include "BasicTypes.h"

// Thin wrapper around raw pointer for an array
template< typename T >
class ArrayWithLength
{
public:

	ArrayWithLength< T >()
	{
		m_arr = NULL;
		m_iLength = -1;
	}

	ArrayWithLength< T >( T* arr, int length )
	{
		m_arr = arr;
		m_iLength = length;
	}

	bool isValid()
	{
		return( m_iLength != -1 );
	}

	T* data()
	{
		return m_arr;
	}

	int length() const
	{
		return m_iLength;
	}

	operator T* ()
	{
		return m_arr;
	}

	T operator [] ( int k ) const
	{
		return m_arr[ k ];
	}

	void destroy()
	{
		delete m_arr;
		m_arr = NULL;
		m_iLength = -1;
	}

private:

	T* m_arr;
	int m_iLength;
};

typedef ArrayWithLength< float >	floatArray;
typedef ArrayWithLength< ubyte >	unsignedByteArray;

#endif
