#include "DynamicVertexBuffer.h"

DynamicVertexBuffer::DynamicVertexBuffer( ID3D10Device* pDevice, int capacity, int vertexSizeBytes ) :
	m_capacity( capacity ),
	m_vertexSizeBytes( vertexSizeBytes )
{
	int bufferSize = capacity * vertexSizeBytes;

	D3D10_BUFFER_DESC bd;
	bd.ByteWidth = bufferSize;
	bd.Usage = D3D10_USAGE_DYNAMIC;
	bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;

	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &m_pBuffer );

}

// virtual
DynamicVertexBuffer::~DynamicVertexBuffer()
{
	m_pBuffer->Release();
}

int DynamicVertexBuffer::capacity()
{
	return m_capacity;
}

ID3D10Buffer* DynamicVertexBuffer::buffer()
{
	return m_pBuffer;
}

UINT DynamicVertexBuffer::defaultStride()
{
	return m_vertexSizeBytes;
}

UINT DynamicVertexBuffer::defaultOffset()
{
	return 0;
}

void* DynamicVertexBuffer::mapForWriteDiscard()
{
	void* ptr;
	m_pBuffer->Map( D3D10_MAP_WRITE_DISCARD, 0, &ptr );
	return ptr;
}

void DynamicVertexBuffer::unmap()
{
	m_pBuffer->Unmap();
}
