#ifndef QD3D11_MULTI_VIEWPORT_VIEWER_H
#define QD3D11_MULTI_VIEWPORT_VIEWER_H

#include "QD3D11Widget.h"
#include <cameras/OrthographicCamera.h>
#include <cameras/PerspectiveCamera.h>
#include <vecmath/Matrix3f.h>
#include <vecmath/Vector2i.h>
#include <vecmath/Vector3f.h>

class QD3D11MultiViewportViewer : public QD3D11Widget
{
	Q_OBJECT

public:

	QD3D11MultiViewportViewer( bool flipMouseUpDown = true,
		float keyWalkSpeed = 0.15f,
		float mousePitchSpeed = 0.005f,
		QWidget* parent = nullptr );

	bool flipMouseUpDown() const;

	// world units per key press
	float keyWalkSpeed() const;
	void setKeyWalkSpeed( float speed );

	// radians per pixel
	float mousePitchSpeed() const;
	void setMousePitchSpeed( float p );

	OrthographicCamera& frontCamera();
	OrthographicCamera& topCamera();
	OrthographicCamera& leftCamera();
	PerspectiveCamera& perspectiveCamera();

	Vector3f upVector() const;
	void setUpVector( const Vector3f& y );

public slots:

	void setFlipMouseUpDown( bool flip );

protected:

	// keyboard handlers
	virtual void keyPressEvent( QKeyEvent* event );

	// mouse handlers
	virtual void mousePressEvent( QMouseEvent* event );
	virtual void mouseMoveEvent( QMouseEvent* event );
	virtual void mouseReleaseEvent( QMouseEvent* event );
	virtual void wheelEvent( QWheelEvent* event );

	virtual void resizeD3D( int width, int height );

	virtual void updateKeyboard();

	D3D11_VIEWPORT m_frontViewport;
	D3D11_VIEWPORT m_topViewport;
	D3D11_VIEWPORT m_leftViewport;
	D3D11_VIEWPORT m_perspectiveViewport;

	OrthographicCamera m_frontCamera;
	OrthographicCamera m_topCamera;
	OrthographicCamera m_leftCamera;
	PerspectiveCamera m_perspectiveCamera;

private:

	void translate( float dx, float dy, float dz );

	// user controls
	bool m_flipMouseUpDown;

	// speed parameters
	float m_keyWalkSpeed;
	float m_mousePitchSpeed;

	Matrix3f m_groundPlaneToWorld; // goes from a coordinate system where the specified up vector is (0,1,0) to the world
	Matrix3f m_worldToGroundPlane;

	Vector2i m_prevPos;	
};


#endif // QD3D11_MULTI_VIEWPORT_VIEWER_H
