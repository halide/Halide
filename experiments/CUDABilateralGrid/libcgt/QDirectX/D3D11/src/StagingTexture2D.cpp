#include "StagingTexture2D.h"

// static
StagingTexture2D* StagingTexture2D::createFloat1( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createFloat2( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createFloat4( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32B32A32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createUnsignedShort1( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UINT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createUnsignedShort1UNorm( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createUnsignedByte4( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R8G8B8A8_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// virtual
StagingTexture2D::~StagingTexture2D()
{
	m_pTexture->Release();
	m_pContext->Release();
	m_pDevice->Release();
}

int StagingTexture2D::width()
{
	return m_width;
}

int StagingTexture2D::height()
{
	return m_height;
}

ID3D11Texture2D* StagingTexture2D::texture()
{
	return m_pTexture;
}

D3D11_MAPPED_SUBRESOURCE StagingTexture2D::mapForReadWrite()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_pContext->Map( m_pTexture, 0, D3D11_MAP_READ_WRITE, 0, &mappedResource );
	return mappedResource;
}

void StagingTexture2D::unmap()
{
	m_pContext->Unmap( m_pTexture, 0 );
}

void StagingTexture2D::copyFrom( ID3D11Texture2D* pSource )
{
	m_pContext->CopyResource( m_pTexture, pSource );
}
	
void StagingTexture2D::copyTo( ID3D11Texture2D* pTarget )
{
	m_pContext->CopyResource( pTarget, m_pTexture );
}

// static
D3D11_TEXTURE2D_DESC StagingTexture2D::makeTextureDescription( int width, int height, DXGI_FORMAT format )
{
	DXGI_SAMPLE_DESC sd;
	sd.Count = 1;
	sd.Quality = 0;

	D3D11_TEXTURE2D_DESC td;
	td.Width = width;
	td.Height = height;
	td.ArraySize = 1;
	td.MipLevels = 1;
	td.Format = format;
	td.SampleDesc = sd;
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	td.MiscFlags = 0;

	return td;
}

StagingTexture2D::StagingTexture2D( ID3D11Device* pDevice,
								   int width, int height,
								   ID3D11Texture2D* pTexture ) :

	m_pDevice( pDevice ),
	m_width( width ),
	m_height( height ),
	m_pTexture( pTexture )

{
	m_pDevice->AddRef();
	pDevice->GetImmediateContext( &m_pContext );
}
