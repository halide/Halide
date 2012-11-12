#ifndef PERSPECTIVE_CAMERA_H
#define PERSPECTIVE_CAMERA_H

#include "Camera.h"

#include <QString>

class PerspectiveCamera : public Camera
{
public:

	// fFovY: field of view angle in the y direction
	// fAspect: aspect ratio in width over height (i.e. x over y)
	PerspectiveCamera( const Vector3f& vEye = Vector3f( 0, 0, 5 ),
		const Vector3f& vCenter = Vector3f( 0, 0, 0 ),
		const Vector3f& vUp = Vector3f( 0, 1, 0 ),
		float fFovY = 50.0f, float fAspect = 1.0f,
		float fZNear = 1.0f, float fZFar = 100.0f,
		bool bIsInfinite = false );

	//virtual ~GLPerspectiveCamera();
	
	// gets the parameters used to set this perspective camera
	// note that these are simply the cached values
	// the state can become *inconsistent* if GLCamera::setFrustum()
	// calls are made
	void getPerspective( float* pfFovY, float* pfAspect,
		float* pfZNear, float* pfZFar,
		bool* pbZFarIsInfinite = NULL );

	void setPerspective( float fFovY = 45.0f, float fAspect = 1.0f,
		float fZNear = 1.0f, float fZFar = 100.0f,
		bool bIsInfinite = false );

	float aspect() const;
	void setAspect( float fAspect );

	// TODO: switch to storing radians internally
	//float fovYRadians() const;
	//void setFovYRadians( float fovY );

	float fovYDegrees() const;
	void setFovYDegrees( float fovY );

	Matrix4f projectionMatrix() const;

	bool saveTXT( QString filename );
	static bool loadTXT( QString filename, PerspectiveCamera& camera );

	static PerspectiveCamera cubicInterpolate( const PerspectiveCamera& c0, const PerspectiveCamera& c1, const PerspectiveCamera& c2, const PerspectiveCamera& c3, float t );	

private:

	float m_fFovY;
	float m_fAspect;

};

#endif // PERSPECTIVE_CAMERA_H
