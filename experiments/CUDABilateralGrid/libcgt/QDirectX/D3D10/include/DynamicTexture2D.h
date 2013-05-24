#ifndef DYNAMIC_TEXTURE_2D_H
#define DYNAMIC_TEXTURE_2D_H

#include <d3d10_1.h>
#include <d3d10.h>

#include <vecmath/Vector2i.h>

class DynamicTexture2D
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static DynamicTexture2D* createFloat1( ID3D10Device* pDevice, int width, int height );
	static DynamicTexture2D* createFloat2( ID3D10Device* pDevice, int width, int height );
	static DynamicTexture2D* createFloat4( ID3D10Device* pDevice, int width, int height );
	static DynamicTexture2D* createUnsignedShort1( ID3D10Device* pDevice, int width, int height );
	static DynamicTexture2D* createUnsignedShort1UNorm( ID3D10Device* pDevice, int width, int height );
	static DynamicTexture2D* createUnsignedByte4( ID3D10Device* pDevice, int width, int height );

	virtual ~DynamicTexture2D();
	
	int width();
	int height();
	Vector2i size();

	ID3D10Texture2D* texture();	
	ID3D10ShaderResourceView* shaderResourceView();

	D3D10_MAPPED_TEXTURE2D map();
	void unmap();

private:

	static D3D10_TEXTURE2D_DESC makeTextureDescription( int width, int height, DXGI_FORMAT format );

	DynamicTexture2D( ID3D10Device* pDevice, int width, int height, ID3D10Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D10Texture2D* m_pTexture;
	ID3D10ShaderResourceView* m_pShaderResourceView;	

};

#endif // DYNAMIC_TEXTURE_2D_H
