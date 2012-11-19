#include "cameras/PerspectiveCamera.h"

#include <QFile>
#include <QTextStream>

#include <math/MathUtils.h>
#include <vecmath/Quat4f.h>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

PerspectiveCamera::PerspectiveCamera( const Vector3f& vEye,
										 const Vector3f& vCenter,
										 const Vector3f& vUp,
										 float fFovY, float fAspect,
										 float fZNear, float fZFar,
										 bool bIsInfinite )
{
	setPerspective( fFovY, fAspect, fZNear, fZFar, bIsInfinite );
	setLookAt( vEye, vCenter, vUp );
}

// virtual
//PerspectiveCamera::~PerspectiveCamera()
//{

//}

void PerspectiveCamera::getPerspective( float* pfFovY, float* pfAspect,
										 float* pfZNear, float* pfZFar,
										 bool* pbZFarIsInfinite )
{
	*pfFovY = m_fFovY;
	*pfAspect = m_fAspect;

	*pfZNear = m_fZNear;
	*pfZFar = m_fZFar;

	if( pbZFarIsInfinite != NULL )
	{
		*pbZFarIsInfinite = m_bZFarIsInfinite;
	}
}

void PerspectiveCamera::setPerspective( float fFovY, float fAspect,
	float fZNear, float fZFar,
	bool bIsInfinite )
{
	m_fFovY = fFovY;
	m_fAspect = fAspect;

	// tan( theta / 2 ) = up / zNear
	float halfFovY = MathUtils::degreesToRadians( fFovY / 2.0f );
	float tanHalfFovY = tan( halfFovY );

	float top = fZNear * tanHalfFovY;
	float bottom = -top;

	// aspect = width / height = ( right - left ) / ( top - bottom )
	float right = fAspect * top;
	float left = -right;

	setFrustum( left, right, bottom, top, fZNear, fZFar, bIsInfinite );
}

float PerspectiveCamera::aspect() const
{
	return m_fAspect;
}

void PerspectiveCamera::setAspect( float fAspect )
{
	m_fAspect = fAspect;

	// HACK

	// tan( theta / 2 ) = up / zNear
	float halfFovY = MathUtils::degreesToRadians( m_fFovY / 2.0f );
	float tanHalfFovY = tan( halfFovY );

	float top = m_fZNear * tanHalfFovY;
	float bottom = -top;

	// aspect = width / height = ( right - left ) / ( top - bottom )
	float right = fAspect * top;
	float left = -right;

	setFrustum( left, right, bottom, top, m_fZNear, m_fZFar, m_bZFarIsInfinite );
}

float PerspectiveCamera::fovYDegrees() const
{
	return m_fFovY;
}

void PerspectiveCamera::setFovYDegrees( float fovY )
{
	m_fFovY = fovY;
	// HACK

	// tan( theta / 2 ) = up / zNear
	float halfFovY = MathUtils::degreesToRadians( m_fFovY / 2.0f );
	float tanHalfFovY = tan( halfFovY );

	float top = m_fZNear * tanHalfFovY;
	float bottom = -top;

	// aspect = width / height = ( right - left ) / ( top - bottom )
	float right = m_fAspect * top;
	float left = -right;

	setFrustum( left, right, bottom, top, m_fZNear, m_fZFar, m_bZFarIsInfinite );
}

// virtual
Matrix4f PerspectiveCamera::projectionMatrix() const
{
	if( m_bZFarIsInfinite )
	{
		return Matrix4f::infinitePerspectiveProjection( m_left, m_right,
			m_bottom, m_top,
            m_fZNear, m_bDirectX );
	}
	else
	{
		return Matrix4f::perspectiveProjection( m_left, m_right,
			m_bottom, m_top,
			m_fZNear, m_fZFar, m_bDirectX );
	}
}

// static
bool PerspectiveCamera::loadTXT( QString filename, PerspectiveCamera& camera )
{
	QFile inputFile( filename );

	// try to open the file in write only mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	QTextStream inputTextStream( &inputFile );
	inputTextStream.setCodec( "UTF-8" );

	QString str;
	int i;

	Vector3f eye;
	Vector3f center;
	Vector3f up;
	float zNear;
	float zFar;
	float fovY;
	float aspect;

	bool isInfinite;
	bool isDirectX;

	inputTextStream >> str >> eye[ 0 ] >> eye[ 1 ] >> eye[ 2 ];
	inputTextStream >> str >> center[ 0 ] >> center[ 1 ] >> center[ 2 ];
	inputTextStream >> str >> up[ 0 ] >> up[ 1 ] >> up[ 2 ];
	inputTextStream >> str >> zNear;
	inputTextStream >> str >> zFar;
	inputTextStream >> str >> i;
	isInfinite = ( i != 0 );
	inputTextStream >> str >> fovY;
	inputTextStream >> str >> aspect;
	inputTextStream >> str >> i;
	isDirectX = ( i != 0 );

	inputFile.close();

	camera.setLookAt( eye, center, up );
	camera.setPerspective( fovY, aspect, zNear, zFar, isInfinite );
	camera.setDirectX( isDirectX );

	return true;
}

bool PerspectiveCamera::saveTXT( QString filename )
{
	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "UTF-8" );

	outputTextStream << "eye " << m_vEye[ 0 ] << " " << m_vEye[ 1 ] << " " << m_vEye[ 2 ] << "\n";
	outputTextStream << "center " << m_vCenter[ 0 ] << " " << m_vCenter[ 1 ] << " " << m_vCenter[ 2 ] << "\n";
	outputTextStream << "up " << m_vUp[ 0 ] << " " << m_vUp[ 1 ] << " " << m_vUp[ 2 ] << "\n";
	outputTextStream << "zNear " << m_fZNear << "\n";
	outputTextStream << "zFar " << m_fZFar << "\n";
	outputTextStream << "zFarInfinite " << static_cast< int >( m_bZFarIsInfinite ) << "\n";
	outputTextStream << "fovY " << m_fFovY << "\n";
	outputTextStream << "aspect " << m_fAspect << "\n";
	outputTextStream << "isDirectX " << static_cast< int >( m_bDirectX ) << "\n";

	outputFile.close();
	return true;
}

// static
PerspectiveCamera PerspectiveCamera::cubicInterpolate( const PerspectiveCamera& c0, const PerspectiveCamera& c1, const PerspectiveCamera& c2, const PerspectiveCamera& c3, float t )
{
	float fov = MathUtils::cubicInterpolate( c0.m_fFovY, c1.m_fFovY, c2.m_fFovY, c3.m_fFovY, t );
	float aspect = MathUtils::cubicInterpolate( c0.m_fAspect, c1.m_fAspect, c2.m_fAspect, c3.m_fAspect, t );

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

	PerspectiveCamera camera
	(
		position, center, y,
		fov, aspect,
		zNear, zFar, farIsInfinite
	);
	camera.m_bDirectX = isDirectX;

	return camera;
}