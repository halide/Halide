#include "StaticDataBuffer.h"

#include <common/BasicTypes.h>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
StaticDataBuffer* StaticDataBuffer::createFloat( ID3D11Device* pDevice,
												 int nElements )
{
	StaticDataBuffer* output = NULL;

	int elementSizeBytes = sizeof( float );
	D3D11_BUFFER_DESC bd = createStaticBufferDescription( nElements, elementSizeBytes );
	
	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new StaticDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
StaticDataBuffer* StaticDataBuffer::createFloat2( ID3D11Device* pDevice,
												 int nElements )
{
	StaticDataBuffer* output = NULL;

	int elementSizeBytes = 2 * sizeof( float );
	D3D11_BUFFER_DESC bd = createStaticBufferDescription( nElements, elementSizeBytes );
	
	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new StaticDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
StaticDataBuffer* StaticDataBuffer::createFloat4( ID3D11Device* pDevice,
												 int nElements )
{
	StaticDataBuffer* output = NULL;

	int elementSizeBytes = 4 * sizeof( float );
	D3D11_BUFFER_DESC bd = createStaticBufferDescription( nElements, elementSizeBytes );
	
	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new StaticDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			pBuffer );
	}

	return output;
}

// static
StaticDataBuffer* StaticDataBuffer::createUInt2( ID3D11Device* pDevice,
												 int nElements )
{
	StaticDataBuffer* output = NULL;

	int elementSizeBytes = 2 * sizeof( uint );
	D3D11_BUFFER_DESC bd = createStaticBufferDescription( nElements, elementSizeBytes );
	
	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new StaticDataBuffer( pDevice,
			nElements, elementSizeBytes,
			DXGI_FORMAT_R32G32_UINT,
			pBuffer );
	}

	return output;
}

// virtual
StaticDataBuffer::~StaticDataBuffer()
{
	m_pBuffer->Release();
}

int StaticDataBuffer::numElements() const
{
	return m_nElements;
}

int StaticDataBuffer::elementSizeBytes() const
{
	return m_elementSizeBytes;
}

int StaticDataBuffer::sizeInBytes() const
{
	return numElements() * elementSizeBytes();
}

DXGI_FORMAT StaticDataBuffer::format() const
{
	return m_format;
}

void StaticDataBuffer::update( ID3D11DeviceContext* pContext, const void* srcData )
{
	pContext->UpdateSubresource( m_pBuffer, 0,
		NULL, srcData, sizeInBytes(), sizeInBytes() );
}

ID3D11Buffer* StaticDataBuffer::buffer() const
{
	return m_pBuffer;
}

ID3D11ShaderResourceView* StaticDataBuffer::shaderResourceView() const
{
	return m_pSRV;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

StaticDataBuffer::StaticDataBuffer( ID3D11Device* pDevice,
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

	HRESULT hr = pDevice->CreateShaderResourceView( m_pBuffer, &desc, &m_pSRV );
}

// static
D3D11_BUFFER_DESC StaticDataBuffer::createStaticBufferDescription( int nElements, int elementSizeBytes )
{
	int bufferSize = nElements * elementSizeBytes;

	D3D11_BUFFER_DESC bd;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.ByteWidth = bufferSize;
	bd.CPUAccessFlags = 0;
	bd.MiscFlags = 0;
	bd.StructureByteStride = elementSizeBytes;
	bd.Usage = D3D11_USAGE_DEFAULT;

	return bd;
}