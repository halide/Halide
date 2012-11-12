#include "io/OBJLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
//#include <QRegExp>

#include "io/OBJData.h"
#include "io/OBJGroup.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
std::shared_ptr< OBJData > OBJLoader::loadFile( QString objFilename )
{
	int lineNumber = 0;
	QString line = "";
	
	std::shared_ptr< OBJData > pOBJData( new OBJData );
	OBJMaterial* pCurrentMaterial = pOBJData->addMaterial( "" ); // default material name is the empty string
	OBJGroup* pCurrentGroup = pOBJData->addGroup( "" ); // default group name is the empty string
	
	bool succeeded = parseOBJ( objFilename, pOBJData );
	if( !succeeded )
	{
		// return null
		pOBJData.reset();
	}

	return pOBJData;
}


//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
bool OBJLoader::parseOBJ( QString objFilename, std::shared_ptr< OBJData > pOBJData )
{
	// attempt to read the file
	QFile inputFile( objFilename );
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	int lineNumber = 0;
	QString line = "";	
	OBJGroup* pCurrentGroup = pOBJData->getGroupByName( "" ); // default group name is the empty string

	QTextStream inputTextStream( &inputFile );

	QString delim( " " );

	line = inputTextStream.readLine();
	while( !( line.isNull() ) )
	{
		if( line != "" )
		{
			QStringList tokens = line.split( delim, QString::SkipEmptyParts );

			if( tokens.size() > 0 )
			{
				QString commandToken = tokens[ 0 ];

				if( commandToken == "mtllib" )
				{
					QString mtlRelativeFilename = tokens[ 1 ];
					QFileInfo objFileInfo( objFilename );
					QDir objDir = objFileInfo.dir();
					QString mtlAbsoluteFilename = objDir.absolutePath() + "/" + mtlRelativeFilename;
					parseMTL( mtlAbsoluteFilename, pOBJData );
				}
				else if( commandToken == "g" )
				{
					QString newGroupName;

					if( tokens.size() < 2 )
					{
						fprintf( stderr, "Warning: group has no name, defaulting to ""\nline: %d\n%s",
							lineNumber, qPrintable( line ) );

						newGroupName = "";
					}
					else
					{
						newGroupName = tokens[ 1 ];
					}

					if( newGroupName != pCurrentGroup->name() )
					{
						if( pOBJData->containsGroup( newGroupName ) )
						{
							pCurrentGroup = pOBJData->getGroupByName( newGroupName );
						}
						else
						{
							pCurrentGroup = pOBJData->addGroup( newGroupName );
						}						
					}
				}
				else if( commandToken == "v" )
				{
					// TODO: error checking on all of these and tear down pOBJData
					OBJLoader::parsePosition( lineNumber, line, tokens, pOBJData );
				}
				else if( commandToken == "vt" )
				{
					OBJLoader::parseTextureCoordinate( lineNumber, line, tokens, pOBJData );
				}
				else if( commandToken == "vn" )
				{
					OBJLoader::parseNormal( lineNumber, line, tokens, pOBJData );
				}
				else if( commandToken == "usemtl" )
				{
					pCurrentGroup->addMaterial( tokens[ 1 ] );
				}
				else if( commandToken == "f" || commandToken == "fo" )
				{
					OBJLoader::parseFace( lineNumber, line,
						tokens, pCurrentGroup );
				}
			}
		}

		++lineNumber;
		line = inputTextStream.readLine();
	}

	return true;
}

// static
bool OBJLoader::parseMTL( QString mtlFilename, std::shared_ptr< OBJData > pOBJData )
{
	// attempt to read the file
	QFile inputFile( mtlFilename );
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	int lineNumber = 0;
	QString line;
	OBJMaterial* pCurrentMaterial = pOBJData->getMaterial( "" );

	QRegExp splitExp( "\\s+" );

	QTextStream inputTextStream( &inputFile );
	line = inputTextStream.readLine();
	while( !( line.isNull() ) )
	{
		if( line != "" )
		{
			QStringList tokens = line.split( splitExp, QString::SkipEmptyParts );

			if( tokens.size() > 0 )
			{
				QString commandToken = tokens[ 0 ];

				if( commandToken == "newmtl" )
				{
					QString newMaterialName;

					if( tokens.size() < 2 )
					{
						fprintf( stderr, "Warning: material has no name, defaulting to ""\nline: %d\n%s",
							lineNumber, qPrintable( line ) );

						newMaterialName = "";
					}
					else
					{
						newMaterialName = tokens[ 1 ];
					}

					// if the new material's name isn't the same as the current one
					if( newMaterialName != pCurrentMaterial->name() )
					{
						// but if it exists, then just set it as current
						if( pOBJData->containsGroup( newMaterialName ) )
						{
							pCurrentMaterial = pOBJData->getMaterial( newMaterialName );
						}
						// otherwise, make a new one and set it as current
						else
						{
							pCurrentMaterial = pOBJData->addMaterial( newMaterialName );
						}						
					}
				}
				else if( commandToken == "Ka" )
				{
					float r = tokens[ 1 ].toFloat();
					float g = tokens[ 2 ].toFloat();
					float b = tokens[ 3 ].toFloat();
					pCurrentMaterial->setAmbientColor( Vector3f( r, g, b ) );
				}
				else if( commandToken == "Kd" )
				{
					float r = tokens[ 1 ].toFloat();
					float g = tokens[ 2 ].toFloat();
					float b = tokens[ 3 ].toFloat();
					pCurrentMaterial->setDiffuseColor( Vector3f( r, g, b ) );
				}
				else if( commandToken == "Ks" )
				{
					float r = tokens[ 1 ].toFloat();
					float g = tokens[ 2 ].toFloat();
					float b = tokens[ 3 ].toFloat();
					pCurrentMaterial->setSpecularColor( Vector3f( r, g, b ) );
				}
				else if( commandToken == "d" )
				{
					float d = tokens[ 1 ].toFloat();
					pCurrentMaterial->setAlpha( d );
				}
				else if( commandToken == "Ns" )
				{
					float ns = tokens[ 1 ].toFloat();
					pCurrentMaterial->setShininess( ns );
				}
				else if( commandToken == "illum" )
				{
					int il = tokens[ 1 ].toInt();
					OBJMaterial::ILLUMINATION_MODEL illum = static_cast< OBJMaterial::ILLUMINATION_MODEL >( il );
					pCurrentMaterial->setIlluminationModel( illum );
				}
				else if( commandToken == "map_Ka" )
				{
					if( tokens.size() > 1 )
					{
						pCurrentMaterial->setAmbientTexture( tokens[ 1 ] );
					}
				}
				else if( commandToken == "map_Kd" )
				{
					if( tokens.size() > 1 )
					{
						pCurrentMaterial->setDiffuseTexture( tokens[ 1 ] );
					}
				}
			}
		}

		++lineNumber;
		line = inputTextStream.readLine();
	}

	return true;
}

// static
bool OBJLoader::parsePosition( int lineNumber, QString line,
							  QStringList tokens, std::shared_ptr< OBJData > pOBJData )
{
	if( tokens.size() < 4 )
	{
		fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
			lineNumber, qPrintable( line ) );
		return false;
	}
	else
	{
		bool succeeded;

		float x = tokens[ 1 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		float y = tokens[ 2 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		float z = tokens[ 3 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		pOBJData->getPositions()->append( Vector3f( x, y, z ) );

		return true;
	}
}

// static
bool OBJLoader::parseTextureCoordinate( int lineNumber, QString line,
									   QStringList tokens, std::shared_ptr< OBJData > pOBJData )
{
	if( tokens.size() < 3 )
	{
		fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
			lineNumber, qPrintable( line ) );
		return false;
	}
	else
	{
		bool succeeded;

		float s = tokens[ 1 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		float t = tokens[ 2 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}		

		pOBJData->getTextureCoordinates()->append( Vector2f( s, t ) );

		return true;
	}
}

// static
bool OBJLoader::parseNormal( int lineNumber, QString line,
							QStringList tokens, std::shared_ptr< OBJData > pOBJData )
{
	if( tokens.size() < 4 )
	{
		fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
			lineNumber, qPrintable( line ) );
		return false;
	}
	else
	{
		bool succeeded;

		float nx = tokens[ 1 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		float ny = tokens[ 2 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		float nz = tokens[ 3 ].toFloat( &succeeded );
		if( !succeeded )
		{
			fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}

		pOBJData->getNormals()->append( Vector3f( nx, ny, nz ) );

		return true;
	}
}

// static
bool OBJLoader::parseFace( int lineNumber, QString line,
						  QStringList tokens, OBJGroup* pCurrentGroup )
{
	// HACK
	/*
	if( tokens.size() < 4 )
	{
		fprintf( stderr, "Incorrect number of tokens at line number: %d\n, %s\n",
			lineNumber, qPrintable( line ) );
		return false;
	}
	else
	*/
	{
		// first check line consistency - each vertex in the face
		// should have the same number of attributes

		// HACK
		bool faceIsValid = true;
		bool faceHasTextureCoordinates = false;
		bool faceHasNormals = true;
		/*
		bool faceIsValid;
		bool faceHasTextureCoordinates;
		bool faceHasNormals;

		faceIsValid = OBJLoader::isFaceLineAttributesConsistent( tokens,
			&faceHasTextureCoordinates, &faceHasNormals );

		if( !faceIsValid )
		{
			fprintf( stderr, "Face attributes inconsistent at line number: %d\n%s\n",
				lineNumber, qPrintable( line ) );
			return false;
		}
		*/

		// ensure that all faces in a group are consistent
		// they either all have texture coordinates or they don't
		// they either all have normals or they don't
		// 
		// check how many faces the current group has
		// if the group has no faces, then the first vertex sets it

		if( pCurrentGroup->getFaces()->size() == 0 )
		{
			pCurrentGroup->setHasTextureCoordinates( faceHasTextureCoordinates );
			pCurrentGroup->setHasNormals( faceHasNormals );
		}

		bool faceIsConsistentWithGroup = ( pCurrentGroup->hasTextureCoordinates() == faceHasTextureCoordinates ) &&
			( pCurrentGroup->hasNormals() == faceHasNormals );

		if( !faceIsConsistentWithGroup )
		{
			fprintf( stderr, "Face attributes inconsistent with group: %s at line: %d\n%s\n",
				qPrintable( pCurrentGroup->name() ), lineNumber, qPrintable( line ) );
			fprintf( stderr, "group.hasTextureCoordinates() = %d\n", pCurrentGroup->hasTextureCoordinates() );
			fprintf( stderr, "face.hasTextureCoordinates() = %d\n", faceHasTextureCoordinates );
			fprintf( stderr, "group.hasNormals() = %d\n", pCurrentGroup->hasNormals() );
			fprintf( stderr, "face.hasNormals() = %d\n", faceHasNormals );
			
			return false;
		}

		OBJFace face( faceHasTextureCoordinates, faceHasNormals );

		// for each vertex
		for( int i = 1; i < tokens.size(); ++i )
		{
			int vertexPositionIndex;
			int vertexTextureCoordinateIndex;
			int vertexNormalIndex;

			OBJLoader::getVertexAttributes( tokens[ i ],
				&vertexPositionIndex, &vertexTextureCoordinateIndex, &vertexNormalIndex );

			face.getPositionIndices()->append( vertexPositionIndex );

			if( faceHasTextureCoordinates )
			{
				face.getTextureCoordinateIndices()->append( vertexTextureCoordinateIndex );
			}
			if( faceHasNormals )
			{
				face.getNormalIndices()->append( vertexNormalIndex );
			}
		}

		pCurrentGroup->addFace( face );
		return true;
	}
}

// static
bool OBJLoader::isFaceLineAttributesConsistent( QStringList tokens,
											   bool* pHasTextureCoordinates, bool* pHasNormals )
{
	int firstVertexPositionIndex;
	int firstVertexTextureCoordinateIndex;
	int firstVertexNormalIndex;

	bool firstVertexIsValid;
	bool firstVertexHasTextureCoordinates;
	bool firstVertexHasNormals;

	firstVertexIsValid = OBJLoader::getVertexAttributes( tokens[1],
		&firstVertexPositionIndex, &firstVertexTextureCoordinateIndex, &firstVertexNormalIndex );
	firstVertexHasTextureCoordinates = ( firstVertexTextureCoordinateIndex != -1 );
	firstVertexHasNormals = ( firstVertexNormalIndex != -1 );

	if( !firstVertexIsValid )
	{
		*pHasTextureCoordinates = false;
		*pHasNormals = false;
		return false;
	}

	for( int i = 2; i < tokens.size(); ++i )
	{
		int vertexPositionIndex;
		int vertexTextureCoordinateIndex;
		int vertexNormalIndex;

		bool vertexIsValid;
		bool vertexHasTextureCoordinates;
		bool vertexHasNormals;

		vertexIsValid = OBJLoader::getVertexAttributes( tokens[i],
			&vertexPositionIndex, &vertexTextureCoordinateIndex, &vertexNormalIndex );
		vertexHasTextureCoordinates = ( vertexTextureCoordinateIndex != -1 );
		vertexHasNormals = ( vertexNormalIndex != -1 );

		if( !vertexIsValid )
		{
			*pHasTextureCoordinates = false;
			*pHasNormals = false;
			return false;
		}

		if( firstVertexHasTextureCoordinates != vertexHasTextureCoordinates )
		{
			*pHasTextureCoordinates = false;
			*pHasNormals = false;
			return false;
		}

		if( firstVertexHasNormals != vertexHasNormals )
		{
			*pHasTextureCoordinates = false;
			*pHasNormals = false;
			return false;
		}
	}

	*pHasTextureCoordinates = firstVertexHasTextureCoordinates;
	*pHasNormals = firstVertexHasNormals;
	return true;
}

// static
bool OBJLoader::getVertexAttributes( QString objFaceVertexToken,
									int* pPositionIndex, int* pTextureCoordinateIndex, int* pNormalIndex )
{
	QStringList vertexAttributes = objFaceVertexToken.split( "/", QString::KeepEmptyParts );
	int vertexNumAttributes = vertexAttributes.size();

	// check if it has position
	if( vertexNumAttributes < 1 )
	{
		*pPositionIndex = -1;
		*pTextureCoordinateIndex = -1;
		*pNormalIndex = -1;
		return false;
	}
	else
	{
		if( vertexAttributes[0] == "" )
		{
			*pPositionIndex = -1;
			*pTextureCoordinateIndex = -1;
			*pNormalIndex = -1;
			return false;
		}
		else
		{
			// TODO: error checking on parsing the ints?
			*pPositionIndex = vertexAttributes[ 0 ].toInt() - 1;

			if( vertexNumAttributes > 1 )
			{
				if( vertexAttributes[1] == "" )
				{
					*pTextureCoordinateIndex = -1;
				}
				else
				{
					*pTextureCoordinateIndex = vertexAttributes[ 1 ].toInt() - 1;
				}

				if( vertexNumAttributes > 2 )
				{
					if( vertexAttributes[2] == "" )
					{
						*pNormalIndex = -1;
					}
					else
					{
						*pNormalIndex = vertexAttributes[ 2 ].toInt() - 1;
					}
				}
				else
				{
					*pNormalIndex = -1;
				}				
			}
			else
			{
				*pTextureCoordinateIndex = -1;
				*pNormalIndex = -1;
			}
		}

		return true;
	}
}
