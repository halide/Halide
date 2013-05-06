#include "io/OBJGroup.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

OBJGroup::OBJGroup( QString name ) :

	m_name( name ),
	m_hasNormals( true ),
	m_hasTextureCoordinates( false )

{
	addMaterial( "" );
}

QString OBJGroup::name()
{
	return m_name;
}

bool OBJGroup::hasTextureCoordinates()
{
	return m_hasTextureCoordinates;
}

void OBJGroup::setHasTextureCoordinates( bool b )
{
	m_hasTextureCoordinates = b;
}

bool OBJGroup::hasNormals()
{
	return m_hasNormals;
}

void OBJGroup::setHasNormals( bool b )
{
	m_hasNormals = b;
}
