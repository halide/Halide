#pragma once

#include <QString>
#include <QVector>

#include "OBJFace.h"

// TODO: getFaces(), etc: don't return pointers

class OBJGroup
{
public:

	OBJGroup( QString name );

	QString name();

	// add a new material
	void addMaterial( QString materialName )
	{
		m_materials.append( materialName );
		m_facesByMaterial.append( QVector< OBJFace >() );
	}

	// TODO: store all faces in a single list
	// each material has a vertex range

	void addFace( const OBJFace& face )
	{
		m_facesByMaterial.last().append( face );
		m_faces.append( face );
	}

	QVector< QString >& getMaterials()
	{
		return m_materials;
	}

	QVector< OBJFace >& getFacesForMaterial( int materialIndex )
	{
		return m_facesByMaterial[ materialIndex ];
	}
	
	QVector< OBJFace >* getFaces()
	{
		return &m_faces;
	}

	int numFaces()
	{
		return m_faces.size();
	}

	bool hasTextureCoordinates();
	void setHasTextureCoordinates( bool b );

	bool hasNormals();
	void setHasNormals( bool b );

private:

	QString m_name;
	bool m_hasTextureCoordinates;
	bool m_hasNormals;

	QVector< QString > m_materials;
	QVector< QVector< OBJFace > > m_facesByMaterial;

	QVector< OBJFace > m_faces;

};
