#include "StagingStructuredBuffer.h"

#include "D3D11Utils_Box.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
StagingStructuredBuffer* StagingStructuredBuffer::create( ID3D11Device* pDevice,
														 int nElements, int elementSizeBytes )
{
	StagingStructuredBuffer* output = NULL;

	int bufferSize = nElements * elementSizeBytes;

	D3D11_BUFFER_DESC bd;
	bd.ByteWidth = bufferSize;
	bd.Usage = D3D11_USAGE_STAGING;
	bd.BindFlags = 0;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = elementSizeBytes;

	ID3D11Buffer* pBuffer;
	HRESULT hr = pDevice->CreateBuffer( &bd, NULL, &pBuffer );
	if( SUCCEEDED(  hr ) )
	{
		output = new StagingStructuredBuffer( pDevice,
			nElements, elementSizeBytes,
			pBuffer );
	}

	return output;
}

// virtual
StagingStructuredBuffer::~StagingStructuredBuffer()
{
	m_pBuffer->Release();
	m_pContext->Release();
	m_pDevice->Release();
}

int StagingStructuredBuffer::numElements() const
{
	return m_nElements;
}

int StagingStructuredBuffer::elementSizeBytes() const
{
	return m_elementSizeBytes;
}

ID3D11Buffer* StagingStructuredBuffer::buffer() const
{
	return m_pBuffer;
}

D3D11_MAPPED_SUBRESOURCE StagingStructuredBuffer::mapForReadWrite()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_pContext->Map( m_pBuffer, 0, D3D11_MAP_READ_WRITE, 0, &mappedResource );
	return mappedResource;
}

void StagingStructuredBuffer::unmap()
{
	m_pContext->Unmap( m_pBuffer, 0 );
}

void StagingStructuredBuffer::copyFrom( ID3D11Buffer* pSource )
{
	m_pContext->CopyResource( m_pBuffer, pSource );
}

void StagingStructuredBuffer::copyTo( ID3D11Buffer* pTarget )
{
	m_pContext->CopyResource( pTarget, m_pBuffer );
}

void StagingStructuredBuffer::copyRangeFrom( ID3D11Buffer* pSource, int srcIndex, int count,
	int dstIndex )
{
	int esb = elementSizeBytes();
	D3D11_BOX srcBox = D3D11Utils_Box::createRange( srcIndex * esb, count * esb );

	m_pContext->CopySubresourceRegion
	(
		m_pBuffer, 0,
		dstIndex * esb, 0, 0,
		pSource, 0,
		&srcBox
	);
}

void StagingStructuredBuffer::copyRangeTo( int srcIndex, int count,
	ID3D11Buffer* pTarget, int dstIndex )
{
	int esb = elementSizeBytes();
	D3D11_BOX srcBox = D3D11Utils_Box::createRange( srcIndex * esb, count * esb );

	m_pContext->CopySubresourceRegion
	(
		pTarget, 0,
		dstIndex * esb, 0, 0,
		m_pBuffer, 0,
		&srcBox
	);
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

StagingStructuredBuffer::StagingStructuredBuffer( ID3D11Device* pDevice,
												 int nElements, int elementSizeBytes,
												 ID3D11Buffer* pBuffer ) :

	m_pDevice( pDevice ),
	m_nElements( nElements ),
	m_elementSizeBytes( elementSizeBytes ),
	m_pBuffer( pBuffer )

{
	m_pDevice->AddRef();
	m_pDevice->GetImmediateContext( &m_pContext );
}
