#ifndef ORTHOGRAPHIC_CAMERA_H
#define ORTHOGRAPHIC_CAMERA_H

#include <QString>

#include "Camera.h"
#include "vecmath/Matrix4f.h"
#include "vecmath/Vector4f.h"

class OrthographicCamera : public Camera
{
public:

	OrthographicCamera( const Vector3f& eye = Vector3f( 0, 0, 5 ),
		const Vector3f& center = Vector3f( 0, 0, 0 ),
		const Vector3f& up = Vector3f( 0, 1, 0 ),
		float left = -5.0f, float right = 5.0f,
		float bottom = -5.0f, float top = 5.0f,
		float zNear = -1.0f, float zFar = 1.0f );		

	//virtual ~OrthographicCamera();
	
	// gets the parameters used to set this orthographic camera
	// note that these are simply the cached values
	// the state can become *inconsistent* if GLCamera::setFrustum() calls are made
	void getOrtho( float* pLeft, float* pRight, float* pBottom, float* pTop, float* pZNear, float* pZFar ) const;

	void setOrtho( float left, float right, float bottom, float top, float zNear, float zFar );

	virtual Matrix4f projectionMatrix() const;

	bool saveTXT( QString filename );

	//static PerspectiveCamera cubicInterpolate( const PerspectiveCamera& c0, const PerspectiveCamera& c1, const PerspectiveCamera& c2, const PerspectiveCamera& c3, float t );	

private:

};

#endif // ORTHOGRAPHIC_CAMERA_H
