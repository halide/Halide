#include "GL/GLVertexBufferObject.h"

#include <cassert>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>

using namespace std;

// static
GLBufferObject* GLVertexBufferObject::fromVector3fVector( vector< Vector3f >* pvData,
														 GLBufferObject::GLBufferObjectUsage usage )
{
	// each element is a vertex
	// each vertex is 3 floats
	// ==> each element = 3 * sizeof( float ) bytes
	int nElements = static_cast< int >( pvData->size() );
	int nBytesPerElement = 3 * sizeof( float );
	int nFloats = 3 * nElements;	

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER, usage,
		nElements, nBytesPerElement,
		NULL );
		
	// write data into buffer
	float* data = reinterpret_cast< float* >( pBufferObject->map( GLBufferObject::TARGET_ARRAY_BUFFER, GLBufferObject::ACCESS_WRITE_ONLY ) );
	for( uint i = 0; i < pvData->size(); ++i )
	{
		data[ 3 * i ] = ( pvData->at( i ) )[0];
		data[ 3 * i + 1 ] = ( pvData->at( i ) )[1];
		data[ 3 * i + 2 ] = ( pvData->at( i ) )[2];
	}
	pBufferObject->unmap( GLBufferObject::TARGET_ARRAY_BUFFER );
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromVector2fVector( vector< Vector2f >* pvData,
														 GLBufferObject::GLBufferObjectUsage usage )
{
	// each element is a vertex
	// each vertex is 2 floats
	// ==> each element = 2 * sizeof( float ) bytes
	int nElements = static_cast< int >( pvData->size() );
	int nBytesPerElement = 2 * sizeof( float );
	int nFloats = 2 * nElements;	

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER, usage,
		nElements, nBytesPerElement,
		NULL );

	// write data into buffer
	float* data = reinterpret_cast< float* >( pBufferObject->map( GLBufferObject::TARGET_ARRAY_BUFFER, GLBufferObject::ACCESS_WRITE_ONLY ) );
	for( uint i = 0; i < pvData->size(); ++i )
	{
		data[ 2 * i ] = ( pvData->at( i ) )[0];
		data[ 2 * i + 1 ] = ( pvData->at( i ) )[1];
	}
	pBufferObject->unmap( GLBufferObject::TARGET_ARRAY_BUFFER );		
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromIntVector( vector< int >* pvData,
													GLBufferObject::GLBufferObjectUsage usage )
{
	// each element is an int
	// ==> each element is sizeof( int ) bytes
	int nElements = static_cast< int >( pvData->size() );
	int nBytesPerElement = sizeof( int );
	int nInts = nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER, usage,
		nElements, nBytesPerElement,
		NULL );

	// write data into buffer
	int* data = reinterpret_cast< int* >( pBufferObject->map( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER, GLBufferObject::ACCESS_WRITE_ONLY ) );
	for( uint i = 0; i < pvData->size(); ++i )
	{
		data[ i ] = pvData->at( i );
	}
	pBufferObject->unmap( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER );		
	pBufferObject->unbind( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromUnsignedIntVector( vector< uint >* pvData,
															GLBufferObject::GLBufferObjectUsage usage )
{
	// each element is an int
	// ==> each element is sizeof( int ) bytes
	int nElements = static_cast< int >( pvData->size() );
	int nBytesPerElement = sizeof( uint );
	int nInts = nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER, usage,
		nElements, nBytesPerElement,
		NULL );

	// write data into buffer
	uint* data = reinterpret_cast< uint* >( pBufferObject->map( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER, GLBufferObject::ACCESS_WRITE_ONLY ) );
	for( uint i = 0; i < pvData->size(); ++i )
	{
		data[ i ] = pvData->at( i );
	}
	pBufferObject->unmap( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER );		
	pBufferObject->unbind( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromFloatArray( float* afData, int nFloats,
													 int nElements,
													 GLBufferObject::GLBufferObjectUsage usage )
{
	int nBytes = nFloats * sizeof( float );
	assert( nBytes % nElements == 0 );
	int nBytesPerElement = nBytes / nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER,
		usage,
		nElements, nBytesPerElement,
		afData );
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromShortArray( GLshort* asData, int nShorts,
													 int nElements,
													 GLBufferObject::GLBufferObjectUsage usage )
{
	int nBytes = nShorts * sizeof( GLshort );
	assert( nBytes % nElements == 0 );
	int nBytesPerElement = nBytes / nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER,
		usage,
		nElements, nBytesPerElement,
		asData );
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromIntArray( int* aiData, int nInts, int nElements,
												   GLBufferObject::GLBufferObjectUsage usage )
												   
{
	int nBytes = nInts * sizeof( int );
	assert( nBytes % nElements == 0 );
	int nBytesPerElement = nBytes / nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER,
		usage,
		nElements, nBytesPerElement,
		aiData );
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromUnsignedIntArray( uint* auiData, int nUnsignedInts, int nElements,
														   GLBufferObject::GLBufferObjectUsage usage )
{
	int nBytes = nUnsignedInts * sizeof( uint );
	assert( nBytes % nElements == 0 );
	int nBytesPerElement = nBytes / nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER,
		usage,
		nElements, nBytesPerElement,
		auiData );
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}

// static
GLBufferObject* GLVertexBufferObject::fromUnsignedByteArray( const ubyte* aubData, int nUnsignedBytes, int nElements,
															GLBufferObject::GLBufferObjectUsage usage )
																  
{
	int nBytes = nUnsignedBytes * sizeof( ubyte );
	assert( nBytes % nElements == 0 );
	int nBytesPerElement = nBytes / nElements;

	GLBufferObject* pBufferObject = new GLBufferObject( GLBufferObject::TARGET_ARRAY_BUFFER,
		usage,
		nElements, nBytesPerElement,
		aubData );
	pBufferObject->unbind( GLBufferObject::TARGET_ARRAY_BUFFER );

	return pBufferObject;
}
