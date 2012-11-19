#ifndef CUDA_ARRAY_2D_H
#define CUDA_ARRAY_2D_H

#include <cuda_runtime.h>
// #include <cutil.h>
#include "cutil_standin.h"

#include "Array2D.h"

// Basic 2D array interface around CUDA global memory
// Wraps around cudaMallocArray() (CUDA Array, can only be used as textures)
template< typename T >
class CUDAArray2D
{
public:

	CUDAArray2D( int width, int height );
	virtual ~CUDAArray2D();

	cudaChannelFormatDesc channelFormatDescription() const;

	int width() const;
	int height() const;
	int numElements() const;

	// TODO: support offsets
	void copyFromHost( void* src ) const;
	void copyFromHost( const Array2D< T >& src ) const;
	
	// TODO: support offsets and sizes
	void copyToHost( Array2D< T >& dst ) const;
	void copyToHost( void* dst ) const;

	// implicit cast to CUDAArray2D
	operator cudaArray* () const;

	cudaArray* deviceArray() const;

private:

	int m_width;
	int m_height;
	cudaChannelFormatDesc m_cfd;
	int m_sizeInBytes;
	cudaArray* m_deviceArray;

};

template< typename T >
CUDAArray2D< T >::CUDAArray2D( int width, int height ) :

	m_width( width ),
	m_height( height ),
	m_sizeInBytes( width * height * sizeof( T ) )

{	
	m_cfd = cudaCreateChannelDesc< T >();

	CUDA_SAFE_CALL( cudaMallocArray( &m_deviceArray, &m_cfd, width, height ) );
}

template< typename T >
// virtual
CUDAArray2D< T >::~CUDAArray2D()
{
	cudaFreeArray( m_deviceArray );
}

template< typename T >
cudaChannelFormatDesc CUDAArray2D< T >::channelFormatDescription() const
{
	return m_cfd;
}

template< typename T >
int CUDAArray2D< T >::width() const
{
	return m_width;
}

template< typename T >
int CUDAArray2D< T >::height() const
{
	return m_height;
}

template< typename T >
int CUDAArray2D< T >::numElements() const
{
	return m_width * m_height;
}

template< typename T >
void CUDAArray2D< T >::copyFromHost( void* src ) const
{
	CUDA_SAFE_CALL
	(
		cudaMemcpyToArray
		(
			m_deviceArray,
			0, 0,
			src,
			m_sizeInBytes,
			cudaMemcpyHostToDevice
		)
	);
}

template< typename T >
void CUDAArray2D< T >::copyFromHost( const Array2D< T >& src ) const
{
	CUDA_SAFE_CALL
	(
		cudaMemcpyToArray
		(
			m_deviceArray,
			0, 0,
			src,
			m_sizeInBytes,
			cudaMemcpyHostToDevice
		)
	);
}

template< typename T >
void CUDAArray2D< T >::copyToHost( Array2D< T >& dst ) const
{
	CUDA_SAFE_CALL
	(
		cudaMemcpyFromArray
		(
			dst,
			m_deviceArray,
			0, 0,
			m_sizeInBytes,
			cudaMemcpyDeviceToHost
		)
	);
}

template< typename T >
void CUDAArray2D< T >::copyToHost( void* dst ) const
{
	CUDA_SAFE_CALL
	(
		cudaMemcpyFromArray
		(
			dst,
			m_deviceArray,
			0, 0,
			m_sizeInBytes,
			cudaMemcpyDeviceToHost
		)
	);
}

template< typename T >
CUDAArray2D< T >::operator cudaArray* () const
{
	return m_deviceArray;
}

template< typename T >
cudaArray* CUDAArray2D< T >::deviceArray() const
{
	return m_deviceArray;
}

#endif // CUDA_ARRAY_2D_H
