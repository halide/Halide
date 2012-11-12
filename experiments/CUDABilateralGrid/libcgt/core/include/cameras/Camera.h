#ifndef CAMERA_H
#define CAMERA_H

#include <cmath>
#include <cstdio>

#include <vecmath/Vector2i.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Matrix4f.h>

#include <QVector>

class BoundingBox3f;

class Camera
{
public:

	// Initializes to a camera at (0, 0, 5) looking at (0, 0, 0)
	// with zNear = 1, zFar = 100, an FOV of 50 degrees and an aspect ratio
	// of 50 degrees
	Camera( const Vector3f& vEye = Vector3f( 0, 0, 5 ),
		const Vector3f& vCenter = Vector3f( 0, 0, 0 ),
		const Vector3f& vUp = Vector3f( 0, 1, 0 ),
		float fLeft = -0.46630767, float fRight = 0.46630767,
		float fBottom = -0.46630767, float fTop = 0.46630767,
		float fZNear = 1.0f, float fZFar = 100.0f,
		bool bIsInfinite = false );

    void setDirectX( bool directX );

	void getFrustum( float* pfLeft, float* pfRight,
		float* pfBottom, float* pfTop,
		float* pfZNear, float* pfZFar,
		bool* pbZFarIsInfinite = NULL ) const;

	QVector< Vector3f > getFrustumCorners() const;

	bool isZFarInfinite();

	void setFrustum( float left, float right,
		float bottom, float top,
		float zNear, float zFar,
		bool bZFarIsInfinite = false );

	void getLookAt( Vector3f* pvEye, Vector3f* pvCenter, Vector3f* pvUp ) const;

	void setLookAt( const Vector3f& vEye,
		const Vector3f& vCenter,
		const Vector3f& vUp );

    Vector3f getEye() const { return m_vEye; }
	Vector3f getCenter() const { return m_vCenter; }

	void setEye( const Vector3f& vEye );
	void setCenter( const Vector3f& vCenter );
	void setUp( const Vector3f& vUp );

	void setForward( const Vector3f& forward );

	// return the "up" unit vector
	// "up" is recomputed to guarantee that up, right, and forward
	// form an orthonormal basis
	Vector3f getUp() const;

	// return the "right" unit vector
	Vector3f getRight() const;

	// return the "forward" unit vector
	Vector3f getForward() const;
	
	float getZNear() const;
	void setZNear( float zNear );

	float getZFar() const;
	void setZFar( float zFar );

	virtual Matrix4f projectionMatrix() const = 0;

	// returns the projection matrix P such that
	// the plane at a distance fFocusZ in front of the center of the lens
	// is kept constant while the eye has been moved
	// by (fEyeX, fEyeY) *in the plane of the lens*
	// i.e. fEyeX is in the direction of getRight()
	// and fEyeY is in the direction of getUp()
	Matrix4f getJitteredProjectionMatrix( float fEyeX, float fEyeY, float fFocusZ ) const;

	Matrix4f getViewMatrix() const;

	// returns the view matrix V such that
	// the eye has been moved by (fEyeX, fEyeY)
	// *in the plane of the lens*
	// i.e. fEyeX is in the direction of getRight()
	// and fEyeY is in the direction of getUp()
	Matrix4f getJitteredViewMatrix( float fEyeX, float fEyeY ) const;

	Matrix4f getViewProjectionMatrix() const;
	
	// equivalent to getJitteredProjectionMatrix() * getJitteredViewMatrix()
	Matrix4f getJitteredViewProjectionMatrix( float fEyeX, float fEyeY, float fFocusZ ) const;

	Matrix4f getInverseProjectionMatrix() const;
	Matrix4f getInverseViewMatrix() const;
	Matrix4f getInverseViewProjectionMatrix() const;

	//static Camera lerp( const Camera& a, const Camera& b, float t );
	//static Camera cubicInterpolate( const Camera& c0, const Camera& c1, const Camera& c2, const Camera& c3, float t );

	// given a 2D pixel (x,y) on a screen of size screenSize
	// returns a 3D ray direction
	// (call getEye() to get the ray origin)
	Vector3f pixelToDirection( const Vector2f& xy, const Vector2i& screenSize );

	// Given a point in the world and a screen of size screenSize
	// returns the 2D pixel coordinate (along with the nonlinear Z)
	Vector3f projectToScreen( const Vector4f& world, const Vector2i& screenSize );

protected:

	float m_left;
	float m_right;
	
	float m_top;
	float m_bottom;

	float m_fZNear;
	float m_fZFar;
	bool m_bZFarIsInfinite;

	Vector3f m_vEye;
	Vector3f m_vCenter;
	Vector3f m_vUp;

    bool m_bDirectX; // if the matrices are constructed for DirectX or OpenGL
};

#endif // CAMERA_H
