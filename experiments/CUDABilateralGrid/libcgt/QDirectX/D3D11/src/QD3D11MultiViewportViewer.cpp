#include "QD3D11MultiViewportViewer.h"

#include <QApplication>
#include <QMouseEvent>

#include <geometry/GeometryUtils.h>

#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>
#include <vecmath/Matrix3f.h>
#include <vecmath/Quat4f.h>

#include "D3D11Utils.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

QD3D11MultiViewportViewer::QD3D11MultiViewportViewer( bool flipMouseUpDown,
						   float keyWalkSpeed,
						   float mousePitchSpeed,
						   QWidget* parent ) :

	m_flipMouseUpDown( flipMouseUpDown ),

	m_keyWalkSpeed( keyWalkSpeed ),
	m_mousePitchSpeed( mousePitchSpeed ),

	QD3D11Widget( parent )

{
	m_frontCamera.setDirectX( true );
	m_frontCamera.setLookAt( Vector3f( 0, 0, 5 ), Vector3f( 0, 0, 0 ), Vector3f( 0, 1, 0 ) );
	m_frontCamera.setOrtho( -5, 5, -5, 5, 0.01f, 10.0f );

	m_topCamera.setDirectX( true );
	m_topCamera.setLookAt( Vector3f( 0, 5, 0 ), Vector3f( 0, 0, 0 ), Vector3f( 0, 0, -1 ) );
	m_frontCamera.setOrtho( -5, 5, -5, 5, 0.01f, 10.0f );

	m_leftCamera.setDirectX( true );
	m_leftCamera.setLookAt( Vector3f( -5, 0, 0 ), Vector3f( 0, 0, 0 ), Vector3f( 0, 1, 0 ) );
	m_frontCamera.setOrtho( -5, 5, -5, 5, 0.01f, 10.0f );
	
    m_perspectiveCamera.setDirectX( true );
	m_perspectiveCamera.setLookAt( 5 * Vector3f( 1, 1, 1 ).normalized(), Vector3f( 0, 0, 0 ), Vector3f( -1, 1, -1 ).normalized() );
	m_perspectiveCamera.setPerspective( 50.f, 1.f, 0.01f, 10.0f );

	setUpVector( Vector3f( 0, 1, 0 ) );
}

Vector3f QD3D11MultiViewportViewer::upVector() const
{
	return m_groundPlaneToWorld.getCol( 1 );
}

void QD3D11MultiViewportViewer::setUpVector( const Vector3f& y )
{
	Vector3f x;
	Vector3f z;
	GeometryUtils::getBasis( y, &x, &z );
	m_groundPlaneToWorld = Matrix3f( x, y, z );
	m_worldToGroundPlane = m_groundPlaneToWorld.inverse();

	// TODO: snap camera to face up when you change the up vector to something new
	//   rotate along current lookat direction?
	// TODO: reset camera
}

bool QD3D11MultiViewportViewer::flipMouseUpDown() const
{
	return m_flipMouseUpDown;
}

float QD3D11MultiViewportViewer::keyWalkSpeed() const
{
	return m_keyWalkSpeed;
}

void QD3D11MultiViewportViewer::setKeyWalkSpeed( float speed )
{
	m_keyWalkSpeed = speed;
}

float QD3D11MultiViewportViewer::mousePitchSpeed() const
{
	return m_mousePitchSpeed;
}

void QD3D11MultiViewportViewer::setMousePitchSpeed( float speed )
{
	m_mousePitchSpeed = speed;
}

OrthographicCamera& QD3D11MultiViewportViewer::frontCamera()
{
	return m_frontCamera;
}

OrthographicCamera& QD3D11MultiViewportViewer::topCamera()
{
	return m_topCamera;
}

OrthographicCamera& QD3D11MultiViewportViewer::leftCamera()
{
	return m_leftCamera;
}

PerspectiveCamera& QD3D11MultiViewportViewer::perspectiveCamera()
{
	return m_perspectiveCamera;
}

//////////////////////////////////////////////////////////////////////////
// Public Slots
//////////////////////////////////////////////////////////////////////////

void QD3D11MultiViewportViewer::setFlipMouseUpDown( bool flip )
{
	m_flipMouseUpDown = flip;
}

//////////////////////////////////////////////////////////////////////////
// Protected
//////////////////////////////////////////////////////////////////////////

void QD3D11MultiViewportViewer::updateKeyboard()
{
	if ( !isActiveWindow() )
		return;

	if( ( GetAsyncKeyState( 'W' ) & 0x8000 ) != 0 )
	{
		translate( 0, 0, m_keyWalkSpeed );
	}

	if( ( GetAsyncKeyState( 'S' ) & 0x8000 ) != 0 )
	{
		translate( 0, 0, -m_keyWalkSpeed );
	}

	if( ( GetAsyncKeyState( 'A' ) & 0x8000 ) != 0 )
	{
		translate( -m_keyWalkSpeed, 0, 0 );
	}

	if( ( GetAsyncKeyState( 'D' ) & 0x8000 ) != 0 )
	{
		translate( m_keyWalkSpeed, 0, 0 );
	}

	if( ( GetAsyncKeyState( 'R' ) & 0x8000 ) != 0 )
	{
		translate( 0, m_keyWalkSpeed, 0 );
	}

	if( ( GetAsyncKeyState( 'F' ) & 0x8000 ) != 0 )
	{
		translate( 0, -m_keyWalkSpeed, 0 );
	}
}

// virtual
void QD3D11MultiViewportViewer::keyPressEvent( QKeyEvent* event )
{
	if( event->key() == Qt::Key_Escape ||
		event->key() == Qt::Key_Q )
	{
		qApp->quit();
	}

	update();
}

void QD3D11MultiViewportViewer::mousePressEvent( QMouseEvent * event )
{
	m_prevPos = Vector2i( event->x(), event->y() );
}

void QD3D11MultiViewportViewer::mouseMoveEvent( QMouseEvent* event )
{
	Vector2i curPos( event->x(), event->y() );
	Vector2f delta = curPos - m_prevPos;

#if 1

	float pitchSpeed = m_flipMouseUpDown ? m_mousePitchSpeed : -m_mousePitchSpeed;
	const float yawSpeed = 0.005f;
	const float panSpeed = 0.005f;
	const float walkSpeed = -0.005f;

	Matrix3f worldToCamera = m_perspectiveCamera.getViewMatrix().getSubmatrix3x3( 0, 0 );
	Matrix3f cameraToWorld = m_perspectiveCamera.getInverseViewMatrix().getSubmatrix3x3( 0, 0 );
	
	Vector3f eye = m_perspectiveCamera.getEye();
	Vector3f x = m_perspectiveCamera.getRight();
	Vector3f y = m_perspectiveCamera.getUp();
	Vector3f z = m_perspectiveCamera.getForward();

	// rotate
	if( event->buttons() == Qt::LeftButton )
	{
		// pitch around the local x axis		
		float pitch = pitchSpeed * delta.y;

		Matrix3f pitchMatrix = Matrix3f::rotateX( pitch );		

		y = cameraToWorld * pitchMatrix * worldToCamera * y;
		z = cameraToWorld * pitchMatrix * worldToCamera * z;

		// yaw around the world up vector
		float yaw = yawSpeed * delta.x;

		Matrix3f yawMatrix = m_groundPlaneToWorld * Matrix3f::rotateY( yaw ) * m_worldToGroundPlane;

		x = yawMatrix * x;
		y = yawMatrix * y;
		z = yawMatrix * z;		

		m_perspectiveCamera.setLookAt( eye, eye + z, y );
	}
	// walk
	else if( event->buttons() == Qt::RightButton )
	{
		float dx = panSpeed * delta.x;
		float dz = walkSpeed * delta.y;

		translate( dx, 0, dz );
	}
	// move up/down
	else if( event->buttons() == Qt::MiddleButton )
	{
		float dy = -panSpeed * delta.y;
		translate( 0, dy, 0 );
	}

#else

	if(event->buttons() & Qt::RightButton) //rotate
	{
		float rotSpeed = 0.005f; //radians per pixel
		Quat4f rotation;
		rotation.setAxisAngle(rotSpeed * delta.abs(), Vector3f(-delta[1], -delta[0], 0));
		Matrix3f rotMatrix = Matrix3f::rotation(rotation);
		Matrix3f viewMatrix = m_camera.getViewMatrix().getSubmatrix3x3(0, 0);
		rotMatrix = viewMatrix.transposed() * rotMatrix * viewMatrix;

		Vector3f eye, center, up;
		m_camera.getLookAt(&eye, &center, &up);
		m_camera.setLookAt(center + rotMatrix * (eye - center), center, rotMatrix * up);
	}
	else if(event->buttons() & Qt::LeftButton) //translate
	{
		float speed = 10.f;
		Vector3f screenDelta(delta[0], delta[1], 0);
		screenDelta[0] /= -double(width());
		screenDelta[1] /= double(height());
		Matrix4f iViewProjMatrix = m_camera.getInverseViewProjectionMatrix();
		Vector3f worldDelta = iViewProjMatrix.getSubmatrix3x3(0, 0) * (speed * screenDelta);

		Vector3f eye, center, up;
		m_camera.getLookAt(&eye, &center, &up);
		m_camera.setLookAt(eye + worldDelta, center + worldDelta, up);
	}
#endif

	m_prevPos = curPos;
	update();
}

void QD3D11MultiViewportViewer::mouseReleaseEvent( QMouseEvent * )
{
	m_prevPos = Vector2i( -1, -1 );
}

void QD3D11MultiViewportViewer::wheelEvent( QWheelEvent * event )
{
	//float speed = 0.002f;
	//float zoom = exp(event->delta() * speed);

 //   float fovY, aspect, zNear, zFar;
 //   m_camera.getPerspective(&fovY, &aspect, &zNear, &zFar);

 //   double h = tan(fovY * 0.5f);
 //   h /= zoom;
 //   fovY = 2.f * atan(h);

 //   m_camera.setPerspective(fovY, aspect, zNear, zFar);

 //   update();
}

void QD3D11MultiViewportViewer::resizeD3D( int width, int height )
{
    float fovY, aspect, zNear, zFar;
    m_perspectiveCamera.getPerspective( &fovY, &aspect, &zNear, &zFar );

    aspect = static_cast< float >( width ) / height;
	m_perspectiveCamera.setPerspective( fovY, aspect, zNear, zFar );

	m_frontViewport = D3D11Utils::createViewport( 0, 0, width / 2, height / 2, 0, 1 );
	m_topViewport = D3D11Utils::createViewport( width / 2, 0, width / 2, height / 2, 0, 1 );
	m_leftViewport = D3D11Utils::createViewport( 0, height / 2, width / 2, height / 2, 0, 1 );
	m_perspectiveViewport = D3D11Utils::createViewport( width / 2, height / 2, width / 2, height / 2, 0, 1 );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

void QD3D11MultiViewportViewer::translate( float dx, float dy, float dz )
{
	Vector3f eye = m_perspectiveCamera.getEye();
	Vector3f x = m_perspectiveCamera.getRight();
	Vector3f y = m_perspectiveCamera.getUp();
	Vector3f z = m_perspectiveCamera.getForward();

	// project the y axis onto the ground plane
	//Vector3f zp = m_worldToGroundPlane * z;
	//zp[ 1 ] = 0;
	//zp = m_groundPlaneToWorld * zp;
	//zp.normalize();

	// TODO: switch GLCamera over to have just a forward vector?
	// center is kinda stupid
	eye = eye + dx * x + dy * upVector() + dz * z;
	m_perspectiveCamera.setLookAt( eye, eye + z, y );
}
