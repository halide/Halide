#ifndef RENDER_TARGET_H
#define RENDER_TARGET_H

#include <D3D11.h>

#include <common/Reference.h>
#include <vecmath/Vector2i.h>
#include <imageproc/Image4f.h>

// A RenderTarget is a texture with:
// Usage = DFEAULT (GPU read/write, no CPU access)
// Binding = render target, shader resource, and unordered access
class RenderTarget
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static RenderTarget* createFloat1( ID3D11Device* pDevice, int width, int height );
	static RenderTarget* createFloat2( ID3D11Device* pDevice, int width, int height );
	static RenderTarget* createFloat4( ID3D11Device* pDevice, int width, int height );
	static RenderTarget* createUnsignedShort1( ID3D11Device* pDevice, int width, int height );
	static RenderTarget* createUnsignedShort1UNorm( ID3D11Device* pDevice, int width, int height );
	static RenderTarget* createUnsignedByte4( ID3D11Device* pDevice, int width, int height );

	virtual ~RenderTarget();
	
	int width();
	int height();
	Vector2i size();

	void update( ID3D11DeviceContext* pContext, Reference< Image4f > im );

	ID3D11Texture2D* texture();
	ID3D11RenderTargetView* renderTargetView();
	ID3D11ShaderResourceView* shaderResourceView();
	ID3D11UnorderedAccessView* unorderedAccessView();

private:

	static D3D11_TEXTURE2D_DESC makeTextureDescription( int width, int height, DXGI_FORMAT format );

	RenderTarget( ID3D11Device* pDevice, int width, int height, ID3D11Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D11Texture2D* m_pTexture;
	ID3D11RenderTargetView* m_pRenderTargetView;
	ID3D11ShaderResourceView* m_pShaderResourceView;	
	ID3D11UnorderedAccessView* m_pUnorderedAccessView;


};

#endif // RENDER_TARGET_H
