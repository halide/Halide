#include <GL/glew.h>
#include <cassert>
#include <common/ArrayWithLength.h>
#include <common/ArrayUtils.h>

#include "GL/GLBufferObject.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------
// Static
// -----------------------------------------------------------------------

// static
GLBufferObject* GLBufferObject::getBoundBufferObject( GLBufferObject::GLBufferObjectTarget target )
{
	return s_apBindingTable[ GLBufferObject::getBindingTableIndex( target ) ];
}

// static
void GLBufferObject::unbind( GLBufferObject::GLBufferObjectTarget target )
{
	if( getBoundBufferObject( target ) != NULL )
	{
		glBindBuffer( target, 0 );
		setBoundBufferObject( target, NULL );
	}
}

// -----------------------------------------------------------------------
// Non-Static
// -----------------------------------------------------------------------

GLBufferObject::GLBufferObject( GLBufferObject::GLBufferObjectTarget target,
							   GLBufferObject::GLBufferObjectUsage usage,
							   int nElements, int bytesPerElement,
							   const void* data ) :

	m_nElements( nElements ),
	m_nBytesPerElement( bytesPerElement ),
	m_nBytes( nElements * bytesPerElement )

{
	// printf( "GLBufferObject: allocating %f megabytes\n", nElements * bytesPerElement / 1048576.f );

	glGenBuffers( 1, &m_iBufferId );
	bind( target );
	glBufferData( target, m_nBytes, data, usage );
}

// static
char* GLBufferObject::convertOffsetToPointer( int i, int elementSize )
{
	int offset = i * elementSize;
	return( ( ( char* )NULL ) + offset );
}

// static
int GLBufferObject::convertPointerToOffset( char* pPtr, int elementSize )
{
	// TODO_x64
	int offsetBytes = ( int )( ( ( char* )pPtr ) - ( ( char* )NULL ) );

	assert( offsetBytes % elementSize == 0 ); // check alignment
	return offsetBytes / elementSize;
}

// virtual
GLBufferObject::~GLBufferObject()
{
	unbindAll();
	glDeleteBuffersARB( 1, &m_iBufferId );
}

int GLBufferObject::getNumElements()
{
	return m_nElements;
}

// get the number of bytes per element
int GLBufferObject::getNumBytesPerElement()
{
	return m_nBytesPerElement;
}

// gets the total number of bytes
int GLBufferObject::getNumBytes()
{
	return m_nBytes;
}

void GLBufferObject::bind( GLBufferObject::GLBufferObjectTarget target )
{
	if( !isBoundToTarget( target ) )
	{
		setBoundBufferObject( target, this );
		glBindBuffer( target, m_iBufferId );
	}
}

void GLBufferObject::unbindAll()
{
	unbind( GLBufferObject::TARGET_ARRAY_BUFFER );
	unbind( GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER );
	unbind( GLBufferObject::TARGET_PIXEL_PACK_BUFFER );
	unbind( GLBufferObject::TARGET_PIXEL_UNPACK_BUFFER );
}

void* GLBufferObject::map( GLBufferObject::GLBufferObjectTarget target,
						  GLBufferObject::GLBufferObjectAccess access )
{
	assert( target != TARGET_NO_TARGET );
	if( isBoundToTarget( target ) )
	{
		return glMapBuffer( target, access );
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer Object not bound to target\n" );
		assert( false );		
	}
#endif
	return NULL;
}

ubyte* GLBufferObject::mapToUnsignedByteArray( GLBufferObjectTarget target, GLBufferObjectAccess access )
{
	return reinterpret_cast< ubyte* >( map( target, access ) );
}

float* GLBufferObject::mapToFloatArray( GLBufferObjectTarget target, GLBufferObjectAccess access )
{
	return reinterpret_cast< float* >( map( target, access ) );
}

void GLBufferObject::unmap( GLBufferObject::GLBufferObjectTarget target )
{
	assert( target != TARGET_NO_TARGET );
	if( isBoundToTarget( target ) )
	{
		glUnmapBuffer( target );
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer Object not bound to target\n" );
		assert( false );
	}
#endif
}

void* GLBufferObject::getData( GLBufferObject::GLBufferObjectTarget target )
{
	assert( target != TARGET_NO_TARGET );

	ubyte* dataOut = NULL;

	if( isBoundToTarget( target ) )
	{
		dataOut = new ubyte[ m_nBytes ];
		glGetBufferSubData( target, 0, m_nBytes, dataOut );
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer Object not bound to target\n" );
		assert( false );
	}
#endif

	return dataOut;
}

void GLBufferObject::setFloatSubData( GLBufferObject::GLBufferObjectTarget target,
									 const float* afData, int nElements,
									 int nFloatsOffset )
{
	assert( target != TARGET_NO_TARGET );

	if( ( nFloatsOffset + nElements ) <= m_nElements )
	{
		if( isBoundToTarget( target ) )
		{
			GLintptr offset = nFloatsOffset * sizeof( float );
			GLsizeiptr size = nElements * m_nBytesPerElement;

			glBufferSubData( target, offset, size, afData );
		}
#if _DEBUG
		else
		{
			fprintf( stderr, "Buffer Object not bound to target\n" );
			assert( false );
		}
#endif
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer overflow.  nElements + nFloatsOffset = %d, m_nElements = %d\n",
			nElements + nFloatsOffset, m_nElements );
		assert( false );
	}
#endif
}

void GLBufferObject::setIntSubData( GLBufferObject::GLBufferObjectTarget target,
								   const int* aiData, int nElements,
								   int nIntsOffset )
{
	assert( target != TARGET_NO_TARGET );

	if( ( nIntsOffset + nElements ) <= m_nElements )
	{
		if( isBoundToTarget( target ) )
		{
			GLintptr offset = nIntsOffset * sizeof( int );
			GLsizeiptr size = nElements * m_nBytesPerElement;

			glBufferSubData( target, offset, size, aiData );
		}
#if _DEBUG
		else
		{
			fprintf( stderr, "Buffer Object not bound to target\n" );
			assert( false );
		}
#endif
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer overflow.  nElements + nIntsOffset = %d, m_nElements = %d\n",
			nElements + nIntsOffset, m_nElements );
		assert( false );
	}
#endif
}

// sets a sub-array of this buffer object from data in aubData
void GLBufferObject::setUnsignedByteSubData( GLBufferObject::GLBufferObjectTarget target,
											const ubyte* aubData, int nElements,
											int nUnsignedBytesOffset )
{
	assert( target != TARGET_NO_TARGET );

	if( ( nUnsignedBytesOffset + nElements ) <= m_nElements )
	{
		if( isBoundToTarget( target ) )
		{
			GLintptr offset = nUnsignedBytesOffset * sizeof( ubyte );
			GLsizeiptr size = nElements * m_nBytesPerElement;

			glBufferSubData( target, offset, size, aubData );
		}
#if _DEBUG
		else
		{
			fprintf( stderr, "Buffer Object not bound to target\n" );
			assert( false );
		}
#endif
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer overflow.  nElements + nUnsignedBytesOffset = %d, m_nElements = %d\n",
			nElements + nUnsignedBytesOffset, m_nElements );
		assert( false );
	}
#endif
}

void GLBufferObject::setUnsignedIntSubData( GLBufferObject::GLBufferObjectTarget target,
										   const uint* auiData, int nElements,
										   int nUnsignedIntsOffset )
{
	assert( target != TARGET_NO_TARGET );

	if( ( nUnsignedIntsOffset + nElements ) <= m_nElements )
	{
		if( isBoundToTarget( target ) )
		{
			GLintptr offset = nUnsignedIntsOffset * sizeof( uint );
			GLsizeiptr size = nElements * m_nBytesPerElement;

			glBufferSubData( target, offset, size, auiData );
		}
#if _DEBUG
		else
		{
			fprintf( stderr, "Buffer Object not bound to target\n" );
			assert( false );
		}
#endif
	}
#if _DEBUG
	else
	{
		fprintf( stderr, "Buffer overflow.  nElements + nUnsignedIntsOffset = %d, m_nElements = %d\n",
			nElements + nUnsignedIntsOffset, m_nElements );
		assert( false );
	}
#endif
}

void GLBufferObject::dumpToTXTFloat( const char* filename )
{
	// HACK: put in type
	bind( GLBufferObject::TARGET_ARRAY_BUFFER );
	float* data = reinterpret_cast< float* >( getData( GLBufferObject::TARGET_ARRAY_BUFFER ) );
	ArrayWithLength< float > arr( data, getNumBytes() / sizeof( float ) );
	ArrayUtils::dumpFloatArrayToFileText( arr, filename );
	arr.destroy();
	unbind( GLBufferObject::TARGET_ARRAY_BUFFER );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
GLBufferObject* GLBufferObject::s_apBindingTable[4] =
{
	NULL,
	NULL,
	NULL,
	NULL
};

// static
int GLBufferObject::getBindingTableIndex( GLBufferObject::GLBufferObjectTarget target )
{	
	switch( target )
	{
	case GLBufferObject::TARGET_ARRAY_BUFFER:
		return 0;
	case GLBufferObject::TARGET_ELEMENT_ARRAY_BUFFER:
		return 1;
	case GLBufferObject::TARGET_PIXEL_PACK_BUFFER:
		return 2;
	case GLBufferObject::TARGET_PIXEL_UNPACK_BUFFER:
		return 3;
	default:
		fprintf( stderr, "Can't get here!\n" );
		assert( false );
		return -1;
	}
}

// static
void GLBufferObject::setBoundBufferObject( GLBufferObject::GLBufferObjectTarget target,
										  GLBufferObject* pBufferObject )
{
	s_apBindingTable[ getBindingTableIndex( target ) ] = pBufferObject;
}

bool GLBufferObject::isBoundToTarget( GLBufferObject::GLBufferObjectTarget target )
{
	return( getBoundBufferObject( target ) == this );
}
