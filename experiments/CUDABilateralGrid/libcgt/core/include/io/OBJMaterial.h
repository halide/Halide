#pragma once

#include <QString>
#include <QVector>

#include "vecmath/Vector3f.h"

class OBJMaterial
{
public:

	enum ILLUMINATION_MODEL
	{
		ILLUMINATION_MODEL_NONE = 0,
		ILLUMINATION_MODEL_DIFFUSE = 1,
		ILLUMINATION_MODEL_DIFFUSE_AND_SPECULAR = 2
	};

	OBJMaterial( QString name );

	QString name();
	
	Vector3f ambientColor() const;
	void setAmbientColor( const Vector3f& color );

	Vector3f diffuseColor() const;
	void setDiffuseColor( const Vector3f& color );

	Vector3f specularColor() const;
	void setSpecularColor( const Vector3f& color );

	float alpha() const;
	void setAlpha( float a );

	float shininess() const;
	void setShininess( float s );
	
	QString ambientTexture() const;
	void setAmbientTexture( QString filename );

	QString diffuseTexture() const;
	void setDiffuseTexture( QString filename );

	ILLUMINATION_MODEL illuminationModel() const;
	void setIlluminationModel( ILLUMINATION_MODEL im );

private:

	// required
	QString m_name;
	ILLUMINATION_MODEL m_illuminationModel;

	Vector3f m_ka;
	Vector3f m_kd;
	Vector3f m_ks;
	
	float m_d; // alpha
	// float m_tr; // 1 - alpha
	float m_ns; // shininess
	
	QString m_mapKa; // ambient texture
	QString m_mapKd; // diffuse texture

	// TODO: parse others
	// http://www.fileformat.info/format/material/
};
