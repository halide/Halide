#include "geometry/TriangleList3f.h"

#include <cassert>
#include <cfloat>

#include <common/BasicTypes.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>

// TODO: use Qt
#include "io/BinaryFileInputStream.h"
#include "io/BinaryFileWriter.h"

#include "geometry/IndexedFace.h"
#include "geometry/GeometryUtils.h"

using namespace std;

// ==============================================================
// Public
// ==============================================================

// static
TriangleList3f* TriangleList3f::create( const char* filename )
{
	TriangleList3f* triangleList = NULL;

	BinaryFileInputStream* pInputStream = BinaryFileInputStream::open( filename );
	if( pInputStream )
	{
		int header[3];
		int numVertices;
		bool bHasTextureCoordinates;
		bool bHasNormals;

		if( pInputStream->readIntArray( header, 3 ) )
		{
			numVertices = header[0];
			bHasTextureCoordinates = ( header[1] == 1 );
			bHasNormals = ( header[2] == 1 );

            float* positionArray = new float[ 3 * numVertices ];
			float* textureCoordinateArray = NULL;
			float* normalArray = NULL;

			pInputStream->readFloatArray( positionArray, 3 * numVertices );
			
			if( bHasTextureCoordinates )
			{
				textureCoordinateArray = new float[ 2 * numVertices ];
				pInputStream->readFloatArray( textureCoordinateArray, 2 * numVertices );
			}

			if( bHasNormals )
			{
				normalArray = new float[ 3 * numVertices ];
				pInputStream->readFloatArray( normalArray, 3 * numVertices );
			}

			triangleList = new TriangleList3f( positionArray,
				textureCoordinateArray,
				normalArray,
				numVertices );			
		}

		delete pInputStream;
	}

	return triangleList;
}

TriangleList3f* TriangleList3f::create( vector< Vector3f >* pvPositions,
									   vector< Vector2f >* pvTextureCoordinates,
									   vector< Vector3f >* pvNormals,
									   vector< IndexedFace >* pvIndexedFaces )
{
	assert( pvPositions != NULL );
	assert( pvTextureCoordinates != NULL );
	assert( pvNormals != NULL );
	assert( pvIndexedFaces != NULL );
	assert( pvIndexedFaces->size() > 0 );

	// ------------ determine if input has normals ------------
	bool bHasTextureCoordinates;
	bool bHasNormals;
	TriangleList3f::determineAttributes( pvIndexedFaces, &bHasTextureCoordinates, &bHasNormals );

	// ------------ count number of vertices to determine how big of an array to allocate ------------
	int totalNumVertices = TriangleList3f::countTotalNumVertices( pvIndexedFaces );

	printf( "# faces: %d\n", pvIndexedFaces->size() );
	printf( "totalNumVertices: %d\n", totalNumVertices );

	// ------------ allocate memory ------------
	float* afPositions = NULL; 
	float* afTextureCoordinates = NULL;
	float* afNormals = NULL;

	afPositions = new float[ 3 * totalNumVertices ];
	if( bHasTextureCoordinates )
	{
		afTextureCoordinates = new float[ 2 * totalNumVertices ];
	}
	if( bHasNormals )
	{
		afNormals = new float[ 3 * totalNumVertices ];
	}

	// populate data arrays
	TriangleList3f::populateDataArrays( pvPositions, pvTextureCoordinates, pvNormals, pvIndexedFaces,
		bHasTextureCoordinates, bHasNormals,
		afPositions, afTextureCoordinates, afNormals );

	return new TriangleList3f( afPositions, afTextureCoordinates, afNormals, totalNumVertices );
}

// virtual
TriangleList3f::~TriangleList3f()
{

}

// ========== I/O ==========

bool TriangleList3f::serialize( const char* filename )
{
	BinaryFileWriter* bfw = BinaryFileWriter::open( filename );

	if( bfw )
	{
		bfw->writeInt( m_iNumVertices );

		if( hasTextureCoordinates() )
		{
			bfw->writeInt( 1 );
		}
		else
		{
			bfw->writeInt( 0 );
		}

		if( hasNormals() )
		{
			bfw->writeInt( 1 );
		}
		else
		{
			bfw->writeInt( 0 );
		}

		bfw->writeFloatArray( m_afPositions, 3 * m_iNumVertices );
		
		if( hasTextureCoordinates() )
		{
			bfw->writeFloatArray( m_afTextureCoordinates, 2 * m_iNumVertices );
		}

		if( hasNormals() )
		{
			bfw->writeFloatArray( m_afNormals, 3 * m_iNumVertices );
		}

		return true;
	}
	else
	{
		return false;
	}
}

// ========== Geometry ==========
void TriangleList3f::recomputeBoundingBox()
{
	float minX = m_afPositions[0];
	float maxX = m_afPositions[0];
	float minY = m_afPositions[1];
	float maxY = m_afPositions[1];
	float minZ = m_afPositions[2];
	float maxZ = m_afPositions[2];

	for( int i = 3; i < 3 * m_iNumVertices; i += 3 )
	{
		float x = m_afPositions[i];
		float y = m_afPositions[i + 1];
		float z = m_afPositions[i + 2];

		if( x < minX )
		{
			minX = x;
		}
		if( x > maxX )
		{
			maxX = x;
		}
		if( y < minY )
		{
			minY = y;
		}
		if( y > maxY )
		{
			maxY = y;
		}
		if( z < minZ )
		{
			minZ = z;
		}
		if( z > maxZ )
		{
			maxZ = z;
		}
	}

	m_boundingBox.minimum() = Vector3f( minX, minY, minZ );
	m_boundingBox.maximum() = Vector3f( maxX, maxY, maxZ );
}

BoundingBox3f* TriangleList3f::getBoundingBox()
{
	return( &m_boundingBox );
}

int TriangleList3f::getNumVertices()
{
	return m_iNumVertices;
}

float* TriangleList3f::getPositions()
{
	return m_afPositions;
}

bool TriangleList3f::hasTextureCoordinates()
{
	return( m_afTextureCoordinates != NULL );
}

float* TriangleList3f::getTextureCoordinates()
{
	return m_afTextureCoordinates;
}

bool TriangleList3f::hasNormals()
{
	return( m_afNormals != NULL );
}

float* TriangleList3f::getNormals()
{
	return m_afNormals;
}

bool TriangleList3f::rayIntersection( const Vector3f& crRayOrigin, const Vector3f& crRayDirection,		
									 int* index,
									 float* t,
									 float* u, float* v )
{
	bool intersectedMesh = false;
	int intersectedPositionIndex = -1;
	float minT = FLT_MAX;
	float minU;
	float minV;

	for( int i = 0; i < 3 * m_iNumVertices; i += 9 )
	{
		Vector3f v0( m_afPositions[i], m_afPositions[i+1], m_afPositions[i+2] );
		Vector3f v1( m_afPositions[i+3], m_afPositions[i+4], m_afPositions[i+5] );
		Vector3f v2( m_afPositions[i+6], m_afPositions[i+7], m_afPositions[i+8] );

		float triangleT;
		float triangleU;
		float triangleV;

		bool intersectedTriangle = GeometryUtils::rayTriangleIntersection( crRayOrigin, crRayDirection,
			v0, v1, v2, &triangleT, &triangleU, &triangleV );
		if( intersectedTriangle )
		{
			intersectedMesh = true;
			if( ( triangleT > 0.0f ) && ( triangleT < FLT_MAX ) )
			{
				intersectedPositionIndex = i;
				minT = triangleT;
				minU = triangleU;
				minV = triangleV;
			}
		}
	}

	*index = intersectedPositionIndex / 3;
	*t = minT;
	*u = minU;
	*v = minV;
	return intersectedMesh;
}

// ==============================================================
// Private
// ==============================================================

TriangleList3f::TriangleList3f( float* afPositions, float* afTextureCoordinates, float* afNormals,
							   int numVertices ) :
	m_afPositions( afPositions ),
	m_afTextureCoordinates( afTextureCoordinates ),
	m_afNormals( afNormals ),
	m_iNumVertices( numVertices )
{
	recomputeBoundingBox();
}

// static
void TriangleList3f::determineAttributes( vector< IndexedFace >* pvIndexedFaces,
										 bool* pbHasTextureCoordinates, bool* pbHasNormals )
{
	IndexedFace* face0 = &( pvIndexedFaces->at( 0 ) );
	if( face0->getTextureCoordinateIndices()->size() > 0 )
	{
		*pbHasTextureCoordinates = true;
	}
	else
	{
		*pbHasTextureCoordinates = false;
	}

	if( face0->getNormalIndices()->size() > 0 )
	{
		*pbHasNormals = true;
	}
	else
	{
		*pbHasNormals = false;
	}
}

// static
int TriangleList3f::countTotalNumVertices( vector< IndexedFace >* pvIndexedFaces )
{
	int totalNumVertices = 0;

	for( uint i = 0; i < pvIndexedFaces->size(); ++i )
	{
		IndexedFace* face = &( pvIndexedFaces->at( i ) );
		uint numVerticesOnFace = static_cast< uint >( face->getPositionIndices()->size() );

		// for a polygon of N vertices
		// triangulating as a triangle list creates 3 * ( N - 2 ) vertices
		// ( N - 2 ) triangles, 3 vertices each
		totalNumVertices += 3 * ( numVerticesOnFace - 2 );
	}

	return totalNumVertices;
}

// static
void TriangleList3f::populateDataArrays( vector< Vector3f >* pvPositions, 
										vector< Vector2f >* pvTextureCoordinates,
										vector< Vector3f >* pvNormals,
										vector< IndexedFace >* pvIndexedFaces,
										bool bHasTextureCoordinates, bool bHasNormals,
										float* afPositions, float* afTextureCoordinates, float* afNormals )
{
	// Populate data arrays
	int positionArrayIndex = 0;
	int textureCoordinateArrayIndex = 0;
	int normalArrayIndex = 0;
	for( uint i = 0; i < pvIndexedFaces->size(); ++i )
	{
		// For the nth face
		IndexedFace* face = &( pvIndexedFaces->at( i ) );

		// get its position indices
		vector< int >* positionIndices = face->getPositionIndices();

		// and triangulate the face
		// using the first position being always on the triangle
		// and rotating around the polygon
		uint numVerticesOnFace = static_cast< uint >( positionIndices->size() );
		int positionIndex0 = positionIndices->at( 0 );		
		Vector3f& p0 = pvPositions->at( positionIndex0 );

		for( uint j = 1; j < numVerticesOnFace - 1; ++j )
		{
			int positionIndex1 = positionIndices->at( j );
			int positionIndex2 = positionIndices->at( j + 1 );
			Vector3f& p1 = pvPositions->at( positionIndex1 );
			Vector3f& p2 = pvPositions->at( positionIndex2 );

			// populate the float[]
			afPositions[ positionArrayIndex ] = p0[0];
			afPositions[ positionArrayIndex + 1 ] = p0[1];
			afPositions[ positionArrayIndex + 2 ] = p0[2];

			afPositions[ positionArrayIndex + 3 ] = p1[0];
			afPositions[ positionArrayIndex + 4 ] = p1[1];
			afPositions[ positionArrayIndex + 5 ] = p1[2];

			afPositions[ positionArrayIndex + 6 ] = p2[0];
			afPositions[ positionArrayIndex + 7 ] = p2[1];
			afPositions[ positionArrayIndex + 8 ] = p2[2];

			positionArrayIndex += 9;
		}		

		// do the same for texture coordinates
		if( bHasTextureCoordinates )
		{
			// TODO: decide on these asserts
			vector< int >* textureCoordinateIndices = face->getTextureCoordinateIndices();
			assert( textureCoordinateIndices->size() == numVerticesOnFace );

			int textureCoordinateIndex0 = textureCoordinateIndices->at( 0 );
			Vector2f& t0 = pvTextureCoordinates->at( textureCoordinateIndex0 );

			for( uint j = 1; j < numVerticesOnFace - 1; ++j )
			{
				int textureCoordinateIndex1 = textureCoordinateIndices->at( j );
				int textureCoordinateIndex2 = textureCoordinateIndices->at( j + 1 );
				Vector2f& t1 = pvTextureCoordinates->at( textureCoordinateIndex1 );
				Vector2f& t2 = pvTextureCoordinates->at( textureCoordinateIndex2 );

				// populate the float[]
				afTextureCoordinates[ textureCoordinateArrayIndex ] = t0[0];
				afTextureCoordinates[ textureCoordinateArrayIndex + 1 ] = t0[1];

				afTextureCoordinates[ textureCoordinateArrayIndex + 2 ] = t1[0];
				afTextureCoordinates[ textureCoordinateArrayIndex + 3 ] = t1[1];

				afTextureCoordinates[ textureCoordinateArrayIndex + 4 ] = t2[0];
				afTextureCoordinates[ textureCoordinateArrayIndex + 5 ] = t2[1];

				textureCoordinateArrayIndex += 6;
			}
		}

		// do the same for normals
		if( bHasNormals )
		{
			// TODO: decide on these asserts
			vector< int >* normalIndices = face->getNormalIndices();
			assert( normalIndices->size() == numVerticesOnFace );

			int normalIndex0 = normalIndices->at( 0 );
			Vector3f& n0 = pvNormals->at( normalIndex0 );

			for( uint j = 1; j < numVerticesOnFace - 1; ++j )
			{
				int normalIndex1 = normalIndices->at( j );
				int normalIndex2 = normalIndices->at( j + 1 );
				Vector3f& n1 = pvNormals->at( normalIndex1 );
				Vector3f& n2 = pvNormals->at( normalIndex2 );

				// populate the float[]
				afNormals[ normalArrayIndex ] = n0[0];
				afNormals[ normalArrayIndex + 1 ] = n0[1];
				afNormals[ normalArrayIndex + 2 ] = n0[2];

				afNormals[ normalArrayIndex + 3 ] = n1[0];
				afNormals[ normalArrayIndex + 4 ] = n1[1];
				afNormals[ normalArrayIndex + 5 ] = n1[2];

				afNormals[ normalArrayIndex + 6 ] = n2[0];
				afNormals[ normalArrayIndex + 7 ] = n2[1];
				afNormals[ normalArrayIndex + 8 ] = n2[2];

				normalArrayIndex += 9;
			}
		}
	}

	printf( "final position array index: %d\n", positionArrayIndex - 1 );
}