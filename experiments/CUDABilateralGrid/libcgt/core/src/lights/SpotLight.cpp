#include "lights/SpotLight.h"

#include <cmath>
#include <QFile>
#include <QTextStream>

#include <vecmath/Vector4f.h>

SpotLight::SpotLight( const Vector3f& position, const Vector3f& center, const Vector3f& up,
		  float fovYRadians, float zNear, float zFar, float aspect,
		  const Vector3f& color, const Vector3f& distanceFalloff ) :

	m_fovYRadians( fovYRadians ),
	m_zNear( zNear ),
	m_zFar( zFar ),

	m_aspect( aspect ),

	m_color( color ),
	m_distanceFalloff( distanceFalloff )

{
	setLookAt( position, center, up );
}

Vector3f SpotLight::color() const
{
	return m_color;
}

void SpotLight::setColor( const Vector3f& color )
{
	m_color = color;
}

Vector3f SpotLight::distanceFalloff() const
{
	return m_distanceFalloff;
}

void SpotLight::setDistanceFalloff( const Vector3f& distanceFalloff )
{
	m_distanceFalloff = distanceFalloff;
}

void SpotLight::setLookAt( const Vector3f& position,
						  const Vector3f& center,
						  const Vector3f& up )
{
	m_position = position;
	m_center = center;
	m_up = up;
}

const Vector3f& SpotLight::position() const
{
	return m_position;
}

void SpotLight::setPosition( const Vector3f& position )
{
	m_position = position;
}

float SpotLight::fovYRadians() const
{
	return m_fovYRadians;
}

void SpotLight::setFovYRadians( float fov )
{
	m_fovYRadians = fov;
}

float SpotLight::aspectRatio() const
{
	return m_aspect;
}

void SpotLight::setAspectRatio( float a )
{
	m_aspect = a;
}

const Vector3f& SpotLight::center() const
{
	return m_center;
}

void SpotLight::setCenter( const Vector3f& center )
{
	m_center = center;
}

const Vector3f& SpotLight::up() const
{
	return m_up;
}

void SpotLight::setUp( const Vector3f& up )
{
	m_up = up;
}

void SpotLight::setUpWithRight( const Vector3f& right )
{
	m_up = Vector3f::cross( right, lightDirection() );
}

float SpotLight::zNear() const
{
	return m_zNear;
}

void SpotLight::setZNear( float zNear )
{
	m_zNear = zNear;
}

float SpotLight::zFar() const
{
	return m_zFar;
}

void SpotLight::setZFar( float zFar )
{
	m_zFar = zFar;
}

Vector3f SpotLight::lightDirection() const
{
	return ( m_center - m_position ).normalized();
}

Vector3f SpotLight::right() const
{
	return Vector3f::cross( lightDirection(), m_up );
}

Matrix4f SpotLight::lightProjectionMatrix() const
{
	return projectionMatrix() * lightMatrix();
}

Matrix4f SpotLight::inverseLightProjectionMatrix() const
{
	return lightProjectionMatrix().inverse();
}

Matrix4f SpotLight::lightMatrix() const
{
	return Matrix4f::lookAt( m_position, m_center, m_up );
}

Matrix4f SpotLight::inverseLightMatrix() const
{
	return lightMatrix().inverse();
}

Matrix4f SpotLight::projectionMatrix() const
{
	return Matrix4f::perspectiveProjection
	(
		m_fovYRadians, m_aspect,
		m_zNear, m_zFar,
		true
	);
}

void SpotLight::rectangleAlignedAt( float z,
								   Vector3f* bottomLeft, Vector3f* bottomRight,
								   Vector3f* topRight, Vector3f* topLeft ) const
{
	float top = z * tanf( 0.5f * m_fovYRadians );
	float bottom = -top;
	float right = m_aspect * top;
	float left = -right;

	Vector3f worldRight = this->right();

	*topLeft = m_position + top * m_up + left * worldRight + z * lightDirection();            
	*bottomLeft = m_position + bottom * m_up + left * worldRight + z * lightDirection();
	*bottomRight = m_position + bottom * m_up + right * worldRight + z * lightDirection();
	*topRight = m_position + top * m_up + right * worldRight + z * lightDirection();
}

QVector< Vector3f > SpotLight::getFrustumCorners() const
{
	QVector< Vector3f > out( 8 );

	Vector4f cubePoint( 0.f, 0.f, 0.f, 1.f );
	Matrix4f invProj = inverseLightProjectionMatrix();

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

bool SpotLight::saveTXT( QString filename )
{
	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "UTF-8" );

	outputTextStream << "position " << m_position[ 0 ] << " " << m_position[ 1 ] << " " << m_position[ 2 ] << "\n";
	outputTextStream << "center " << m_center[ 0 ] << " " << m_center[ 1 ] << " " << m_center[ 2 ] << "\n";
	outputTextStream << "up " << m_up[ 0 ] << " " << m_up[ 1 ] << " " << m_up[ 2 ] << "\n";
	outputTextStream << "zNear " << m_zNear << "\n";
	outputTextStream << "zFar " << m_zFar << "\n";
	outputTextStream << "fovYDegrees " << MathUtils::radiansToDegrees( m_fovYRadians ) << "\n";
	outputTextStream << "aspect " << m_aspect << "\n";

	outputFile.close();
	return true;
}
