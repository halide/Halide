#ifndef ARRAY_3D_H
#define ARRAY_3D_H

#include <cstdio>

// A simple 3D array class (with row-major storage)
template< typename T >
class Array3D
{
public:	

	Array3D();
	Array3D( const char* filename );
	Array3D( int width, int height, int depth );
	Array3D( const Array3D& copy );
	Array3D& operator = ( const Array3D& copy );
	virtual ~Array3D();
	
	bool isNull() const;
	bool notNull() const;

	int width() const;
	int height() const;
	int depth() const;
	int numElements() const;

	void fill( const T& val );
	void resize( int width, int height, int depth );

	// Returns a pointer to the beginning of the y-th row of the z-th slice
	T* getRowPointer( int y, int z ) const;

	// Returns a pointer to the beginning of the z-th slice
	T* getSlicePointer( int z ) const;

	operator T* () const;

	T operator () ( int k ) const; // read
	T& operator () ( int k ); // write

	T operator () ( int x, int y, int z ) const; // read
	T& operator () ( int x, int y, int z ); // write

	bool load( const char* filename );
	bool save( const char* filename );

private:
	
	int m_width;
	int m_height;
	int m_depth;
	T* m_array;

};

template< typename T >
Array3D< T >::Array3D() :

	m_width( -1 ),
	m_height( -1 ),
	m_depth( -1 ),
	m_array( NULL )

{
	
}

template< typename T >
Array3D< T >::Array3D( const char* filename ) :

	m_width( -1 ),
	m_height( -1 ),
	m_depth( -1 ),
	m_array( NULL )

{
	load( filename );
}

template< typename T >
Array3D< T >::Array3D( int width, int height, int depth )
{
	m_width = width;
	m_height = height;
	m_depth = depth;
	m_array = new T[ width * height * depth ];
}

template< typename T >
Array3D< T >::Array3D( const Array3D& copy )
{
	m_width = copy.m_width;
	m_height = copy.m_height;
	m_depth = copy.m_depth;

	m_array = new T[ m_width * m_height * m_depth ];
	memcpy( m_array, copy.m_array, m_width * m_height * m_depth * sizeof( T ) );
}

template< typename T >
Array3D< T >& Array3D< T >::operator = ( const Array3D< T >& copy )
{
	if( this != &copy )
	{
		m_width = copy.m_width;
		m_height = copy.m_height;
		m_depth = copy.m_depth;

		m_array = new T[ m_width * m_height * m_depth ];
		memcpy( m_array, copy.m_array, m_width * m_height * m_depth * sizeof( T ) );
	}
	return *this;
}

template< typename T >
// virtual
Array3D< T >::~Array3D()
{
	if( m_array != NULL )
	{
		delete[] m_array;
	}
}

template< typename T >
bool Array3D< T >::isNull() const
{
	return( m_array == NULL );
}

template< typename T >
bool Array3D< T >::notNull() const
{
	return( m_array != NULL );
}

template< typename T >
int Array3D< T >::width() const
{
	return m_width;
}

template< typename T >
int Array3D< T >::height() const
{
	return m_height;
}

template< typename T >
int Array3D< T >::depth() const
{
	return m_depth;
}

template< typename T >
int Array3D< T >::numElements() const
{
	return m_width * m_height * m_depth;
}

template< typename T >
void Array3D< T >::fill( const T& val )
{
	int ne = numElements();
	for( int i = 0; i < ne; ++i )
	{
		m_array[ i ] = val;
	}
}

template< typename T >
void Array3D< T >::resize( int width, int height, int depth )
{
	if( width * height * depth != m_width * m_height * m_depth )
	{
		delete[] m_array;
		m_array = new T[ width * height * depth ];
	}

	m_width = width;
	m_height = height;
	m_depth = depth;
}

template< typename T >
T* Array3D< T >::getRowPointer( int y, int z ) const
{
	return &( m_array[ z * m_width * m_height + y * m_width ] );
}

template< typename T >
T* Array3D< T >::getSlicePointer( int z ) const
{
	return &( m_array[ z * m_width * m_height ] );
}

template< typename T >
Array3D< T >::operator T* () const
{
	return m_array;
}

template< typename T >
T Array3D< T >::operator () ( int k ) const
{
	return m_array[ k ];
}

template< typename T >
T& Array3D< T >::operator () ( int k )
{
	return m_array[ k ];
}

template< typename T >
T Array3D< T >::operator () ( int x, int y, int z ) const
{
	int k = z * m_width * m_height + y * m_width + x;
	return m_array[ k ];
}

template< typename T >
T& Array3D< T >::operator () ( int x, int y, int z )
{
	int k = z * m_width * m_height + y * m_width + x;
	return m_array[ k ];
}

template< typename T >
bool Array3D< T >::load( const char* filename )
{
	FILE* fp = fopen( filename, "rb" );
	if( fp == NULL )
	{
		return false;
	}

	// TODO: ensure that the read was successful
	// then delete the old array, and swap
	m_width = -1;
	m_height = -1;
	m_depth = -1;
	if( m_array != NULL )
	{
		delete[] m_array;
	}

	int width;
	int height;
	int depth;

	fread( &width, sizeof( int ), 1, fp );
	fread( &height, sizeof( int ), 1, fp );
	fread( &depth, sizeof( int ), 1, fp );

	m_width = width;
	m_height = height;
	m_depth = depth;
	m_array = new T[ width * height * depth ];

	fread( m_array, sizeof( T ), width * height * depth, fp );	

	fclose( fp );

	return false;
}

template< typename T >
bool Array3D< T >::save( const char* filename )
{
	FILE* fp = fopen( filename, "wb" );
	if( fp == NULL )
	{
		return false;
	}

	fwrite( &m_width, sizeof( int ), 1, fp );
	fwrite( &m_height, sizeof( int ), 1, fp );
	fwrite( &m_depth, sizeof( int ), 1, fp );
	fwrite( m_array, sizeof( T ), m_width * m_height * m_depth, fp );
	fclose( fp );

	return true;
}

#endif // ARRAY_3D_H
