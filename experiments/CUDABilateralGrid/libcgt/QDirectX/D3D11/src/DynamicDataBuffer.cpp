#include "DynamicDataBuffer.h"

#include <common/BasicTypes.h>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
DynamicDataBuffer* DynamicDataBuffer::createFloat( ID3D11Device* pDevice,
	int nElements )
{
	DynamicDataBuffer* output = NULL;

	int elementSizeBytes = sizeof( float );
	D3D11_BUFFER_DESC bd = createDynamicBufferDescription( nElements, elementSizeBytes );

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new DynamicDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
DynamicDataBuffer* DynamicDataBuffer::createFloat2( ID3D11Device* pDevice,
	int nElements )
{
	DynamicDataBuffer* output = NULL;

	int elementSizeBytes = 2 * sizeof( float );
	D3D11_BUFFER_DESC bd = createDynamicBufferDescription( nElements, elementSizeBytes );

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new DynamicDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
DynamicDataBuffer* DynamicDataBuffer::createFloat3( ID3D11Device* pDevice,
	int nElements )
{
	DynamicDataBuffer* output = NULL;

	int elementSizeBytes = 3 * sizeof( float );
	D3D11_BUFFER_DESC bd = createDynamicBufferDescription( nElements, elementSizeBytes );

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new DynamicDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32B32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
DynamicDataBuffer* DynamicDataBuffer::createFloat4( ID3D11Device* pDevice,
	int nElements )
{
	DynamicDataBuffer* output = NULL;

	int elementSizeBytes = 4 * sizeof( float );
	D3D11_BUFFER_DESC bd = createDynamicBufferDescription( nElements, elementSizeBytes );

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new DynamicDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
DynamicDataBuffer* DynamicDataBuffer::createUInt2( ID3D11Device* pDevice,
	int nElements )
{
	DynamicDataBuffer* output = NULL;

	int elementSizeBytes = 2 * sizeof( uint );
	D3D11_BUFFER_DESC bd = createDynamicBufferDescription( nElements, elementSizeBytes );

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new DynamicDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32_UINT,
			pBuffer );
	}

	return output;
}

// static
DynamicDataBuffer* DynamicDataBuffer::createUInt4( ID3D11Device* pDevice,
	int nElements )
{
	DynamicDataBuffer* output = NULL;

	int elementSizeBytes = 4 * sizeof( uint );
	D3D11_BUFFER_DESC bd = createDynamicBufferDescription( nElements, elementSizeBytes );

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new DynamicDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32B32A32_UINT,
			pBuffer );
	}

	return output;
}

// virtual
DynamicDataBuffer::~DynamicDataBuffer()
{
	m_pContext->Release();
	m_pBuffer->Release();
}

int DynamicDataBuffer::numElements() const
{
	return m_nElements;
}

int DynamicDataBuffer::elementSizeBytes() const
{
	return m_elementSizeBytes;
}

int DynamicDataBuffer::sizeInBytes() const
{
	return numElements() * elementSizeBytes();
}

DXGI_FORMAT DynamicDataBuffer::format() const
{
	return m_format;
}

void DynamicDataBuffer::update( ID3D11DeviceContext* pContext, const void* srcData )
{
	pContext->UpdateSubresource( m_pBuffer, 0,
		NULL, srcData, sizeInBytes(), sizeInBytes() );
}

D3D11_MAPPED_SUBRESOURCE DynamicDataBuffer::mapForWriteDiscard()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_pContext->Map( m_pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );	
	return mappedResource;
}

void DynamicDataBuffer::unmap()
{
	m_pContext->Unmap( m_pBuffer, 0 );
}

ID3D11Buffer* DynamicDataBuffer::buffer() const
{
	return m_pBuffer;
}

ID3D11ShaderResourceView* DynamicDataBuffer::shaderResourceView() const
{
	return m_pSRV;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

DynamicDataBuffer::DynamicDataBuffer( ID3D11Device* pDevice,
	int nElements, int elementSizeBytes,
	DXGI_FORMAT format,
	ID3D11Buffer* pBuffer ) :

	m_nElements( nElements ),
	m_elementSizeBytes( elementSizeBytes ),
	m_format( format ),
	m_pBuffer( pBuffer )

{
	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Format = format;
	desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	D3D11_BUFFER_SRV bsrv;
	bsrv.ElementOffset = 0;
	bsrv.ElementWidth = numElements();
	desc.Buffer = bsrv;

	// TODO: this can fail
	HRESULT hr = pDevice->CreateShaderResourceView( m_pBuffer, &desc, &m_pSRV );
	// TODO: want something other than immediate context?
	pDevice->GetImmediateContext( &m_pContext );
}

// static
D3D11_BUFFER_DESC DynamicDataBuffer::createDynamicBufferDescription( int nElements, int elementSizeBytes )
{
	int bufferSize = nElements * elementSizeBytes;

	D3D11_BUFFER_DESC bd;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.ByteWidth = bufferSize;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;
	bd.StructureByteStride = elementSizeBytes;
	bd.Usage = D3D11_USAGE_DYNAMIC;

	return bd;
}