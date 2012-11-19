#ifndef CUDA_VECTOR_H
#define CUDA_VECTOR_H

#include <cuda_runtime.h>
//#include <cutil.h>
#include "cutil_standin.h"

// Basic vector interface around CUDA global memory
// Wraps around cudaMalloc() (linear allocation)
template< typename T >
class CUDAVector
{
public:

	CUDAVector();
	CUDAVector( int length );
	virtual ~CUDAVector();

	bool isNull() const;
	bool notNull() const;

	int length() const;
	size_t sizeInBytes() const;

	// sets the vector to 0 (all bytes to 0)
	void clear();

	// resizes the vector
	// original data is not preserved
	void resize( int length );

	// copy length() elements from input --> device vector
	void copyFromHost( void* input );

	// TODO: look into this - since with Fermi, we can use pointer arithmetic
	// copy nElements starting at input[ inputOffset ] --> device_vector[ outputOffset ]
	// void copyFromHost( T* input, int inputOffset, int outputOffset, int nElements );

	// copy length() elements from device vector --> host
	void copyToHost( void* output );

	// implicit cast to device pointer
	operator T* () const;

	T* devicePtr() const;

private:

	int m_sizeInBytes;
	int m_length;
	T* m_devicePtr;

	void destroy();
};

template< typename T >
CUDAVector< T >::CUDAVector() :

	m_sizeInBytes( 0 ),
	m_length( -1 ),
	m_devicePtr( NULL )

{

}


template< typename T >
CUDAVector< T >::CUDAVector( int length ) :

	m_sizeInBytes( 0 ),
	m_length( -1 ),
	m_devicePtr( NULL )

{
	resize( length );
}

template< typename T >
// virtual
CUDAVector< T >::~CUDAVector()
{
	destroy();	
}

template< typename T >
bool CUDAVector< T >::isNull() const
{
	return( m_devicePtr == NULL );
}

template< typename T >
bool CUDAVector< T >::notNull() const
{
	return( m_devicePtr != NULL );
}

template< typename T >
int CUDAVector< T >::length() const
{
	return m_length;
}

template< typename T >
size_t CUDAVector< T >::sizeInBytes() const
{
	return m_sizeInBytes;
}

template< typename T >
void CUDAVector< T >::clear()
{
	cudaMemset( m_devicePtr, 0, m_sizeInBytes );
}

template< typename T >
void CUDAVector< T >::resize( int length )
{
	if( m_length == length )
	{
		return;
	}

	destroy();

	m_length = length;
	m_sizeInBytes = length * sizeof( T );

	CUDA_SAFE_CALL( cudaMalloc( reinterpret_cast< void** >( &m_devicePtr ), m_sizeInBytes ) );
}

template< typename T >
void CUDAVector< T >::copyFromHost( void* input )
{
	CUDA_SAFE_CALL( cudaMemcpy( m_devicePtr, input, m_sizeInBytes, cudaMemcpyHostToDevice ) );
}

template< typename T >
void CUDAVector< T >::copyToHost( void* output )
{
	CUDA_SAFE_CALL( cudaMemcpy( output, m_devicePtr, m_sizeInBytes, cudaMemcpyDeviceToHost ) );
}

template< typename T >
CUDAVector< T >::operator T* () const
{
	return m_devicePtr;
}

template< typename T >
T* CUDAVector< T >::devicePtr() const
{
	return m_devicePtr;
}

template< typename T >
void CUDAVector< T >::destroy()
{
	if( notNull() )
	{
		CUDA_SAFE_CALL( cudaFree( m_devicePtr ) );
		m_devicePtr = NULL;
	}

	m_sizeInBytes = 0;
	m_length = -1;	
}

#endif // CUDA_VECTOR_H
