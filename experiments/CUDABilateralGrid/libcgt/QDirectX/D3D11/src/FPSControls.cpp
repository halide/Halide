#include "FPSControls.h"

#include <cassert>
#include <geometry/GeometryUtils.h>

FPSMouseParameters::FPSMouseParameters( bool invertX, bool invertY,
	const Vector3f& translationPerPixel,
	float rotationRadiansPerPixel, float fovRadiansPerMouseWheelDelta ) :

	invertX( invertX ),
	invertY( invertY ),
	translationPerPixel( translationPerPixel ),
	rotationRadiansPerPixel( rotationRadiansPerPixel ),
	fovRadiansPerMouseWheelDelta( fovRadiansPerMouseWheelDelta )

{

}

FPSKeyboardParameters::FPSKeyboardParameters( float translationPerKeyPress ) :

	translationPerKeyPress( translationPerKeyPress )

{

}

FPSXboxGamepadParameters::FPSXboxGamepadParameters( bool invertX, bool invertY,
	float translationPerTick,
	float yawRadiansPerTick, float pitchRadiansPerTick,
	float fovRadiansPerTick ) :

	invertX( invertX ),
	invertY( invertY ),
	translationPerTick( translationPerTick ),
	yawRadiansPerTick( yawRadiansPerTick ),
	pitchRadiansPerTick( pitchRadiansPerTick ),
	fovRadiansPerTick( fovRadiansPerTick )

{

}

FPSControls::FPSControls( PerspectiveCamera* pCamera,
	const FPSMouseParameters& mouseParameters,
	const FPSKeyboardParameters& keyboardParameters,
	const FPSXboxGamepadParameters& xboxGamepadParameters ) :

	m_pCamera( pCamera ),

	m_mouseParameters( mouseParameters ),
	m_keyboardParameters( keyboardParameters ),
	m_xboxGamepadParameters( xboxGamepadParameters )

{
	setUpVector( Vector3f( 0, 1, 0 ) );
}

Vector3f FPSControls::upVector() const
{
	return m_groundPlaneToWorld.getCol( 1 );
}

void FPSControls::setUpVector( const Vector3f& y )
{
	Matrix3f b = GeometryUtils::getRightHandedBasis( y );
	m_groundPlaneToWorld.setCol( 0, b.getCol( 1 ) );
	m_groundPlaneToWorld.setCol( 1, b.getCol( 2 ) );
	m_groundPlaneToWorld.setCol( 2, b.getCol( 0 ) );

	m_worldToGroundPlane = m_groundPlaneToWorld.inverse();

	// TODO: snap camera to face up when you change the up vector to something new
	//   rotate along current lookat direction?
	// TODO: reset camera
}

FPSMouseParameters& FPSControls::mouseParameters()
{
	return m_mouseParameters;
}

FPSKeyboardParameters& FPSControls::keyboardParameters()
{
	return m_keyboardParameters;
}

FPSXboxGamepadParameters& FPSControls::xboxGamepadParameters()
{
	return m_xboxGamepadParameters;
}

void FPSControls::handleXboxController( XboxController* pXboxController )
{
	if( pXboxController->isConnected() && m_pCamera != nullptr )
    {
		XINPUT_STATE state = pXboxController->getState();
        computeXboxTranslation( &state.Gamepad );
        computeXboxRotation( &state.Gamepad );
        computeXboxFoV( &state.Gamepad );
    }
}

void FPSControls::computeXboxTranslation( XINPUT_GAMEPAD* pGamepad )
{
	int lx = pGamepad->sThumbLX;
	int ly = pGamepad->sThumbLY;

	bool move = false;
	float moveX = 0;
	float moveY = 0;
	float moveZ = 0;

	// left stick: move
	if( abs( lx ) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  )
	{
		moveX = lx * m_xboxGamepadParameters.translationPerTick;
		move = true;
	}
	if( abs( ly ) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE )
	{
		moveZ = ly * m_xboxGamepadParameters.translationPerTick;
		move = true;
	}

	// dpad: up/down
#if 0
	if( pGamepad->wButtons & XINPUT_GAMEPAD_DPAD_UP )
	{
		moveY = 0.01f;
		move = true;
	}	
	if( pGamepad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN )
	{
		moveY = -0.01f;
		move = true;
	}
#endif

	if( move )
	{
		applyTranslation( moveX, moveY, moveZ );
	}
}

void FPSControls::computeXboxRotation( XINPUT_GAMEPAD* pGamepad )
{
	bool doRotate = false;
    float yaw = 0;
    float pitch = 0;
	int rx = pGamepad->sThumbRX;
	int ry = pGamepad->sThumbRY;

    // right stick: rotate
    if( abs( rx ) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE )
    {
        yaw = rx * m_xboxGamepadParameters.yawRadiansPerTick;
		doRotate = true;
    }
    if( abs( ry ) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE )
    {
        pitch = ry * m_xboxGamepadParameters.pitchRadiansPerTick;
		doRotate = true;
    }

	if( doRotate )
	{
		if( m_xboxGamepadParameters.invertX )
		{
			yaw = -yaw;
		}
		if( m_xboxGamepadParameters.invertY )
		{
			pitch = -pitch;
		}

		applyRotation( yaw, pitch );
	}
}

void FPSControls::computeXboxFoV( XINPUT_GAMEPAD* pGamepad )
{
    if( m_pCamera == nullptr )
    {
        return;
    }

    ubyte lt = pGamepad->bLeftTrigger;
    ubyte rt = pGamepad->bRightTrigger;

    // do nothing if both triggers are held down
    if( lt > 0 && rt > 0 )
    {
        return;
    }
            
    float fov = MathUtils::degreesToRadians( m_pCamera->fovYDegrees() );

    // left trigger: zoom out
    if( lt > 0 )
    {
        fov += lt * m_xboxGamepadParameters.fovRadiansPerTick;
    }
    // right trigger: zoom in
    else
    {
        fov -= rt * m_xboxGamepadParameters.fovRadiansPerTick;
    }
	 
	float fovMin = MathUtils::degreesToRadians( 1.0f );
	float fovMax = MathUtils::degreesToRadians( 179.0f );
    fov = MathUtils::clampToRangeFloat( fov, fovMin, fovMax );
    
	float fovDegrees = MathUtils::radiansToDegrees( fov );
	m_pCamera->setFovYDegrees( fovDegrees );
}

void FPSControls::applyTranslation( float dx, float dy, float dz )
{
	Vector3f eye = m_pCamera->getEye();
	Vector3f x = m_pCamera->getRight();
	Vector3f y = m_pCamera->getUp();
	Vector3f z = m_pCamera->getForward();

	// project the y axis onto the ground plane
	//Vector3f zp = m_worldToGroundPlane * z;
	//zp[ 1 ] = 0;
	//zp = m_groundPlaneToWorld * zp;
	//zp.normalize();

	// TODO: switch Camera over to have just a forward vector?
	// center is kinda stupid
	eye = eye + dx * x + dy * upVector() + dz * z;
	m_pCamera->setLookAt( eye, eye + z, y );


	//Vector3f translation( moveX * ProjectedRight + moveY * GroundPlaneUp + moveZ * ProjectedForward )
}

void FPSControls::applyRotation( float yaw, float pitch )
{
	Matrix3f worldToCamera = m_pCamera->getViewMatrix().getSubmatrix3x3( 0, 0 );
	Matrix3f cameraToWorld = m_pCamera->getInverseViewMatrix().getSubmatrix3x3( 0, 0 );

	Vector3f eye = m_pCamera->getEye();
	Vector3f y = m_pCamera->getUp();
	Vector3f z = -( m_pCamera->getForward() );

	auto x = m_pCamera->getRight();

	// pitch around the local x axis		
	Matrix3f pitchMatrix = Matrix3f::rotateX( pitch );		

	y = cameraToWorld * pitchMatrix * worldToCamera * y;
	z = cameraToWorld * pitchMatrix * worldToCamera * z;

	// yaw around the world up vector
	Matrix3f yawMatrix = m_groundPlaneToWorld * Matrix3f::rotateY( yaw ) * m_worldToGroundPlane;
	y = yawMatrix * y;
	z = yawMatrix * z;	

	m_pCamera->setLookAt( eye, eye - z, y );

	auto z2 = -( m_pCamera->getForward() );
}