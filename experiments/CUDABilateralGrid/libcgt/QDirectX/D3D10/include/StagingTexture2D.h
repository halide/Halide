#ifndef STAGING_TEXTURE_2D_H
#define STAGING_TEXTURE_2D_H

#include <d3d10_1.h>
#include <d3d10.h>

class StagingTexture2D
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static StagingTexture2D* createFloat1( ID3D10Device* pDevice, int width, int height );
	static StagingTexture2D* createFloat4( ID3D10Device* pDevice, int width, int height );
	static StagingTexture2D* createUnsignedShort1( ID3D10Device* pDevice, int width, int height );
	static StagingTexture2D* createUnsignedShort1UNorm( ID3D10Device* pDevice, int width, int height );
	static StagingTexture2D* createUnsignedByte4( ID3D10Device* pDevice, int width, int height );

	virtual ~StagingTexture2D();
	
	int width();
	int height();

	ID3D10Texture2D* texture();

	// read/write mapping
	D3D10_MAPPED_TEXTURE2D map();

	void unmap();

	// Copy from pSource to this
	void copyFrom( ID3D10Texture2D* pSource );

	// Copy from this to pTarget
	void copyTo( ID3D10Texture2D* pTarget );	

private:

	static D3D10_TEXTURE2D_DESC makeTextureDescription( int width, int height, DXGI_FORMAT format );

	StagingTexture2D( ID3D10Device* pDevice, int width, int height, ID3D10Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D10Device* m_pDevice;
	ID3D10Texture2D* m_pTexture;

};

#endif // STAGING_TEXTURE_2D_H
