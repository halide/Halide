#include "DynamicIndexBuffer.h"

// static
DXGI_FORMAT DynamicIndexBuffer::s_format = DXGI_FORMAT_R32_UINT;

DynamicIndexBuffer::DynamicIndexBuffer( ID3D11Device* pDevice, int capacity ) :

	m_capacity( capacity )

{
	int bufferSize = capacity * sizeof( uint );

	D3D11_BUFFER_DESC bd;
	bd.ByteWidth = bufferSize;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;

	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &m_pBuffer );

	// TODO: make this a factory function and return NULL on FAILED( hr )

	pDevice->GetImmediateContext( &m_pContext );
}

// virtual
DynamicIndexBuffer::~DynamicIndexBuffer()
{
	m_pBuffer->Release();
}

int DynamicIndexBuffer::capacity() const
{
	return m_capacity;
}

ID3D11Buffer* DynamicIndexBuffer::buffer()
{
	return m_pBuffer;
}

uint* DynamicIndexBuffer::mapForWriteDiscard()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_pContext->Map( m_pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );	
	return reinterpret_cast< uint* >( mappedResource.pData );
}

void DynamicIndexBuffer::unmap()
{
	m_pContext->Unmap( m_pBuffer, 0 );
}
