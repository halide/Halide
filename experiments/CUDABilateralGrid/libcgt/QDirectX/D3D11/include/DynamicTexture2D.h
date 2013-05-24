#pragma once

#include <D3D11.h>

#include <vecmath/Vector2i.h>

class DynamicTexture2D
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static DynamicTexture2D* createFloat1( ID3D11Device* pDevice, int width, int height );
	static DynamicTexture2D* createFloat2( ID3D11Device* pDevice, int width, int height );
	static DynamicTexture2D* createFloat4( ID3D11Device* pDevice, int width, int height );
	static DynamicTexture2D* createUnsignedShort1( ID3D11Device* pDevice, int width, int height );
	static DynamicTexture2D* createUnsignedShort1UNorm( ID3D11Device* pDevice, int width, int height );
	static DynamicTexture2D* createUnsignedByte4( ID3D11Device* pDevice, int width, int height );

	virtual ~DynamicTexture2D();
	
	int width();
	int height();
	Vector2i size();

	ID3D11Texture2D* texture();	
	ID3D11ShaderResourceView* shaderResourceView();	

	D3D11_MAPPED_SUBRESOURCE mapForWriteDiscard();
	void unmap();

private:

	static D3D11_TEXTURE2D_DESC makeTextureDescription( int width, int height, DXGI_FORMAT format );

	DynamicTexture2D( ID3D11Device* pDevice, int width, int height, ID3D11Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D11Texture2D* m_pTexture;
	ID3D11ShaderResourceView* m_pShaderResourceView;	

	ID3D11DeviceContext* m_pContext;

};
