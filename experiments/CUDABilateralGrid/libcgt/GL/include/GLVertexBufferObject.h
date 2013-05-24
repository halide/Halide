#ifndef GL_VERTEX_BUFFER_OBJECT
#define GL_VERTEX_BUFFER_OBJECT

#include <common/BasicTypes.h>
#include "GLBufferObject.h"

class Vector2f;
class Vector3f;

class GLVertexBufferObject
{
public:

	// vertices, normals, colors, etc
	// the returned buffer object is UNBOUND
	static GLBufferObject* fromVector3fVector( std::vector< Vector3f >* pvData,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// texture coordinates
	// the returned buffer object is UNBOUND
	static GLBufferObject* fromVector2fVector( std::vector< Vector2f >* pvData,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// TODO: definitely do better...
	// for indexed arrays
	// the returned buffer object is UNBOUND
	static GLBufferObject* fromIntVector( std::vector< int >* pvData,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// for indexed arrays
	// the returned buffer object is UNBOUND
	static GLBufferObject* fromUnsignedIntVector( std::vector< uint >* pvData,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// the returned buffer object is UNBOUND
	static GLBufferObject* fromFloatArray( float* afData, int nFloats, int nElements,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// the returned buffer object is UNBOUND
	static GLBufferObject* fromShortArray( GLshort* asData, int nShorts, int nElements,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// the returned buffer object is UNBOUND
	static GLBufferObject* fromIntArray( int* aiData, int nInts, int nElements,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// the returned buffer object is UNBOUND
	static GLBufferObject* fromUnsignedIntArray( uint* auiData, int nUnsignedInts, int nElements,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

	// the returned buffer object is UNBOUND
	static GLBufferObject* fromUnsignedByteArray( const ubyte* aubData, int nUnsignedBytes, int nElements,
		GLBufferObject::GLBufferObjectUsage usage = GLBufferObject::USAGE_STATIC_DRAW );

};

#endif // GL_VERTEX_BUFFER_OBJECT
