#include "QD3D11Widget.h"

#include <d3dx11effect.h>

class DynamicVertexBuffer;
class QTimer;

class SimpleTriangleWidget : public QD3D11Widget
{
Q_OBJECT

public:

	SimpleTriangleWidget();

	bool m_bRotating;

protected:

	virtual void initializeD3D();
	virtual void resizeD3D( int width, int height );
	virtual void paintD3D();

private:

	void loadShaders();

	DynamicVertexBuffer* m_pVertexBuffer;	
	ID3D11InputLayout* m_pInputLayout;
	
	ID3DX11Effect* m_pEffect;
	ID3DX11EffectPass* m_pPass;

	float m_theta;
	QTimer* m_pAnimationTimer;

private slots:

	void handleTimeout();

};
