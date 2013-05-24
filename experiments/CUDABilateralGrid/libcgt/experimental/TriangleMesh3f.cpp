#include "geometry/TriangleMesh3f.h"

#include <cassert>
#include <cstdlib>


#if 0

#include <GL/glew.h>
#include <GL/glut.h>

using namespace std;

// destructor
TriangleMesh3f::~TriangleMesh3f()
{
	if( m_pVertexBuffer != NULL )
	{
		delete m_pVertexBuffer;
	}

	if( m_pTextureCoordinateBuffer != NULL )
	{
		delete m_pTextureCoordinateBuffer;
	}

	if( m_pNormalBuffer != NULL )
	{
		delete m_pNormalBuffer;
	}
}

vector< Vector3f >* TriangleMesh3f::getVertices()
{
	return &m_vVertices;
}

vector< Vector2f >* TriangleMesh3f::getTextureCoordinates()
{
	return &m_vTextureCoordinates;
}

vector< Vector3f >* TriangleMesh3f::getNormals()
{
	return &m_vNormals;
}

void TriangleMesh3f::recomputeBoundingBox()
{
	Vector3f* v0 = &( m_vVertices[0] );

	float minX = v0->elements[0];
	float maxX = v0->elements[0];
	float minY = v0->elements[1];
	float maxY = v0->elements[1];
	float minZ = v0->elements[2];
	float maxZ = v0->elements[2];

	for( uint i = 1; i < m_vVertices.size(); ++i )
	{
		Vector3f* pv = &( m_vVertices[i] );
		if( pv->elements[0] < minX )
		{
			minX = pv->elements[0];
		}
		if( pv->elements[0] > maxX )
		{
			maxX = pv->elements[0];
		}
		if( pv->elements[1] < minY )
		{
			minY = pv->elements[1];
		}
		if( pv->elements[1] > maxY )
		{
			maxY = pv->elements[1];
		}
		if( pv->elements[2] < minZ )
		{
			minZ = pv->elements[2];
		}
		if( pv->elements[2] > maxZ )
		{
			maxZ = pv->elements[2];
		}
	}

	m_boundingBox.minX = minX;
	m_boundingBox.maxX = maxX;
	m_boundingBox.minY = minY;
	m_boundingBox.maxY = maxY;
	m_boundingBox.minZ = minZ;
	m_boundingBox.maxZ = maxZ;
}

BoundingBox3f* TriangleMesh3f::getBoundingBox()
{
	return( &m_boundingBox );
}

void TriangleMesh3f::drawImmediate()
{
	glBegin( GL_TRIANGLES );

	for( uint i = 0; i < m_vVertices.size(); ++i )
	{
		Vector3f* v = &( m_vVertices[i] );
		glVertex3fv( v->elements );
	}
}

void TriangleMesh3f::drawVertexBufferObject()
{
	glEnableClientState( GL_VERTEX_ARRAY );
	m_pVertexBuffer->bind();
	glVertexPointer( 3, GL_FLOAT, 0, 0 );

	if( !( m_vNormals.empty() ) )
	{
		glEnableClientState( GL_NORMAL_ARRAY );
		m_pNormalBuffer->bind();
		glNormalPointer( GL_FLOAT, 0, 0 );
	}

	if( !( m_vTextureCoordinates.empty() ) )
	{
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		m_pTextureCoordinateBuffer->bind();
		glTexCoordPointer( 2, GL_FLOAT,	0, 0 );
	}

	glDrawArrays( GL_TRIANGLES, 0, m_pVertexBuffer->getCount() );
}

TriangleMesh3f::TriangleMesh3f() :
m_pVertexBuffer( NULL ),
m_pTextureCoordinateBuffer( NULL ),
m_pNormalBuffer( NULL )
{

}

#if 0

#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

// #include <NvTriStrip/NvTriStrip.h>
// #include <tristripper/tri_stripper.h>

#include "TriangleMesh3f.h"

#include "TriangleMesh3fHalfEdge.h"
#include "TriangleMesh3fFace.h"
#include "TriangleMesh3fVertex.h"
#include "IndexedFace.h"

using namespace std;
// using namespace triangle_stripper;

// static
TriangleMesh3f* TriangleMesh3f::create( vector< Vector3f >* pvVertices,
									   vector< Vector2f >* pvTextureCoordinates,
									   vector< Vector3f >* pvNormals,
									   vector< IndexedFace >* pvIndexedFaces )
{
	TriangleMesh3f* pMesh = new TriangleMesh3f;
	if( pMesh != NULL )
	{
		if( pvIndexedFaces->at( 0 ).getTextureCoordinateIndices()->size() != 0 )
		{
			pMesh->m_bHasTextureCoordinates = true;
		}
		else
		{
			pMesh->m_bHasTextureCoordinates = false;
		}

		if( pvIndexedFaces->at( 0 ).getNormalIndices()->size() != 0 )
		{
			pMesh->m_bHasNormals = true;
		}
		else
		{
			pMesh->m_bHasNormals = false;
		}

		const size_t numVertices = pvVertices->size();
		vector< TriangleMesh3fVertex* > vertexArray( numVertices, NULL );
		// TODO: hashmap
		map< int, TriangleMesh3fHalfEdge* > edgeMatrix;

		for( uint i = 0; i < pvIndexedFaces->size(); ++i )
		{
			vector< int >* pVertexIndices = pvIndexedFaces->at( i ).getVertexIndices();
			vector< int >* pTextureCoordinateIndices = NULL;
			vector< int >* pNormalIndices = NULL;

			if( pMesh->m_bHasTextureCoordinates )
			{
				pTextureCoordinateIndices = pvIndexedFaces->at( i ).getTextureCoordinateIndices();
			}
			if( pMesh->m_bHasNormals )
			{
				pNormalIndices = pvIndexedFaces->at( i ).getNormalIndices();
			}

			const size_t numVerticesInFace = pVertexIndices->size();

			// create vertices if they don't already exist
			for( uint j = 0; j < numVerticesInFace; ++j )
			{
				TriangleMesh3fVertex* pVertex = NULL;

				int currentVertexIndex = pVertexIndices->at( j );
				int currentTextureCoordinateIndex = -1;
				int currentNormalIndex = -1;

				if( pMesh->m_bHasTextureCoordinates )
				{
					currentTextureCoordinateIndex = pTextureCoordinateIndices->at( j );
				}
				if( pMesh->m_bHasNormals )
				{
					currentNormalIndex = pNormalIndices->at( j );
				}

				if( vertexArray[ currentVertexIndex ] == NULL )
				{
					Vector3f* pvPosition = &( pvVertices->at( currentVertexIndex ) );
					Vector2f* pvTextureCoordinate = NULL;
					Vector3f* pvNormal = NULL;

					if( pMesh->m_bHasTextureCoordinates )
					{
						pvTextureCoordinate = &( pvTextureCoordinates->at( currentTextureCoordinateIndex ) );
					}
					if( pMesh->m_bHasNormals )
					{
						pvNormal = &( pvNormals->at( currentNormalIndex ) );
					}

					vertexArray[ currentVertexIndex ] = new TriangleMesh3fVertex
						(
						pvPosition,
						pvTextureCoordinate,
						pvNormal
						);
				}
				pVertex = vertexArray[ currentVertexIndex ];
			}

			// triangulate face
			// add TriangleMesh3fHalfEdges if they don't already exist
			int vertexIndex0 = pVertexIndices->at( 0 );

			for( uint j = 1; j < numVerticesInFace - 1; ++j )
			{
				int vertexIndex1 = pVertexIndices->at( j );
				int vertexIndex2 = pVertexIndices->at( ( j + 1 ) % numVerticesInFace );

				size_t edgeIndex0 = vertexIndex1 * numVertices + vertexIndex0; // 0 --> 1
				size_t edgeIndex1 = vertexIndex2 * numVertices + vertexIndex1; // 1 --> 2
				size_t edgeIndex2 = vertexIndex0 * numVertices + vertexIndex2; // 2 --> 0

				size_t twinIndex0 = vertexIndex0 * numVertices + vertexIndex1; // 1 --> 0
				size_t twinIndex1 = vertexIndex1 * numVertices + vertexIndex2; // 2 --> 1
				size_t twinIndex2 = vertexIndex2 * numVertices + vertexIndex0; // 0 --> 2

				TriangleMesh3fHalfEdge* triangleEdges[3];
				TriangleMesh3fHalfEdge* twinEdges[3];

				triangleEdges[0] = edgeMatrix[ edgeIndex0 ];
				triangleEdges[1] = edgeMatrix[ edgeIndex1 ];
				triangleEdges[2] = edgeMatrix[ edgeIndex2 ];

				twinEdges[0] = edgeMatrix[ twinIndex0 ];
				twinEdges[1] = edgeMatrix[ twinIndex1 ];
				twinEdges[2] = edgeMatrix[ twinIndex2 ];

				if( triangleEdges[0] == NULL )
				{
					triangleEdges[0] = new TriangleMesh3fHalfEdge;
					edgeMatrix[ edgeIndex0 ] = triangleEdges[0];
				}
				triangleEdges[0]->setDestinationVertex( vertexArray[ vertexIndex1 ] );

				if( triangleEdges[1] == NULL )
				{
					triangleEdges[1] = new TriangleMesh3fHalfEdge;
					edgeMatrix[ edgeIndex1 ] = triangleEdges[1];					
				}
				triangleEdges[1]->setDestinationVertex( vertexArray[ vertexIndex2 ] );

				if( triangleEdges[2] == NULL )
				{
					triangleEdges[2] = new TriangleMesh3fHalfEdge;
					edgeMatrix[ edgeIndex2 ] = triangleEdges[2];					
				}
				triangleEdges[2]->setDestinationVertex( vertexArray[ vertexIndex0 ] );

				TriangleMesh3fFace* pFace = new TriangleMesh3fFace( triangleEdges[0] );
				triangleEdges[0]->setPrevAndNext( triangleEdges[2], triangleEdges[1] );
				triangleEdges[1]->setPrevAndNext( triangleEdges[0], triangleEdges[2] );
				triangleEdges[2]->setPrevAndNext( triangleEdges[1], triangleEdges[0] );		

				triangleEdges[0]->setIncidentFace( pFace );
				triangleEdges[1]->setIncidentFace( pFace );
				triangleEdges[2]->setIncidentFace( pFace );

				if( twinEdges[0] == NULL )
				{
					twinEdges[0] = new TriangleMesh3fHalfEdge;
					edgeMatrix[ twinIndex0 ] = twinEdges[0];
				}
				TriangleMesh3fHalfEdge::setTwins( triangleEdges[0], twinEdges[0] );

				if( twinEdges[1] == NULL )
				{
					twinEdges[1] = new TriangleMesh3fHalfEdge;
					edgeMatrix[ twinIndex1 ] = twinEdges[1];
				}
				TriangleMesh3fHalfEdge::setTwins( triangleEdges[1], twinEdges[1] );

				if( twinEdges[2] == NULL )
				{
					twinEdges[2] = new TriangleMesh3fHalfEdge;
					edgeMatrix[ twinIndex2 ] = twinEdges[2];
				}
				TriangleMesh3fHalfEdge::setTwins( triangleEdges[2], twinEdges[2] );

				pMesh->m_vFaces.push_back( pFace );
			}
		}

		for( uint i = 0; i < vertexArray.size(); ++i )
		{
			if( vertexArray[i] != NULL )
			{
				pMesh->m_vVertices.push_back( vertexArray[i] );
			}
		}

		for( uint i = 0; i < pMesh->m_vVertices.size(); ++i )
		{
			( pMesh->m_vVertices )[i]->setArrayIndex( i );
		}

		int numFaces = pMesh->m_vFaces.size();

		pMesh->recomputeBoundingBox();
		// TODO:
		// pMesh->recomputeTriangleStrips();		
	}

	return pMesh;
}

#if 0
TriangleMesh3f* TriangleMesh3f::create( vector< Vector3f >* pvVertices,
									   vector< Vector2f >* pvTextureCoordinates,
									   vector< Vector3f >* pvNormals,									 
									   vector< IndexedFace >* pvIndexedFaces )
{
	TriangleMesh3f* pMesh = new TriangleMesh3f;
	pMesh->getVertices()->reserve( pvVertices->size() );
	pMesh->getTextureCoordinates()->reserve( pvTextureCoordinates->size() );
	pMesh->getNormals()->reserve( pvNormals->size() );

	if( pMesh != NULL )
	{
		IndexedTriangleMesh3fFace* pFace = &( pvIndexedFaces->at( 0 ) );
		if( pFace->getTextureCoordinateIndices()->size() != 0 )
		{
			pMesh->m_bHasTextureCoordinates = true;
		}
		if( pFace->getNormalIndices()->size() != 0 )
		{
			// if no normals, generate them
			pMesh->m_bHasNormals = true;
		}

		for( uint i = 0; i < pvIndexedFaces->size(); ++i )
		{
			IndexedTriangleMesh3fFace* pFace = &( pvIndexedFaces->at( i ) );

			// TODO: stripify to draw probably
			vector< int >* pvVertexIndices = pFace->getVertexIndices();

			Vector3f v0 = pvVertices->at( pvVertexIndices->at( 0 ) );

			// triangulate
			for( uint j = 1; j < pvVertexIndices->size() - 1; ++j )
			{
				Vector3f v1 = pvVertices->at( pvVertexIndices->at( j ) );
				Vector3f v2 = pvVertices->at( pvVertexIndices->at( j + 1 ) );

				pMesh->m_vVertices.push_back( v0 );
				pMesh->m_vVertices.push_back( v1 );
				pMesh->m_vVertices.push_back( v2 );
			}

			if( pMesh->m_bHasTextureCoordinates )
			{
				vector< int >* pvTextureCoordinateIndices = pFace->getTextureCoordinateIndices();

				Vector2f t0 = pvTextureCoordinates->at( pvTextureCoordinateIndices->at( 0 ) );

				// triangulate
				for( uint k = 1; k < pvTextureCoordinateIndices->size() - 1; ++k )
				{
					Vector2f t1 = pvTextureCoordinates->at( pvTextureCoordinateIndices->at( k ) );
					Vector2f t2 = pvTextureCoordinates->at( pvTextureCoordinateIndices->at( k + 1 ) );

					pMesh->m_vTextureCoordinates.push_back( t0 );
					pMesh->m_vTextureCoordinates.push_back( t1 );
					pMesh->m_vTextureCoordinates.push_back( t2 );
				}
			}

			if( pMesh->m_bHasNormals )
			{
				vector< int >* pvNormalIndices = pFace->getNormalIndices();

				Vector3f n0 = pvNormals->at( pvNormalIndices->at( 0 ) );

				// triangulate
				for( uint l = 0; l < pvNormalIndices->size() - 1; ++l )
				{
					Vector3f n1 = pvNormals->at( pvNormalIndices->at( l ) );
					Vector3f n2 = pvNormals->at( pvNormalIndices->at( l + 1 ) );

					pMesh->m_vNormals.push_back( n0 );
					pMesh->m_vNormals.push_back( n1 );
					pMesh->m_vNormals.push_back( n2 );
				}
			}
			else
			{
				// triangulate
				Vector3f v0 = pvVertices->at( pvVertexIndices->at( 0 ) );

				for( uint l = 1; l < pvVertexIndices->size() - 1; ++l )
				{
					Vector3f v1 = pvVertices->at( pvVertexIndices->at( l ) );
					Vector3f v2 = pvVertices->at( pvVertexIndices->at( l + 1 ) );

					Vector3f v0v1;
					Vector3f v0v2;
					Vector3f normal;

					Vector3f::subtract( &v1, &v0, &v0v1 );
					Vector3f::subtract( &v2, &v0, &v0v2 );
					Vector3f::cross( &v0v1, &v0v2, &normal );

					pMesh->m_vNormals.push_back( normal );
					pMesh->m_vNormals.push_back( normal );
					pMesh->m_vNormals.push_back( normal );
				}
			}

			/*
			if( vertexIndices->size() == 3 )
			{
			for( uint j = 0; j < 3; ++j )
			{					
			pMesh->m_vVertices.push_back( pvVertices->at( vertexIndices->at( j ) ) );
			}

			if( pMesh->m_bHasTextureCoordinates )
			{
			vector< int >* textureCoordinateIndices = pFace->getTextureCoordinateIndices();
			if( textureCoordinateIndices != NULL )
			{
			if( textureCoordinateIndices->size() == 3 )
			{
			for( uint k = 0; k < 3; ++k )
			{
			pMesh->m_vTextureCoordinates.push_back( pvTextureCoordinates->at( textureCoordinateIndices->at( k ) ) );
			}
			}
			else
			{							
			// fprintf( stderr, "Warning: Triangular face without 3 texture coordinates, assigning index (0,0)\n" );
			for( uint k = 0; k < 3; ++k )
			{
			pMesh->m_vTextureCoordinates.push_back( Vector2f( 0, 0 ) );
			}
			}
			}
			}

			if( pMesh->m_bHasNormals )
			{
			vector< int >* normalIndices = pFace->getNormalIndices();
			if( normalIndices != NULL )
			{
			if( normalIndices->size() == 3 )
			{
			for( uint k = 0; k < 3; ++k )
			{
			pMesh->m_vNormals.push_back( pvNormals->at( normalIndices->at( k ) ) );
			}
			}
			else
			{
			// fprintf( stderr, "Warning: Triangular face without 3 normals, assigning (0, 0, 0)\n" );
			for( uint k = 0; k < 3; ++k )
			{
			pMesh->m_vNormals.push_back( Vector3f( 0, 0, 0 ) );
			}
			}
			}
			}
			else
			{
			// generate the normals
			Vector3f* v0 = &( pvVertices->at( pFace->getVertexIndices()->at( 0 ) ) );
			Vector3f* v1 = &( pvVertices->at( pFace->getVertexIndices()->at( 1 ) ) );
			Vector3f* v2 = &( pvVertices->at( pFace->getVertexIndices()->at( 2 ) ) );

			Vector3f v0v1;
			Vector3f v0v2;
			Vector3f normal;

			Vector3f::subtract( v1, v0, &v0v1 );
			Vector3f::subtract( v2, v0, &v0v2 );
			Vector3f::cross( &v0v1, &v0v2, &normal );

			pMesh->m_vNormals.push_back( normal );
			pMesh->m_vNormals.push_back( normal );
			pMesh->m_vNormals.push_back( normal );
			}
			}
			else
			{
			// TODO: fprintfs here and in OBJLoader, what to do?
			// fprintf( stderr, "Warning: Non-triangular face detected, ignoring face\n" );
			}
			*/
		}

		// now create the VBO
		pMesh->m_pVertexBuffer = GLBufferObject::fromVector3fVector( &( pMesh->m_vVertices ) );
		pMesh->m_bHasNormals = true; // TODO: think of an elegant way here
		pMesh->m_pNormalBuffer = GLBufferObject::fromVector3fVector( &( pMesh->m_vNormals ) );

		if( pMesh->m_bHasTextureCoordinates )
		{
			pMesh->m_pTextureCoordinateBuffer = GLBufferObject::fromVector2fVector( &( pMesh->m_vTextureCoordinates ) );
		}
	}

	return pMesh;
}
#endif

bool TriangleMesh3f::checkIntegrity()
{
	TriangleMesh3fFace* currentFace;
	TriangleMesh3fHalfEdge* e0;
	TriangleMesh3fHalfEdge* e1;
	TriangleMesh3fHalfEdge* e2;
	TriangleMesh3fHalfEdge* t0;
	TriangleMesh3fHalfEdge* t1;
	TriangleMesh3fHalfEdge* t2;

	printf( "================================\n" );

	// assume triangles
	for( uint i = 0; i < m_vFaces.size(); ++i )	
	{
		printf( "Checking face: %d\n", i );

		currentFace = m_vFaces[i];
		if( currentFace == NULL )
		{
			printf( "Face %d is NULL\n", i );
			return false;
		}

		e0 = currentFace->getInitialEdge();
		if( e0 == NULL )
		{
			printf( "e0 is NULL\n" );
			return false;
		}
		// e0->print();

		e1 = e0->getNextEdge();
		if( e1 == NULL )
		{
			printf( "e1 is NULL\n" );
			return false;
		}
		// e1->print();

		e2 = e1->getNextEdge();
		if( e2 == NULL )
		{
			printf( "e2 is NULL\n" );
			return false;
		}
		// e2->print();

		t0 = e0->getTwin();
		if( t0 == NULL )
		{
			printf( "t0 is NULL\n" );
			return false;
		}

		t1 = e1->getTwin();
		if( t1 == NULL )
		{
			printf( "t1 is NULL\n" );
			return false;
		}

		t2 = e2->getTwin();
		if( t2 == NULL )
		{
			printf( "t2 is NULL\n" );
			return false;
		}

		if( t0->getTwin() != e0 )
		{
			printf( "e0 <--> t0 do not match\n" );
			return false;
		}

		if( t1->getTwin() != e1 )
		{
			printf( "e1 <--> t1 do not match\n" );
			return false;
		}

		if( t2->getTwin() != e2 )
		{
			printf( "e2 <--> t2 do not match\n" );
			return false;
		}

		if( e2->getNextEdge() != e0 )
		{
			printf( "e2->next != e0\ne2->next =\n" );
			e2->getNextEdge()->print();
			return false;
		}

		if( e0->getIncidentFace() != currentFace )
		{
			printf( "e0->face != this face\n" );
			return false;
		}

		if( e1->getIncidentFace() != currentFace )
		{
			printf( "e1->face != this face\n" );
			return false;
		}

		if( e2->getIncidentFace() != currentFace )
		{
			printf( "e2->face != this face\n" );
			return false;
		}
	}

	return true;
}

void TriangleMesh3f::drawImmediate()
{
	glBegin( GL_TRIANGLES );

	for( uint i = 0; i < m_vFaces.size(); ++i )
	{
		TriangleMesh3fFace* pFace = m_vFaces[i];
		TriangleMesh3fHalfEdge* e0 = pFace->getInitialEdge();
		TriangleMesh3fHalfEdge* e1 = e0->getNextEdge();
		TriangleMesh3fHalfEdge* e2 = e1->getNextEdge();

		Vector3f* v0 = e0->getDestinationVertex()->getPosition();
		Vector3f* v1 = e1->getDestinationVertex()->getPosition();
		Vector3f* v2 = e2->getDestinationVertex()->getPosition();

		Vector3f faceNormal;
		pFace->getNormal( &faceNormal );
		faceNormal.normalize();

		glNormal3fv( faceNormal );
		glVertex3fv( v0->elements );
		glVertex3fv( v1->elements );
		glVertex3fv( v2->elements );
	}

	glEnd();
}

/*
void TriangleMesh3f::recomputeTriangleStrips()
{
uint* vertexIndices = new uint[ m_vFaces.size() * 3 ];	

for( uint i = 0; i < m_vFaces.size(); ++i )
{
TriangleMesh3fFace* pFace = m_vFaces[i];
TriangleMesh3fHalfEdge* e0 = pFace->getInitialEdge();
TriangleMesh3fHalfEdge* e1 = e0->getNextEdge();
TriangleMesh3fHalfEdge* e2 = e1->getNextEdge();

TriangleMesh3fVertex* v0 = e0->getDestinationVertex();
TriangleMesh3fVertex* v1 = e1->getDestinationVertex();
TriangleMesh3fVertex* v2 = e2->getDestinationVertex();

vertexIndices[ 3 * i ] = v0->getArrayIndex();
vertexIndices[ 3 * i + 1 ] = v1->getArrayIndex();
vertexIndices[ 3 * i + 2 ] = v2->getArrayIndex();		
}

PrimitiveGroup* primitiveGroups;
uint numPrimitiveGroups;

GenerateStrips( vertexIndices, m_vFaces.size() * 3,
&primitiveGroups, &numPrimitiveGroups );

assert( numPrimitiveGroups == 1 );
assert( primitiveGroups[0].type == PT_STRIP );

GLBufferObject::fromNvTriStrip( primitiveGroups[0].indices,
primitiveGroups[0].numIndices, &m_vVertices,
m_bHasTextureCoordinates, m_bHasNormals,
&m_pVertexBuffer, &m_pTextureCoordinateBuffer, &m_pNormalBuffer );

delete[] primitiveGroups;
delete[] vertexIndices;
}
*/

void TriangleMesh3f::recomputeTriangleStrips()
{
	/*
	tri_stripper::indices triangleIndices;

	// uint* vertexIndices = new uint[ m_vFaces.size() * 3 ];	

	for( uint i = 0; i < m_vFaces.size(); ++i )
	{
	TriangleMesh3fFace* pFace = m_vFaces[i];
	TriangleMesh3fHalfEdge* e0 = pFace->getInitialEdge();
	TriangleMesh3fHalfEdge* e1 = e0->getNextEdge();
	TriangleMesh3fHalfEdge* e2 = e1->getNextEdge();

	TriangleMesh3fVertex* v0 = e0->getDestinationVertex();
	TriangleMesh3fVertex* v1 = e1->getDestinationVertex();
	TriangleMesh3fVertex* v2 = e2->getDestinationVertex();

	triangleIndices.push_back( v0->getArrayIndex() );
	triangleIndices.push_back( v1->getArrayIndex() );
	triangleIndices.push_back( v2->getArrayIndex() );
	}

	tri_stripper::primitives_vector primitivesVector;
	tri_stripper ts( triangleIndices );
	ts.Strip( &primitivesVector );
	*/
}

void TriangleMesh3f::recomputeShadowVolume( Vector3f* pvLightPosition, ShadowVolume* pSV )
{
	pSV->clear();

	vector< Vector4f >* pvSideQuads = pSV->getSideQuads();
	vector< TriangleMesh3fFace* >* pvLightCap = pSV->getLightCap();
	vector< Vector4f >* pvDarkCap = pSV->getDarkCap();

	for( uint i = 0; i < m_vFaces.size(); ++i )
	{
		TriangleMesh3fFace* pFace = m_vFaces[i];

		TriangleMesh3fHalfEdge* e0 = pFace->getInitialEdge();
		TriangleMesh3fHalfEdge* e1 = e0->getNextEdge();
		TriangleMesh3fHalfEdge* e2 = e1->getNextEdge();

		Vector3f* v0 = e0->getDestinationVertex()->getPosition();
		Vector3f* v1 = e1->getDestinationVertex()->getPosition();
		Vector3f* v2 = e2->getDestinationVertex()->getPosition();

		if( pFace->isFrontFacing( pvLightPosition ) )
		{
			pvLightCap->push_back( pFace );
		}
		else
		{
			// extrude dark cap
			Vector3f temp;

			Vector3f::subtract( v0, pvLightPosition, &temp );
			pvDarkCap->push_back( Vector4f( &temp, 0 ) );

			Vector3f::subtract( v2, pvLightPosition, &temp );
			pvDarkCap->push_back( Vector4f( &temp, 0 ) );

			Vector3f::subtract( v1, pvLightPosition, &temp );
			pvDarkCap->push_back( Vector4f( &temp, 0 ) );			
		}
	}

	for( uint i = 0; i < pvLightCap->size(); ++i )
	{
		TriangleMesh3fFace* pFace = pvLightCap->at( i );

		TriangleMesh3fHalfEdge* e0 = pFace->getInitialEdge();
		TriangleMesh3fHalfEdge* e1 = e0->getNextEdge();
		TriangleMesh3fHalfEdge* e2 = e1->getNextEdge();

		TriangleMesh3fFace* f0 = e0->getAdjacentFace();
		TriangleMesh3fFace* f1 = e1->getAdjacentFace();
		TriangleMesh3fFace* f2 = e2->getAdjacentFace();

		Vector4f v0;
		Vector4f v1;
		Vector4f v2;
		Vector4f v3;
		Vector3f temp;

		if( !( f0->isFrontFacing( pvLightPosition ) ) )
		{
			v0.set( e0->getOriginVertex()->getPosition(), 1 );

			Vector3f::subtract( e0->getOriginVertex()->getPosition(), pvLightPosition, &temp );
			v1.set( &temp, 0 );

			Vector3f::subtract( e0->getDestinationVertex()->getPosition(), pvLightPosition, &temp );
			v2.set( &temp, 0 );

			v3.set( e0->getDestinationVertex()->getPosition(), 1 );

			pvSideQuads->push_back( v0 );
			pvSideQuads->push_back( v1 );
			pvSideQuads->push_back( v2 );
			pvSideQuads->push_back( v3 );
		}
		if( !( f1->isFrontFacing( pvLightPosition ) ) )
		{
			v0.set( e1->getOriginVertex()->getPosition(), 1 );

			Vector3f::subtract( e1->getOriginVertex()->getPosition(), pvLightPosition, &temp );
			v1.set( &temp, 0 );

			Vector3f::subtract( e1->getDestinationVertex()->getPosition(), pvLightPosition, &temp );
			v2.set( &temp, 0 );

			v3.set( e1->getDestinationVertex()->getPosition(), 1 );

			pvSideQuads->push_back( v0 );
			pvSideQuads->push_back( v1 );
			pvSideQuads->push_back( v2 );
			pvSideQuads->push_back( v3 );
		}
		if( !( f2->isFrontFacing( pvLightPosition ) ) )
		{
			v0.set( e2->getOriginVertex()->getPosition(), 1 );

			Vector3f::subtract( e2->getOriginVertex()->getPosition(), pvLightPosition, &temp );
			v1.set( &temp, 0 );

			Vector3f::subtract( e2->getDestinationVertex()->getPosition(), pvLightPosition, &temp );
			v2.set( &temp, 0 );

			v3.set( e2->getDestinationVertex()->getPosition(), 1 );

			pvSideQuads->push_back( v0 );
			pvSideQuads->push_back( v1 );
			pvSideQuads->push_back( v2 );
			pvSideQuads->push_back( v3 );
		}
	}
}

vector< TriangleMesh3fFace* >* TriangleMesh3f::getFaces()
{
	return &m_vFaces;
}

vector< TriangleMesh3fVertex* >* TriangleMesh3f::getVertices()
{
	return &m_vVertices;
}

#if 0
vector< Vector2f >* TriangleMesh3f::getTextureCoordinates()
{
	return &m_vTextureCoordinates;
}

vector< Vector3f >* TriangleMesh3f::getNormals()
{
	return &m_vNormals;
}
#endif

#endif

#endif