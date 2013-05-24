#include "DepthStencilTarget.h"

// static
DepthStencilTarget* DepthStencilTarget::createDepthFloat24StencilUnsignedByte8( ID3D11Device* pDevice, int width, int height )
{
	DXGI_SAMPLE_DESC sd;
	sd.Count = 1;
	sd.Quality = 0;

	D3D11_TEXTURE2D_DESC td;
	td.Width = width;
	td.Height = height;
	td.ArraySize = 1;
	td.MipLevels = 1;
	td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	td.SampleDesc = sd;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	td.CPUAccessFlags = 0;
	td.MiscFlags = 0;

	ID3D11Texture2D* pTexture;
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DepthStencilTarget( pDevice, width, height, pTexture );
}

// static
DepthStencilTarget* DepthStencilTarget::createDepthFloat32( ID3D11Device* pDevice, int width, int height )
{
	DXGI_SAMPLE_DESC sd;
	sd.Count = 1;
	sd.Quality = 0;

	D3D11_TEXTURE2D_DESC td;
	td.Width = width;
	td.Height = height;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_D32_FLOAT;
	td.SampleDesc = sd;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	td.CPUAccessFlags = 0;
	td.MiscFlags = 0;

	ID3D11Texture2D* pTexture;
	pDevice->CreateTexture2D( &td, NULL, &pTexture );

	return new DepthStencilTarget( pDevice, width, height, pTexture );
}

// virtual
DepthStencilTarget::~DepthStencilTarget()
{
	m_pTexture->Release();
	m_pDepthStencilView->Release();	
}

int DepthStencilTarget::width()
{
	return m_width;
}

int DepthStencilTarget::height()
{
	return m_height;
}

ID3D11Texture2D* DepthStencilTarget::texture()
{
	return m_pTexture;
}

ID3D11DepthStencilView* DepthStencilTarget::depthStencilView()
{
	return m_pDepthStencilView;
}

DepthStencilTarget::DepthStencilTarget( ID3D11Device* pDevice, int width, int height, ID3D11Texture2D* pTexture ) :
	m_width( width ),
	m_height( height ),
	m_pTexture( pTexture )
{
	pDevice->CreateDepthStencilView( pTexture, NULL, &m_pDepthStencilView );	
}
