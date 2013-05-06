#ifndef CUDA_VECTOR_3D_H
#define CUDA_VECTOR_3D_H

#include <cuda_runtime.h>
//#include <cutil.h>
#include "cutil_standin.h"

#include <common/Array3D.h>

template< typename T >
struct DeviceArray3D
{
	cudaPitchedPtr pitchedPointer;
	int width;
	int height;
	int depth;
	size_t slicePitch;

	__host__
	DeviceArray3D( cudaPitchedPtr _pitchedPointer, int _width, int _height, int _depth ) :

		pitchedPointer( _pitchedPointer ),
		width( _width ),
		height( _height ),
		depth( _depth ),
		slicePitch( _pitchedPointer.pitch * _pitchedPointer.ysize )

	{

	}

	__device__
	T* getRowPointer( int y, int z )
	{
		// TODO: char --> ubyte
		char* p = reinterpret_cast< char* >( pitchedPointer.ptr );
	
		size_t rowPitch = pitchedPointer.pitch;

		// TODO: switch pointer arithmetic to array indexing?
		char* pSlice = p + z * slicePitch;
		return reinterpret_cast< T* >( pSlice + y * rowPitch );
	}

	__device__
	T* getSlicePointer( int z )
	{
		// TODO: char --> ubyte
		char* p = reinterpret_cast< char* >( pitchedPointer.ptr );

		// TODO: switch pointer arithmetic to array indexing?
		return reinterpret_cast< T* >( p + z * slicePitch );
	}

	__device__
	T& operator () ( int x, int y, int z )
	{
		return getRowPointer( y, z )[ x ];
	}

	__device__
	T& operator () ( int3 xyz )
	{
		return getRowPointer( xyz.y, xyz.z )[ xyz.x ];
	}
};

// Basic 3D array interface around CUDA global memory
// Wraps around cudaMalloc3D() (linear allocation with pitch)
template< typename T >
class CUDAVector3D
{
public:

	CUDAVector3D();
	CUDAVector3D( int width, int height, int depth );
	CUDAVector3D( const Array3D< T >& src );
	virtual ~CUDAVector3D();
	
	bool isNull() const;
	bool notNull() const;

	int width() const;
	int height() const;
	int depth() const;
	int numElements() const;

	size_t rowPitch() const;
	size_t slicePitch() const;

	// Total size of the data in bytes (counting alignment)
	size_t sizeInBytes() const;

	// resizes the vector
	// original data is not preserved
	void resize( int width, int height, int depth );

	// sets the vector to 0 (all bytes to 0)
	void clear();

	// copy from host array src to this
	void copyFromHost( const Array3D< T >& src );

	// copy from this to host array dst
	void copyToHost( Array3D< T >& dst ) const;
	
	// copy length() elements from device vector --> host
	// void copyToHost( void* output );

	// implicit cast to pitched pointer?
	operator cudaPitchedPtr() const;

	cudaPitchedPtr pitchedPointer() const;

	DeviceArray3D< T > deviceArray() const;

	// TODO: constructor from filename: just use load
	void load( const char* filename );
	void save( const char* filename ) const;

private:

	int m_width;
	int m_height;
	int m_depth;

	size_t m_sizeInBytes;
	cudaPitchedPtr m_pitchedPointer;
	cudaExtent m_extent;

	// frees the memory if this is not null
	void destroy();	
};

template< typename T >
CUDAVector3D< T >::CUDAVector3D() :

	m_width( -1 ),
	m_height( -1 ),
	m_depth( -1 ),

	m_sizeInBytes( 0 )

{
	m_pitchedPointer.ptr = NULL;
	m_pitchedPointer.pitch = 0;
	m_pitchedPointer.xsize = 0;
	m_pitchedPointer.ysize = 0;

	m_extent = make_cudaExtent( 0, 0, 0 );
}

template< typename T >
CUDAVector3D< T >::CUDAVector3D( int width, int height, int depth ) :

	m_width( -1 ),
	m_height( -1 ),
	m_depth( -1 ),

	m_sizeInBytes( 0 )

{
	m_pitchedPointer.ptr = NULL;
	m_pitchedPointer.pitch = 0;
	m_pitchedPointer.xsize = 0;
	m_pitchedPointer.ysize = 0;

	m_extent = make_cudaExtent( 0, 0, 0 );

	resize( width, height, depth );
}

template< typename T >
CUDAVector3D< T >::CUDAVector3D( const Array3D< T >& src ) :

	m_width( -1 ),
	m_height( -1 ),
	m_depth( -1 ),

	m_sizeInBytes( 0 )	

{
	m_pitchedPointer.ptr = NULL;
	m_pitchedPointer.pitch = 0;
	m_pitchedPointer.xsize = 0;
	m_pitchedPointer.ysize = 0;

	m_extent = make_cudaExtent( 0, 0, 0 );

	resize( src.width(), src.height(), src.depth() );
	copyFromHost( src );
}

template< typename T >
// virtual
CUDAVector3D< T >::~CUDAVector3D()
{
	destroy();
}

template< typename T >
bool CUDAVector3D< T >::isNull() const
{
	return( m_pitchedPointer.ptr == NULL );
}

template< typename T >
bool CUDAVector3D< T >::notNull() const
{
	return( m_pitchedPointer.ptr != NULL );
}

template< typename T >
int CUDAVector3D< T >::width() const
{
	return m_width;
}

template< typename T >
int CUDAVector3D< T >::height() const
{
	return m_height;
}

template< typename T >
int CUDAVector3D< T >::depth() const
{
	return m_depth;
}

template< typename T >
int CUDAVector3D< T >::numElements() const
{
	return m_width * m_height;
}

template< typename T >
size_t CUDAVector3D< T >::rowPitch() const
{
	return m_pitchedPointer.pitch;
}

template< typename T >
size_t CUDAVector3D< T >::slicePitch() const
{
	return m_pitchedPointer.pitch * m_height;
}

template< typename T >
size_t CUDAVector3D< T >::sizeInBytes() const
{
	return m_sizeInBytes;
}

template< typename T >
void CUDAVector3D< T >::resize( int width, int height, int depth )
{
	if( width == m_width &&
		height == m_height &&
		depth == m_depth )
	{
		return;
	}

	destroy();

	m_width = width;
	m_height = height;
	m_depth = depth;
	m_extent = make_cudaExtent( width * sizeof( T ), height, depth );

	CUDA_SAFE_CALL
	(
		cudaMalloc3D( &m_pitchedPointer, m_extent )
	);

	m_sizeInBytes = m_pitchedPointer.pitch * height * depth;
}

template< typename T >
void CUDAVector3D< T >::clear()
{
	CUDA_SAFE_CALL( cudaMemset3D( m_pitchedPointer, 0, m_extent ) );
}

template< typename T >
void CUDAVector3D< T >::copyFromHost( const Array3D< T >& src )
{
	cudaMemcpy3DParms params;

	params.kind = cudaMemcpyHostToDevice;

	// Since the source (on the host) is not pitched
	// make a pitchedPointer for it
	params.srcPtr = make_cudaPitchedPtr( src, src.width() * sizeof( T ), src.width(), src.height() );
	params.srcArray = NULL; // we're not copying a CUDA array
	params.srcPos = make_cudaPos( 0, 0, 0 );
	
	params.dstPtr = m_pitchedPointer;
	params.dstArray = NULL; // we're not copying a CUDA array
	params.dstPos = make_cudaPos( 0, 0, 0 );	

	params.extent = m_extent;	

	CUDA_SAFE_CALL( cudaMemcpy3D( &params ) );
}

template< typename T >
void CUDAVector3D< T >::copyToHost( Array3D< T >& dst ) const
{
	cudaMemcpy3DParms params;

	params.kind = cudaMemcpyDeviceToHost;

	params.srcPtr = m_pitchedPointer;
	params.srcArray = NULL; // we're not copying a CUDA array
	params.srcPos = make_cudaPos( 0, 0, 0 );
	
	// Since the destination (on the host) is not pitched
	// make a pitchedPointer for it
	params.dstPtr = make_cudaPitchedPtr( dst, dst.width() * sizeof( T ), dst.width(), dst.height() );
	params.dstArray = NULL; // we're not copying a CUDA array
	params.dstPos = make_cudaPos( 0, 0, 0 );
	
	params.extent = m_extent;

	CUDA_SAFE_CALL( cudaMemcpy3D( &params ) );
}

template< typename T >
CUDAVector3D< T >::operator cudaPitchedPtr() const
{
	return m_pitchedPointer;
}

template< typename T >
cudaPitchedPtr CUDAVector3D< T >::pitchedPointer() const
{
	return m_pitchedPointer;
}

template< typename T >
DeviceArray3D< T > CUDAVector3D< T >::deviceArray() const
{
	return DeviceArray3D< T >( m_pitchedPointer, m_width, m_height, m_depth );
}

template< typename T >
void CUDAVector3D< T >::load( const char* filename )
{
	Array3D< T > h_arr( filename );
	if( !( h_arr.isNull() ) )
	{
		resize( h_arr.width(), h_arr.height(), h_arr.depth() );
		copyFromHost( h_arr );
	}
}

template< typename T >
void CUDAVector3D< T >::save( const char* filename ) const
{
	Array3D< T > h_arr( width(), height(), height() );
	copyToHost( h_arr );
	h_arr.save( filename );
}

template< typename T >
void CUDAVector3D< T >::destroy()
{
	if( notNull() )
	{
		CUDA_SAFE_CALL( cudaFree( m_pitchedPointer.ptr ) );
		m_pitchedPointer.ptr = NULL;
		m_pitchedPointer.pitch = 0;
		m_pitchedPointer.xsize = 0;
		m_pitchedPointer.ysize = 0;
	}

	m_width = -1;
	m_height = -1;
	m_depth = -1;

	m_sizeInBytes = 0;

	m_extent = make_cudaExtent( 0, 0, 0 );
}

#endif // CUDA_VECTOR_3D_H
