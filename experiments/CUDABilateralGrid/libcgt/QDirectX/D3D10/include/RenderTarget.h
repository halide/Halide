#ifndef RENDER_TARGET_H
#define RENDER_TARGET_H

#include <d3d10_1.h>
#include <d3d10.h>

#include <vecmath/Vector2i.h>

class RenderTarget
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static RenderTarget* createFloat1( ID3D10Device* pDevice, int width, int height );
	static RenderTarget* createFloat2( ID3D10Device* pDevice, int width, int height );
	static RenderTarget* createFloat4( ID3D10Device* pDevice, int width, int height );
	static RenderTarget* createUnsignedShort1( ID3D10Device* pDevice, int width, int height );
	static RenderTarget* createUnsignedShort1UNorm( ID3D10Device* pDevice, int width, int height );
	static RenderTarget* createUnsignedByte4( ID3D10Device* pDevice, int width, int height );

	virtual ~RenderTarget();
	
	int width();
	int height();
	Vector2i size();

	ID3D10Texture2D* texture();
	ID3D10RenderTargetView* renderTargetView();
	ID3D10ShaderResourceView* shaderResourceView();

private:

	static D3D10_TEXTURE2D_DESC makeTextureDescription( int width, int height, DXGI_FORMAT format );

	RenderTarget( ID3D10Device* pDevice, int width, int height, ID3D10Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D10Texture2D* m_pTexture;
	ID3D10RenderTargetView* m_pRenderTargetView;
	ID3D10ShaderResourceView* m_pShaderResourceView;	

};

#endif // RENDER_TARGET_H
