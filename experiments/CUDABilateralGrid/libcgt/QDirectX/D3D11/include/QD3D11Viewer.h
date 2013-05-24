#pragma once

#include <cameras/PerspectiveCamera.h>
#include <vecmath/Matrix3f.h>
#include <vecmath/Vector2i.h>
#include <vecmath/Vector3f.h>

#include "QD3D11Widget.h"
#include "FPSControls.h"

class QD3D11Viewer : public QD3D11Widget
{
	Q_OBJECT

public:

	QD3D11Viewer( bool flipMouseUpDown = true,
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

	PerspectiveCamera& camera();
	void setCamera( const PerspectiveCamera& camera );

	XboxController* xboxController0();

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

	// sample keyboard state (moves the camera)
	virtual void updateKeyboard();

	// sample xbox controller state (i.e. move the camera with thumbsticks)
	virtual void updateXboxController();

	FPSControls m_fpsControls;
	XboxController* m_pXboxController0;

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

	PerspectiveCamera m_camera;
};
