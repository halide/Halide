#pragma once

#include <memory>
#include <QString>
#include <d3d11.h>

#include <imageproc/Image4ub.h>

class RenderTarget;
class DepthStencilTarget;
class StagingTexture2D;

class SequenceExporter
{
public:

	SequenceExporter( ID3D11Device* pDevice, int width, int height, QString prefix, int startFrameIndex = 0 );
	virtual ~SequenceExporter();

	D3D11_VIEWPORT viewport();
	std::shared_ptr< RenderTarget > renderTarget();
	std::shared_ptr< DepthStencilTarget > depthStencilTarget();

	// saves the current render target, depth stencil target, and viewport
	void begin();

	// clears the color and depth stencil targets
	void beginFrame();

	// saves 
	void endFrame();

	// restore the render target, depth stencil target, and viewport
	void end();

	// in case you want to start somewhere else, or re-start and overwrite
	void setFrameIndex( int i );

private:

	int m_frameIndex;
	int m_width;
	int m_height;
	QString m_prefix;

	ID3D11DeviceContext* m_pImmediateContext;

	std::shared_ptr< RenderTarget > m_pRT;
	std::shared_ptr< DepthStencilTarget > m_pDST;
	std::shared_ptr< StagingTexture2D > m_pStagingTexture;
	D3D11_VIEWPORT m_viewport;
	Image4ub m_image;

	// saved for begin / end
	D3D11_VIEWPORT m_savedViewport;
	ID3D11RenderTargetView* m_pSavedRTV;
	ID3D11DepthStencilView* m_pSavedDSV;
};