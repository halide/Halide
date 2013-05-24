#include "RenderTarget.h"

// static
RenderTarget* RenderTarget::createFloat1( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new RenderTarget( pDevice, width, height, pTexture );
}

// static
RenderTarget* RenderTarget::createFloat2( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new RenderTarget( pDevice, width, height, pTexture );
}

// static
RenderTarget* RenderTarget::createFloat4( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R32G32B32A32_FLOAT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new RenderTarget( pDevice, width, height, pTexture );
}

// static
RenderTarget* RenderTarget::createUnsignedShort1( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UINT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new RenderTarget( pDevice, width, height, pTexture );
}

// static
RenderTarget* RenderTarget::createUnsignedShort1UNorm( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R16_UINT );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new RenderTarget( pDevice, width, height, pTexture );
}

// static
RenderTarget* RenderTarget::createUnsignedByte4( ID3D11Device* pDevice, int width, int height )
{
	ID3D11Texture2D* pTexture;
	D3D11_TEXTURE2D_DESC td = makeTextureDescription( width, height, DXGI_FORMAT_R8G8B8A8_UNORM );
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new RenderTarget( pDevice, width, height, pTexture );
}

// virtual
RenderTarget::~RenderTarget()
{
	m_pTexture->Release();
	m_pRenderTargetView->Release();
	m_pShaderResourceView->Release();
}

int RenderTarget::width()
{
	return m_width;
}

int RenderTarget::height()
{
	return m_height;
}

Vector2i RenderTarget::size()
{
	return Vector2i( m_width, m_height );
}

void RenderTarget::update( ID3D11DeviceContext* pContext, Reference< Image4f > im )
{
	uint rowPitch = im->width() * 4 * sizeof( float );
	uint depthPitch = im->width() * im->height() * sizeof( float );

	pContext->UpdateSubresource( m_pTexture, 0,
		NULL, im->pixels(), rowPitch, depthPitch );
}

ID3D11Texture2D* RenderTarget::texture()
{
	return m_pTexture;
}

ID3D11RenderTargetView* RenderTarget::renderTargetView()
{
	return m_pRenderTargetView;
}

ID3D11ShaderResourceView* RenderTarget::shaderResourceView()
{
	return m_pShaderResourceView;
}

ID3D11UnorderedAccessView* RenderTarget::unorderedAccessView()
{
	return m_pUnorderedAccessView;
}

// static
D3D11_TEXTURE2D_DESC RenderTarget::makeTextureDescription( int width, int height, DXGI_FORMAT format )
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
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	td.CPUAccessFlags = 0;
	td.MiscFlags = 0;

	return td;
}

RenderTarget::RenderTarget( ID3D11Device* pDevice, int width, int height, ID3D11Texture2D* pTexture ) :
	m_width( width ),
	m_height( height ),
	m_pTexture( pTexture )
{
	pDevice->CreateRenderTargetView( pTexture, NULL, &m_pRenderTargetView );
	pDevice->CreateShaderResourceView( pTexture, NULL, &m_pShaderResourceView );
	pDevice->CreateUnorderedAccessView( pTexture, NULL, &m_pUnorderedAccessView );
}
