#include "io/OBJData.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

OBJData::OBJData()
{
	
}

// virtual
OBJData::~OBJData()
{
	foreach( OBJMaterial* pMaterial, m_materials )
	{
		delete pMaterial;
	}
	foreach( OBJGroup* pGroup, m_groups )
	{
		delete pGroup;
	}
}

QVector< Vector3f >* OBJData::getPositions()
{
	return &m_positions;
}

QVector< Vector2f >* OBJData::getTextureCoordinates()
{
	return &m_textureCoordinates;
}

QVector< Vector3f >* OBJData::getNormals()
{
	return &m_normals;
}

QVector< OBJGroup* >* OBJData::getGroups()
{
	return &m_groups;
}

QHash< QString, OBJGroup* >* OBJData::getGroupsByName()
{
	return &m_groupsByName;
}

OBJGroup* OBJData::addGroup( QString name )
{
	if( !( m_groupsByName.contains( name ) ) )
	{
		m_groupsByName.insert( name, new OBJGroup( name ) );
		m_groups.append( m_groupsByName[ name ] );
	}

	return m_groupsByName[ name ];
}

OBJGroup* OBJData::getGroupByName( QString name )
{
	return m_groupsByName[ name ];
}

bool OBJData::containsGroup( QString name )
{
	return m_groupsByName.contains( name );
}

OBJMaterial* OBJData::addMaterial( QString name )
{
	if( !( m_materials.contains( name ) ) )
	{
		m_materials.insert( name, new OBJMaterial( name ) );
	}

	return m_materials[ name ];
}

OBJMaterial* OBJData::getMaterial( QString name )
{
	return m_materials[ name ];
}

bool OBJData::containsMaterial( QString name )
{
	return m_materials.contains( name );
}

bool OBJData::save( QString filename )
{
	FILE* fp = fopen( filename.toAscii().constData(), "w" );

	for( int i = 0; i < m_positions.size(); ++i )
	{
		Vector3f v = m_positions[ i ];
		fprintf( fp, "v %f %f %f\n", v.x, v.y, v.z );
	}

	for( int i = 0; i < m_textureCoordinates.size(); ++i )
	{
		Vector2f t = m_textureCoordinates[ i ];
		fprintf( fp, "vt %f %f\n", t.x, t.y );
	}

	for( int i = 0; i < m_normals.size(); ++i )
	{
		Vector3f n = m_normals[ i ];
		fprintf( fp, "vn %f %f %f\n", n.x, n.y, n.z );
	}

	QVector< Vector3f > m_positions;
	QVector< Vector2f > m_textureCoordinates;
	QVector< Vector3f > m_normals;

	foreach( OBJGroup* pGroup, m_groups )
	{
		int materialIndex = 0;
		QVector< QString >& materials = pGroup->getMaterials();
		foreach( QString materialName, materials )
		{
			// TODO: skip over materials referenced by no faces
			QVector< OBJFace >& faces = pGroup->getFacesForMaterial( materialIndex );
			for( int i = 0; i < faces.size(); ++i )
			{
				OBJFace& face = faces[ i ];

				auto pis = face.getPositionIndices();
				auto tis = face.getTextureCoordinateIndices();
				auto nis = face.getNormalIndices();

				int nIndices = pis->size();
				fprintf( fp, "f" );
				for( int j = 0; j < nIndices; ++j )
				{
					int pi = pis->at( j ) + 1;
					fprintf( fp, " %d", pi );

					if( pGroup->hasTextureCoordinates() )
					{
						int ti = tis->at( j ) + 1;
						if( pGroup->hasNormals() )
						{
							int ni = nis->at( j ) + 1;
							fprintf( fp, "/%d/%d", ti, ni );
						}
						else
						{
							fprintf( fp, "/%d", ti );
						}
					}					
					else if( pGroup->hasNormals() )
					{
						int ni = nis->at( j ) + 1;
						fprintf( fp, "//%d", ni );
					}
				}
				fprintf( fp, "\n" );
			}

			++materialIndex;
		}		
	}

	fclose( fp );

	// TODO: handle errors in writing
	return true;
}