#ifndef DEPTH_STENCIL_TARGET_H
#define DEPTH_STENCIL_TARGET_H

#include <d3d10_1.h>
#include <d3d10.h>

class DepthStencilTarget
{
public:

	// TODO: test for failure in texture/view creation, return NULL

	static DepthStencilTarget* createDepthFloat24StencilUnsignedByte8( ID3D10Device* pDevice, int width, int height );	

	// untested!
	static DepthStencilTarget* createDepthFloat32( ID3D10Device* pDevice, int width, int height );	

	virtual ~DepthStencilTarget();
	
	int width();
	int height();

	ID3D10Texture2D* texture();
	ID3D10DepthStencilView* depthStencilView();

private:

	DepthStencilTarget( ID3D10Device* pDevice, int width, int height, ID3D10Texture2D* pTexture );

	int m_width;
	int m_height;

	ID3D10Texture2D* m_pTexture;
	ID3D10DepthStencilView* m_pDepthStencilView;	

};

#endif // DEPTH_STENCIL_TARGET_H
