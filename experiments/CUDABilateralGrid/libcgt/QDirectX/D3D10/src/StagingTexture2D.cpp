#include "StagingTexture2D.h"

// static
StagingTexture2D* StagingTexture2D::createFloat1( ID3D10Device* pDevice, int width, int height )
{
	ID3D10Texture2D* pTexture;
	D3D10_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createFloat4( ID3D10Device* pDevice, int width, int height )
{
	ID3D10Texture2D* pTexture;
	D3D10_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32B32A32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createUnsignedShort1( ID3D10Device* pDevice, int width, int height )
{
	ID3D10Texture2D* pTexture;
	D3D10_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UINT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createUnsignedShort1UNorm( ID3D10Device* pDevice, int width, int height )
{
	ID3D10Texture2D* pTexture;
	D3D10_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// static
StagingTexture2D* StagingTexture2D::createUnsignedByte4( ID3D10Device* pDevice, int width, int height )
{
	ID3D10Texture2D* pTexture;
	D3D10_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R8G8B8A8_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new StagingTexture2D( pDevice, width, height, pTexture );
}

// virtual
StagingTexture2D::~StagingTexture2D()
{
	m_pTexture->Release();
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

ID3D10Texture2D* StagingTexture2D::texture()
{
	return m_pTexture;
}

D3D10_MAPPED_TEXTURE2D StagingTexture2D::map()
{
	D3D10_MAPPED_TEXTURE2D mappedTexture;
	m_pTexture->Map( 0, D3D10_MAP_READ_WRITE, 0, &mappedTexture );
	return mappedTexture;
}

void StagingTexture2D::unmap()
{
	m_pTexture->Unmap( 0 );
}

void StagingTexture2D::copyFrom( ID3D10Texture2D* pSource )
{
	m_pDevice->CopyResource( m_pTexture, pSource );
}
	
void StagingTexture2D::copyTo( ID3D10Texture2D* pTarget )
{
	m_pDevice->CopyResource( pTarget, m_pTexture );
}

// static
D3D10_TEXTURE2D_DESC StagingTexture2D::makeTextureDescription( int width, int height, DXGI_FORMAT format )
{
	DXGI_SAMPLE_DESC sd;
	sd.Count = 1;
	sd.Quality = 0;

	D3D10_TEXTURE2D_DESC td;
	td.Width = width;
	td.Height = height;
	td.ArraySize = 1;
	td.MipLevels = 1;
	td.Format = format;
	td.SampleDesc = sd;
	td.Usage = D3D10_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D10_CPU_ACCESS_READ | D3D10_CPU_ACCESS_WRITE;
	td.MiscFlags = 0;

	return td;
}

StagingTexture2D::StagingTexture2D( ID3D10Device* pDevice, int width, int height, ID3D10Texture2D* pTexture ) :
	m_pDevice( pDevice ),
	m_width( width ),
	m_height( height ),
	m_pTexture( pTexture )
{
	m_pDevice->AddRef();
}
