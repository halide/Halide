#include "QD3D10Widget.h"

class DynamicVertexBuffer;
class QTimer;

class SimpleTriangleWidget : public QD3D10Widget
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
	ID3D10InputLayout* m_pInputLayout;
	
	ID3D10Effect* m_pEffect;
	ID3D10EffectPass* m_pPass;

	float m_theta;
	QTimer* m_pAnimationTimer;

private slots:

	void handleTimeout();

};
