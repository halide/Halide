#ifndef STAGING_TEXTURE_2D_H
#define STAGING_TEXTURE_2D_H

#include <D3D11.h>

class StagingTexture2D
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static StagingTexture2D* createFloat1( ID3D11Device* pDevice, int width, int height );
	static StagingTexture2D* createFloat2( ID3D11Device* pDevice, int width, int height );
	static StagingTexture2D* createFloat4( ID3D11Device* pDevice, int width, int height );
	static StagingTexture2D* createUnsignedShort1( ID3D11Device* pDevice, int width, int height );
	static StagingTexture2D* createUnsignedShort1UNorm( ID3D11Device* pDevice, int width, int height );
	static StagingTexture2D* createUnsignedByte4( ID3D11Device* pDevice, int width, int height );

	virtual ~StagingTexture2D();
	
	int width();
	int height();

	ID3D11Texture2D* texture();

	// read/write mapping
	D3D11_MAPPED_SUBRESOURCE mapForReadWrite();
	void unmap();

	// Copy from pSource to this
	void copyFrom( ID3D11Texture2D* pSource );

	// Copy from this to pTarget
	void copyTo( ID3D11Texture2D* pTarget );

private:

	static D3D11_TEXTURE2D_DESC makeTextureDescription( int width, int height, DXGI_FORMAT format );

	StagingTexture2D( ID3D11Device* pDevice, int width, int height, ID3D11Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D11Device* m_pDevice;
	ID3D11DeviceContext* m_pContext;
	ID3D11Texture2D* m_pTexture;

};

#endif // STAGING_TEXTURE_2D_H
