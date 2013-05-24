#ifndef GL_BUFFER_OBJECT
#define GL_BUFFER_OBJECT

#include <GL/glew.h>
#include <vector>

#include <common/BasicTypes.h>

class TriangleMesh3fVertex;
class Vector2f;
class Vector3f;

class GLBufferObject
{
public:

	enum GLBufferObjectTarget
	{
		TARGET_NO_TARGET = 0,

		// Vertex Buffer Objects
		TARGET_ARRAY_BUFFER = GL_ARRAY_BUFFER,
		TARGET_ELEMENT_ARRAY_BUFFER = GL_ELEMENT_ARRAY_BUFFER,

		// Pixel Buffer Objects
		TARGET_PIXEL_PACK_BUFFER = GL_PIXEL_PACK_BUFFER_ARB,
		TARGET_PIXEL_UNPACK_BUFFER = GL_PIXEL_UNPACK_BUFFER_ARB
	};

	enum GLBufferObjectUsage
	{
		USAGE_STREAM_DRAW = GL_STREAM_DRAW,
		USAGE_STREAM_READ = GL_STREAM_READ,
		USAGE_STREAM_COPY = GL_STREAM_COPY,

		USAGE_STATIC_DRAW = GL_STATIC_DRAW,
		USAGE_STATIC_READ = GL_STATIC_READ,
		USAGE_STATIC_COPY = GL_STATIC_COPY,

		USAGE_DYNAMIC_DRAW = GL_DYNAMIC_DRAW,
		USAGE_DYNAMIC_READ = GL_DYNAMIC_READ,
		USAGE_DYNAMIC_COPY = GL_DYNAMIC_COPY,
	};

	enum GLBufferObjectAccess
	{
		ACCESS_READ_ONLY = GL_READ_ONLY,
		ACCESS_WRITE_ONLY = GL_WRITE_ONLY,
		ACCESS_READ_WRITE = GL_READ_WRITE
	};

	// get the GLBufferObject currently bound to target
	static GLBufferObject* getBoundBufferObject( GLBufferObjectTarget target );

	// unbind target
	static void unbind( GLBufferObjectTarget target );	

	// used to get the "offset-as-a-pointer" of this buffer
	// used for things like glDrawElements and glTexImage2D
	// that require pointers instead of offsets
	// 
	// if i = 3 and elementSize is 2 * sizeof( float ),
	// i.e. each element is a float2
	// then it will return the offset into the buffer of the 6th float
	static char* convertOffsetToPointer( int i, int elementSize );
	static int convertPointerToOffset( char* pPtr, int elementSize );

	// TODO: getUsage()?
	// TODO: how do you change the usage?

	// Construct a GLBufferObject with data of size nBytes, bound to target with usage
	// if data is NULL (default), then it creates an empty GLBufferObject
	// nElements = number of "elements"
	// for a Vertex Buffer Object, it would be the number of vertices
	//     i.e. the number of times glVertex would be called,
	//     and also the parameter passed to glDrawArrays and glDrawElements
	// for a Pixel Buffer Object, it would be the number of pixels
	// nBytes should be equal to nElements * bytesPerElement
	GLBufferObject( GLBufferObjectTarget target,
		GLBufferObjectUsage usage,
		int nElements, int bytesPerElement,
		const void* data = NULL );

	// destroy a GLBufferObject
	virtual ~GLBufferObject();

	// gets the number of elements
	int getNumElements();

	// get the number of bytes per element
	int getNumBytesPerElement();

	// gets the total number of bytes
	int getNumBytes();

	// bind this GLBufferObject to target
	void bind( GLBufferObjectTarget target );	

	// unbinds this buffer object from ALL targets
	void unbindAll();

	// map this buffer into local memory
	void* map( GLBufferObjectTarget target, GLBufferObjectAccess access );

	ubyte* mapToUnsignedByteArray( GLBufferObjectTarget target, GLBufferObjectAccess access );

	float* mapToFloatArray( GLBufferObjectTarget target, GLBufferObjectAccess access );

	// unmap this buffer from local memory
	void unmap( GLBufferObjectTarget target );

	// gets the data stored in this GLBufferObject
	void* getData( GLBufferObjectTarget target );

	// sets a sub-array of this buffer object from data in afData
	void setFloatSubData( GLBufferObjectTarget target,
		const float* afData, int nElements,
		int nFloatsOffset = 0 );

	// sets a sub-array of this buffer object from data in aiData
	void setIntSubData( GLBufferObjectTarget target,
		const int* aiData, int nElements,
		int nIntsOffset = 0 );

	// sets a sub-array of this buffer object from data in aubData
	void setUnsignedByteSubData( GLBufferObjectTarget target,
		const ubyte* aubData, int nElements,
		int nUnsignedBytesOffset = 0 );

	// sets a sub-array of this buffer object from data in auiData
	void setUnsignedIntSubData( GLBufferObjectTarget target,
		const uint* auiData, int nElements,
		int nUnsignedIntsOffset = 0 );	

	// ==== Debugging ====
	void dumpToTXTFloat( const char* filename );

protected:

	// ---- Static ----

	static GLBufferObject* s_apBindingTable[4];

	// returns the index of the binding table entry corresponding to target
	// ARRAY_BUFFER --> 0
	// ELEMENT_ARRAY_BUFFER --> 1
	// PIXEL_PACK_BUFFER --> 2
	// PIXEL_UNPACK_BUFFER --> 2
	static int getBindingTableIndex( GLBufferObjectTarget target );

	static void setBoundBufferObject( GLBufferObjectTarget target,
		GLBufferObject* pBufferObject );
	
	// ---- Non-static ----
	
	bool isBoundToTarget( GLBufferObjectTarget target );

	int m_nElements;
	int m_nBytesPerElement;
	int m_nBytes;
	GLuint m_iBufferId;
};

#endif // GL_BUFFER_OBJECT
