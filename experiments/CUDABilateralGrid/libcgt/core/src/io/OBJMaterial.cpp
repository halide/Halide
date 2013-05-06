#include "io/OBJMaterial.h"

OBJMaterial::OBJMaterial( QString name ) :

	m_name( name ),
	m_illuminationModel( OBJMaterial::ILLUMINATION_MODEL_NONE ),

	m_ka( 0.2f, 0.2f, 0.2f ),
	m_kd( 0.8f, 0.8f, 0.8f ),
	m_ks( 1.0f, 1.0f, 1.0f ),

	m_d( 1 ),
	m_ns( 0 )

{

}

QString OBJMaterial::name()
{
	return m_name;
}

Vector3f OBJMaterial::ambientColor() const
{
	return m_ka;
}

void OBJMaterial::setAmbientColor( const Vector3f& color )
{
	m_ka = color;
}

Vector3f OBJMaterial::diffuseColor() const
{
	return m_kd;
}

void OBJMaterial::setDiffuseColor( const Vector3f& color )
{
	m_kd = color;
}

Vector3f OBJMaterial::specularColor() const
{
	return m_ks;
}

void OBJMaterial::setSpecularColor( const Vector3f& color )
{
	m_ks = color;
}

float OBJMaterial::alpha() const
{
	return m_d;
}

void OBJMaterial::setAlpha( float a )
{
	m_d = a;
}

float OBJMaterial::shininess() const
{
	return m_ns;
}

void OBJMaterial::setShininess( float s )
{
	m_ns = s;
}

QString OBJMaterial::ambientTexture() const
{
	return m_mapKa;
}

void OBJMaterial::setAmbientTexture( QString filename )
{
	m_mapKa = filename;
}

QString OBJMaterial::diffuseTexture() const
{
	return m_mapKd;
}

void OBJMaterial::setDiffuseTexture( QString filename )
{
	m_mapKd = filename;
}

OBJMaterial::ILLUMINATION_MODEL OBJMaterial::illuminationModel() const
{
	return m_illuminationModel;
}

void OBJMaterial::setIlluminationModel( OBJMaterial::ILLUMINATION_MODEL im )
{
	m_illuminationModel = im;
}