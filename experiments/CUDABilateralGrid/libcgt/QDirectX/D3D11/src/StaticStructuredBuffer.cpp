#include "StaticStructuredBuffer.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
StaticStructuredBuffer* StaticStructuredBuffer::create( ID3D11Device* pDevice,
													   int nElements, int elementSizeBytes )
{
	StaticStructuredBuffer* output = NULL;

	int bufferSize = nElements * elementSizeBytes;

	D3D11_BUFFER_DESC bd;
	bd.ByteWidth = bufferSize;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;;
	bd.CPUAccessFlags = 0;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = elementSizeBytes;

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new StaticStructuredBuffer( pDevice,
			nElements, elementSizeBytes,
			pBuffer );
	}

	return output;
}

// virtual
StaticStructuredBuffer::~StaticStructuredBuffer()
{
	m_pBuffer->Release();
}

int StaticStructuredBuffer::numElements() const
{
	return m_nElements;
}

int StaticStructuredBuffer::elementSizeBytes() const
{
	return m_elementSizeBytes;
}

ID3D11Buffer* StaticStructuredBuffer::buffer() const
{
	return m_pBuffer;
}

ID3D11ShaderResourceView* StaticStructuredBuffer::shaderResourceView() const
{
	return m_pSRV;
}

ID3D11UnorderedAccessView* StaticStructuredBuffer::unorderedAccessView() const
{
	return m_pUAV;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

StaticStructuredBuffer::StaticStructuredBuffer( ID3D11Device* pDevice,
											   int nElements, int elementSizeBytes,
											   ID3D11Buffer* pBuffer ) :

	m_nElements( nElements ),
	m_elementSizeBytes( elementSizeBytes ),
	m_pBuffer( pBuffer )

{
	/*
	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Format = DXGI_FORMAT_R32G32_FLOAT;
	desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	D3D11_BUFFER_SRV bsrv;
	bsrv.ElementOffset = 0;
	bsrv.ElementWidth = numElements();
	desc.Buffer = bsrv;
	
	HRESULT hr = pDevice->CreateShaderResourceView( m_pBuffer, &desc, &m_pSRV );
	*/

	pDevice->CreateShaderResourceView( pBuffer, NULL, &m_pSRV );
	pDevice->CreateUnorderedAccessView( pBuffer, NULL, &m_pUAV );
	
	/*
	D3D11_SHADER_RESOURCE_VIEW_DESC desc2;
	m_pSRV->GetDesc( &desc2 );

	D3D11_UNORDERED_ACCESS_VIEW_DESC desc3;
	m_pUAV->GetDesc( &desc3 );
	*/
}