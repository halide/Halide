#include "DynamicVertexBuffer.h"

DynamicVertexBuffer::DynamicVertexBuffer( ID3D11Device* pDevice, int capacity, int vertexSizeBytes ) :
	m_capacity( capacity ),
	m_vertexSizeBytes( vertexSizeBytes )
{
	int bufferSize = capacity * vertexSizeBytes;

	D3D11_BUFFER_DESC bd;
	bd.ByteWidth = bufferSize;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;

	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &m_pBuffer );

	// TODO: make this a factory function and return NULL on FAILED( hr )
	// TODO: want something other than immediate context?
	pDevice->GetImmediateContext( &m_pContext );
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

ID3D11Buffer* DynamicVertexBuffer::buffer()
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

D3D11_MAPPED_SUBRESOURCE DynamicVertexBuffer::mapForWriteDiscard()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_pContext->Map( m_pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );	
	return mappedResource;
}

void DynamicVertexBuffer::unmap()
{
	m_pContext->Unmap( m_pBuffer, 0 );
}
