#ifndef CUDA_VECTOR_2D_H
#define CUDA_VECTOR_2D_H

#include <cuda_runtime.h>
//#include <cutil.h>
#include "cutil_standin.h"

#include <common/Array2D.h>

#include "DeviceVector2D.h"

// Basic 2D array interface around CUDA global memory
// Wraps around cudaMallocPitch() (linear allocation with pitch)
template< typename T >
class CUDAVector2D
{
public:

	CUDAVector2D();
	CUDAVector2D( int width, int height );
	CUDAVector2D( const Array2D< T >& src );
	virtual ~CUDAVector2D();
	
	bool isNull() const;
	bool notNull() const;

	int width() const;
	int height() const;
	int numElements() const;

	// The number of bytes between rows
	size_t pitch() const;	

	// Total size of the data in bytes (counting alignment)
	size_t sizeInBytes() const;

	// resizes the vector
	// original data is not preserved
	void resize( int width, int height );

	// sets the vector to 0 (all bytes to 0)
	void clear();

	// copy from cudaArray src to this
	void copyFromArray( cudaArray* src );

	// copy from this to cudaArray dst
	void copyToArray( cudaArray* dst );

	// copy from host array src to this
	void copyFromHost( const Array2D< T >& src );

	// copy from this to host array dst
	void copyToHost( Array2D< T >& dst ) const;
	
	// copy length() elements from device vector --> host
	// void copyToHost( void* output );

	// implicit cast to device pointer
	operator T* () const;

	T* devicePtr() const;

	DeviceVector2D< T > deviceVector() const;

	// TODO: constructor from filename: just use load	

	void load( const char* filename );
	void save( const char* filename ) const;

private:

	int m_width;
	int m_height;
	size_t m_pitch;
	size_t m_sizeInBytes;
	T* m_devicePtr;

	// frees the memory if this is not null
	void destroy();

	// Size of one row in bytes (not counting alignment)
	// Used for cudaMemset, which requires both a pitch and the original width
	size_t widthInBytes() const;
};

template< typename T >
CUDAVector2D< T >::CUDAVector2D() :

	m_width( -1 ),
	m_height( -1 ),

	m_pitch( 0 ),
	m_sizeInBytes( 0 ),
	m_devicePtr( NULL )

{
}

template< typename T >
CUDAVector2D< T >::CUDAVector2D( int width, int height ) :

	m_width( -1 ),
	m_height( -1 ),

	m_pitch( 0 ),
	m_sizeInBytes( 0 ),
	m_devicePtr( NULL )

{
	resize( width, height );
}

template< typename T >
CUDAVector2D< T >::CUDAVector2D( const Array2D< T >& src ) :

	m_width( -1 ),
	m_height( -1 ),

	m_pitch( 0 ),
	m_sizeInBytes( 0 ),
	m_devicePtr( NULL )

{
	resize( src.width(), src.height() );
	copyFromHost( src );
}

template< typename T >
// virtual
CUDAVector2D< T >::~CUDAVector2D()
{
	destroy();
}

template< typename T >
bool CUDAVector2D< T >::isNull() const
{
	return( m_devicePtr == NULL );
}

template< typename T >
bool CUDAVector2D< T >::notNull() const
{
	return( m_devicePtr != NULL );
}

template< typename T >
int CUDAVector2D< T >::width() const
{
	return m_width;
}

template< typename T >
int CUDAVector2D< T >::height() const
{
	return m_height;
}

template< typename T >
int CUDAVector2D< T >::numElements() const
{
	return m_width * m_height;
}

template< typename T >
size_t CUDAVector2D< T >::pitch() const
{
	return m_pitch;
}

template< typename T >
size_t CUDAVector2D< T >::sizeInBytes() const
{
	return m_sizeInBytes;
}

template< typename T >
void CUDAVector2D< T >::resize( int width, int height )
{
	if( width == m_width && height == m_height )
	{
		return;
	}

	destroy();

	m_width = width;
	m_height = height;

	CUDA_SAFE_CALL
	(
		cudaMallocPitch
		(
			reinterpret_cast< void** >( &m_devicePtr ),
			&m_pitch,
			m_width * sizeof( T ),
			m_height
		)
	);

	m_sizeInBytes = m_pitch * height;
}

template< typename T >
void CUDAVector2D< T >::clear()
{
	CUDA_SAFE_CALL( cudaMemset2D( devicePtr(), pitch(), 0, widthInBytes(), height() ) );
}

template< typename T >
void CUDAVector2D< T >::copyFromArray( cudaArray* src )
{
	CUDA_SAFE_CALL
	(
		cudaMemcpy2DFromArray
		(
			devicePtr(), pitch(),			
			src,
			0, 0,
			widthInBytes(), height(),
			cudaMemcpyDeviceToDevice
		)
	);
}

template< typename T >
void CUDAVector2D< T >::copyToArray( cudaArray* dst )
{
	CUDA_SAFE_CALL
	(
		cudaMemcpy2DToArray
		(
			dst,
			0, 0,
			devicePtr(), pitch(),
			widthInBytes(), height(),
			cudaMemcpyDeviceToDevice
		)
	);
}

template< typename T >
void CUDAVector2D< T >::copyFromHost( const Array2D< T >& src )
{
	CUDA_SAFE_CALL
	(
		cudaMemcpy2D
		(
			devicePtr(), pitch(),
			src, src.width() * sizeof( T ),
			src.width() * sizeof( T ), src.height(),
			cudaMemcpyHostToDevice
		)
	);
}

template< typename T >
void CUDAVector2D< T >::copyToHost( Array2D< T >& dst ) const
{
	CUDA_SAFE_CALL
	(
		cudaMemcpy2D
		(
			dst, dst.width() * sizeof( T ),
			devicePtr(), pitch(),
			widthInBytes(), height(),
			cudaMemcpyDeviceToHost
		)
	);
}

template< typename T >
CUDAVector2D< T >::operator T* () const
{
	return m_devicePtr;
}

template< typename T >
T* CUDAVector2D< T >::devicePtr() const
{
	return m_devicePtr;
}

template< typename T >
DeviceVector2D< T > CUDAVector2D< T >::deviceVector() const
{
	return DeviceVector2D< T >( m_devicePtr, m_width, m_height, m_pitch );
}

template< typename T >
void CUDAVector2D< T >::load( const char* filename )
{
	Array2D< T > h_arr( filename );
	if( !( h_arr.isNull() ) )
	{
		resize( h_arr.width(), h_arr.height() );
		copyFromHost( h_arr );
	}
}

template< typename T >
void CUDAVector2D< T >::save( const char* filename ) const
{
	Array2D< T > h_arr( width(), height() );
	copyToHost( h_arr );
	h_arr.save( filename );
}

template< typename T >
void CUDAVector2D< T >::destroy()
{
	if( notNull() )
	{
		CUDA_SAFE_CALL( cudaFree( m_devicePtr ) );
		m_devicePtr = NULL;
	}

	m_width = -1;
	m_height = -1;
	m_pitch = 0;
	m_sizeInBytes = 0;
}

template< typename T >
size_t CUDAVector2D< T >::widthInBytes() const
{
	return m_width * sizeof( T );
}

#endif // CUDA_VECTOR_2D_H
