#ifndef DYNAMIC_VERTEX_BUFFER_H
#define DYNAMIC_VERTEX_BUFFER_H

#include <d3d10_1.h>
#include <d3d10.h>

class DynamicVertexBuffer
{
public:

	DynamicVertexBuffer( ID3D10Device* pDevice, int capacity, int vertexSizeBytes );
	virtual ~DynamicVertexBuffer();

	int capacity();

	ID3D10Buffer* buffer();
	UINT defaultStride();
	UINT defaultOffset();

	void* mapForWriteDiscard();
	void unmap();	

private:

	int m_capacity;
	int m_vertexSizeBytes;
	ID3D10Buffer* m_pBuffer;

};

#endif // DYNAMIC_VERTEX_BUFFER_H
