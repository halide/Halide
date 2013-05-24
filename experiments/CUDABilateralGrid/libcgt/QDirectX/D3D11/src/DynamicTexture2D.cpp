#include "DynamicTexture2D.h"

// static
DynamicTexture2D* DynamicTexture2D::createFloat1( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DynamicTexture2D( pDevice, width, height, pTexture );
}

// static
DynamicTexture2D* DynamicTexture2D::createFloat2( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DynamicTexture2D( pDevice, width, height, pTexture );
}

// static
DynamicTexture2D* DynamicTexture2D::createFloat4( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32B32A32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DynamicTexture2D( pDevice, width, height, pTexture );
}

// static
DynamicTexture2D* DynamicTexture2D::createUnsignedShort1( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UINT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DynamicTexture2D( pDevice, width, height, pTexture );
}

// static
DynamicTexture2D* DynamicTexture2D::createUnsignedShort1UNorm( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DynamicTexture2D( pDevice, width, height, pTexture );
}

// static
DynamicTexture2D* DynamicTexture2D::createUnsignedByte4( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R8G8B8A8_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DynamicTexture2D( pDevice, width, height, pTexture );
}

// virtual
DynamicTexture2D::~DynamicTexture2D()
{
	m_pTexture->Release();
	m_pShaderResourceView->Release();
}

int DynamicTexture2D::width()
{
	return m_width;
}

int DynamicTexture2D::height()
{
	return m_height;
}

Vector2i DynamicTexture2D::size()
{
	return Vector2i( m_width, m_height );
}

ID3D11Texture2D* DynamicTexture2D::texture()
{
	return m_pTexture;
}

ID3D11ShaderResourceView* DynamicTexture2D::shaderResourceView()
{
	return m_pShaderResourceView;
}

D3D11_MAPPED_SUBRESOURCE DynamicTexture2D::mapForWriteDiscard()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_pContext->Map( m_pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	return mappedResource;
}

void DynamicTexture2D::unmap()
{
	m_pContext->Unmap( m_pTexture, 0 );
}

// static
D3D11_TEXTURE2D_DESC DynamicTexture2D::makeTextureDescription( int width, int height, DXGI_FORMAT format )
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
	td.Usage = D3D11_USAGE_DYNAMIC;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	td.MiscFlags = 0;

	return td;
}

DynamicTexture2D::DynamicTexture2D( ID3D11Device* pDevice, int width, int height, ID3D11Texture2D* pTexture ) :
	m_width( width ),
	m_height( height ),
	m_pTexture( pTexture )
{
	pDevice->CreateShaderResourceView( pTexture, NULL, &m_pShaderResourceView );	
	pDevice->GetImmediateContext( &m_pContext );	
}
