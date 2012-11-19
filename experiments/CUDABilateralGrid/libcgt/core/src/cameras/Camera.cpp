#include "cameras/Camera.h"

#include <math/MathUtils.h>
#include <vecmath/Vector4f.h>
#include <vecmath/Quat4f.h>

#include "geometry/BoundingBox3f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Camera::Camera( const Vector3f& vEye,
				   const Vector3f& vCenter,
				   const Vector3f& vUp,
				   float fLeft, float fRight,
				   float fBottom, float fTop,
				   float fZNear, float fZFar,
				   bool bIsInfinite )
: m_bZFarIsInfinite(bIsInfinite)
{
	setLookAt( vEye, vCenter, vUp );
	setFrustum( fLeft, fRight,
		fBottom, fTop,
		fZNear, fZFar,
		false );
}

// virtual
//Camera::~Camera()
//{

//}

void Camera::setDirectX( bool directX )
{
	m_bDirectX = directX;
}

void Camera::getFrustum( float* pfLeft, float* pfRight,
						  float* pfBottom, float* pfTop,
						  float* pfZNear, float* pfZFar,
						  bool* pbZFarIsInfinite ) const
{
	*pfLeft = m_left;
	*pfRight = m_right;

	*pfBottom = m_bottom;
	*pfTop = m_top;

	*pfZNear = m_fZNear;
	*pfZFar = m_fZFar;

	if( pbZFarIsInfinite != NULL )
	{
		*pbZFarIsInfinite = m_bZFarIsInfinite;
	}
}

QVector< Vector3f > Camera::getFrustumCorners() const
{
	QVector< Vector3f > out( 8 );

	Vector4f cubePoint( 0.f, 0.f, 0.f, 1.f );
	Matrix4f invProj = getInverseViewProjectionMatrix();

	// take the NDC cube and unproject it
	for( int i = 0; i < 8; ++i )
	{
		cubePoint[1] = ( i & 2 ) ? 1.f : -1.f;
		cubePoint[0] = ( ( i & 1 ) ? 1.f : -1.f ) * cubePoint[1]; // so vertices go around in order
		// cubePoint[2] = ( i & 4 ) ? 1.f : ( m_bDirectX ? 0.f : -1.f );
		cubePoint[2] = ( i & 4 ) ? 1.f : 0;

		out[ i ] = ( invProj * cubePoint ).homogenized().xyz();
	}

	return out;
}

bool Camera::isZFarInfinite()
{
	return m_bZFarIsInfinite;
}

void Camera::setFrustum( float left, float right,
				float bottom, float top,
				float zNear, float zFar,
				bool bZFarIsInfinite )
{
	m_left = left;
	m_right = right;

	m_bottom = bottom;
	m_top = top;

	m_fZNear = zNear;
	m_fZFar = zFar;

	m_bZFarIsInfinite = bZFarIsInfinite;
}

void Camera::getLookAt( Vector3f* pvEye,
						 Vector3f* pvCenter,
						 Vector3f* pvUp ) const
{
	*pvEye = m_vEye;
	*pvCenter = m_vCenter;
	*pvUp = m_vUp;
}

void Camera::setLookAt( const Vector3f& vEye,
						 const Vector3f& vCenter,
						 const Vector3f& vUp )
{
	m_vEye = vEye;
	m_vCenter = vCenter;
	m_vUp = vUp;

#if 0
	m_vEye = vEye;
	m_vCenter = vCenter;
	m_vUp = vUp.normalized();
	
	// recompute up to ensure an orthonormal basis
	m_vUp = Vector3f::cross( -getForward(), getRight() );
#endif
}

void Camera::setEye( const Vector3f& vEye )
{
	m_vEye = vEye;
}

void Camera::setCenter( const Vector3f& vCenter )
{
	m_vCenter = vCenter;	
}

void Camera::setUp( const Vector3f& vUp )
{
	m_vUp = vUp;
}

void Camera::setForward( const Vector3f& forward )
{
	m_vCenter = m_vEye - forward;
}

Vector3f Camera::getUp() const
{
	return m_vUp;
}

Vector3f Camera::getRight() const
{
	return Vector3f::cross( getForward(), m_vUp );
}

Vector3f Camera::getForward() const
{
	return ( m_vCenter - m_vEye ).normalized();
}

float Camera::getZNear() const
{
	return m_fZNear;
}

void Camera::setZNear( float zNear )
{
	m_fZNear = zNear;
}

float Camera::getZFar() const
{
	return m_fZFar;
}

void Camera::setZFar( float zFar )
{
	m_fZFar = zFar;
}

Matrix4f Camera::getJitteredProjectionMatrix( float fEyeX, float fEyeY, float fFocusZ ) const
{
	float dx = -fEyeX * m_fZNear / fFocusZ;
	float dy = -fEyeY * m_fZNear / fFocusZ;

	if( m_bZFarIsInfinite )
	{
		return Matrix4f::infinitePerspectiveProjection( m_left + dx, m_right + dx,
			m_bottom + dy, m_top + dy,
			m_fZNear, m_bDirectX );
	}
	else
	{
		return Matrix4f::perspectiveProjection( m_left + dx, m_right + dx,
			m_bottom + dy, m_top + dy,
			m_fZNear, m_fZFar, m_bDirectX );
	}
}

Matrix4f Camera::getViewMatrix() const
{
	return Matrix4f::lookAt( m_vEye, m_vCenter, m_vUp );
}

Matrix4f Camera::getJitteredViewMatrix( float fEyeX, float fEyeY ) const
{
	Matrix4f view;

	// z is negative forward
	Vector3f z = -getForward();
	Vector3f y = getUp();
	Vector3f x = getRight();

	// the x, y, and z vectors define the orthonormal coordinate system
	// the affine part defines the overall translation

	Vector3f jitteredEye = m_vEye + fEyeX * x + fEyeY * y;

	view.setRow( 0, Vector4f( x, -Vector3f::dot( x, jitteredEye ) ) );
	view.setRow( 1, Vector4f( y, -Vector3f::dot( y, jitteredEye ) ) );
	view.setRow( 2, Vector4f( z, -Vector3f::dot( z, jitteredEye ) ) );
	view.setRow( 3, Vector4f( 0, 0, 0, 1 ) );

	return view;
}

Matrix4f Camera::getViewProjectionMatrix() const
{
	return projectionMatrix() * getViewMatrix();
}

Matrix4f Camera::getJitteredViewProjectionMatrix( float fEyeX, float fEyeY, float fFocusZ ) const
{
	return
		(
			getJitteredProjectionMatrix( fEyeX, fEyeY, fFocusZ ) *
			getJitteredViewMatrix( fEyeX, fEyeY )
		);
}

Matrix4f Camera::getInverseProjectionMatrix() const
{
	return projectionMatrix().inverse();
}

Matrix4f Camera::getInverseViewMatrix() const
{
	return getViewMatrix().inverse();
}

Matrix4f Camera::getInverseViewProjectionMatrix() const
{
	return getViewProjectionMatrix().inverse();
}

Vector3f Camera::pixelToDirection( const Vector2f& xy, const Vector2i& screenSize )
{
	// convert from screen coordinates to NDC
	float ndcX = 2 * xy.x / screenSize.x - 1;
	float ndcY = 2 * xy.y / screenSize.y - 1;

	Vector4f clip( ndcX, ndcY, 0, 1 );
	Vector4f eye = getInverseProjectionMatrix() * clip;
	Vector4f world = getInverseViewMatrix() * eye;
	
	Vector3f pointOnNearPlane = world.homogenized().xyz();

	return ( pointOnNearPlane - m_vEye ).normalized();
}

Vector3f Camera::projectToScreen( const Vector4f& world, const Vector2i& screenSize )
{
	Vector4f clip = getViewProjectionMatrix() * world;
	Vector4f ndc = clip.homogenized();

	float sx = screenSize.x * 0.5f * ( ndc.x + 1.0f );
	float sy = screenSize.y * 0.5f * ( ndc.y + 1.0f );

	// OpenGL:
	// float sz = 0.5f * ( ndc.z + 1.0f );
	float sz = ndc.z;
	// float w = clip.w?

	return Vector3f( sx, sy, sz );
}

#if 0
// static
Camera Camera::lerp( const Camera& a, const Camera& b, float t )
{
	float left = MathUtils::lerp( a.m_left, b.m_left, t );
	float right = MathUtils::lerp( a.m_right, b.m_right, t );

	float top = MathUtils::lerp( a.m_top, b.m_top, t );
	float bottom = MathUtils::lerp( a.m_bottom, b.m_bottom, t );

	float zNear = MathUtils::lerp( a.m_fZNear, b.m_fZNear, t );
	float zFar = MathUtils::lerp( a.m_fZFar, b.m_fZFar, t );

	bool farIsInfinite = a.m_bZFarIsInfinite;
	bool isDirectX = a.m_bDirectX;

	Vector3f position = Vector3f::lerp( a.m_vEye, b.m_vEye, t );
	
	Quat4f qA = Quat4f::fromRotatedBasis( a.getRight(), a.getUp(), -( a.getForward() ) );
	Quat4f qB = Quat4f::fromRotatedBasis( b.getRight(), b.getUp(), -( b.getForward() ) );
	Quat4f q = Quat4f::slerp( qA, qB, t );

	Vector3f x = q.rotateVector( Vector3f::RIGHT );
	Vector3f y = q.rotateVector( Vector3f::UP );
	Vector3f z = q.rotateVector( -Vector3f::FORWARD );

	Vector3f center = position - z;

	Camera camera
	(
		position, center, y,
		left, right, bottom, top, zNear, zFar, farIsInfinite
	);
	camera.m_bDirectX = isDirectX;

	return camera;
}

// static
Camera Camera::cubicInterpolate( const Camera& c0, const Camera& c1, const Camera& c2, const Camera& c3, float t )
{
	float left = MathUtils::cubicInterpolate( c0.m_left, c1.m_left, c2.m_left, c3.m_left, t );
	float right = MathUtils::cubicInterpolate( c0.m_right, c1.m_right, c2.m_right, c3.m_right, t );

	float top = MathUtils::cubicInterpolate( c0.m_top, c1.m_top, c2.m_top, c3.m_top, t );
	float bottom = MathUtils::cubicInterpolate( c0.m_bottom, c1.m_bottom, c2.m_bottom, c3.m_bottom, t );

	float zNear = MathUtils::cubicInterpolate( c0.m_fZNear, c1.m_fZNear, c2.m_fZNear, c3.m_fZNear, t );
	float zFar = MathUtils::cubicInterpolate( c0.m_fZFar, c1.m_fZFar, c2.m_fZFar, c3.m_fZFar, t );

	bool farIsInfinite = c0.m_bZFarIsInfinite;
	bool isDirectX = c0.m_bDirectX;

	Vector3f position = Vector3f::cubicInterpolate( c0.m_vEye, c1.m_vEye, c2.m_vEye, c3.m_vEye, t );

	Quat4f q0 = Quat4f::fromRotatedBasis( c0.getRight(), c0.getUp(), -( c0.getForward() ) );	
	Quat4f q1 = Quat4f::fromRotatedBasis( c1.getRight(), c1.getUp(), -( c1.getForward() ) );	
	Quat4f q2 = Quat4f::fromRotatedBasis( c2.getRight(), c2.getUp(), -( c2.getForward() ) );	
	Quat4f q3 = Quat4f::fromRotatedBasis( c3.getRight(), c3.getUp(), -( c3.getForward() ) );	

	Quat4f q = Quat4f::cubicInterpolate( q0, q1, q2, q3, t );

	Vector3f x = q.rotateVector( Vector3f::RIGHT );
	Vector3f y = q.rotateVector( Vector3f::UP );
	Vector3f z = q.rotateVector( -Vector3f::FORWARD );

	Vector3f center = position - z;

	Camera camera
	(
		position, center, y,
		left, right, bottom, top, zNear, zFar, farIsInfinite
	);
	camera.m_bDirectX = isDirectX;

	return camera;
}
#endif