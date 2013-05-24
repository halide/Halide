#ifndef MESH_2F_H
#define MESH_2F_H

#include <common/Reference.h>
#include <QHash>
#include <QPair>
#include <QVector>
#include <vecmath/Vector2f.h>
#include "MeshEdgeKey.h"

class MeshFace2f;
class MeshHalfEdge2f;
class MeshVertex2f;

class Mesh2f
{
public:

	// TODO: attach attributes to vertices
	// TODO: flatten into triangle list
	// TODO: construct from OBJData
	// TODO: currently assumes that every position is referenced
	//  in the future, just compact the indices beforehand
	//  also assumes that there are no duplicate faces or edges in the face list
	Mesh2f( QVector< Vector2f > positions, QVector< QVector< int > > faces );
	Mesh2f( Reference< Mesh2f > pCopy );

	bool checkIntegrity();

	void addVertexIntAttribute( QString name, QVector< int > values );
	int getVertexIntAttribute( QString name, MeshVertex2f* pVertex );
	void setVertexIntAttribute( QString name, MeshVertex2f* pVertex, int value );

	void addVertexFloatAttribute( QString name, QVector< float > values );
	int getVertexFloatAttribute( QString name, MeshVertex2f* pVertex );
	void setVertexFloatAttribute( QString name, MeshVertex2f* pVertex, float value );

	// compacts this mesh by:
	// 1. removing all NULL entries from the internal vertex and face lists
	// 2. reindexing all edges to their new indices
	//
	// returns two vectors which map the old index to the new index
	void compact( QVector< int >* oldVertexIndexToNewVertexIndex,
		QVector< int >* oldFaceIndexToNewFaceIndex );

	MeshFace2f* getFaceByIndex( int index );
	MeshVertex2f* getVertexByIndex( int index );
	MeshHalfEdge2f* getEdgeByIndex( int sourceVertexIndex, int destinationVertexIndex );

	int numVertices();
	int numFaces();

	QVector< MeshFace2f* > faces();
	QVector< MeshVertex2f* > vertices();
	QHash< MeshEdgeKey, MeshHalfEdge2f* > edges();
	QVector< Vector2f > holes();

	// deletes a vertex while maintaing topology as much as possible
	// 1. deleting an interior vertex deletes all the neighboring faces, incoming and outgoing edges
	//    and its one ring of neighbors become connected in a new face
	// 2. deleting a boundary vertex creates a new boundary along its neighbors
	// 3. deleting a vertex whose neighbor is a corner on a triangle will orphan the neighbor
	//    (if the neighbor's polygon has more than 3 sides, it will reattach)
	void deleteVertex( MeshVertex2f* pVertex );

	void deleteVertices( QVector< int > vertexIndices );
	void deleteFace( int faceIndex );
	void deleteFaces( QVector< int > faceIndices );

	QVector< QVector< Vector2f > > flatPolygonList();

	// returns the list of undirected edges
	QVector< MeshEdgeKey > segmentList();

private:

	int getNewVertexIndex();
	int getNewFaceIndex();

	// TODO: fix this?
	// slow, but useful for when the mesh is inconsistent...
	QVector< MeshHalfEdge2f* > outgoingEdgesSlow( MeshVertex2f* vertex );

	QVector< MeshVertex2f* > m_qvVertices;
	QVector< MeshFace2f* > m_qvFaces;
	QHash< MeshEdgeKey, MeshHalfEdge2f* > m_qhEdges;

	QVector< Vector2f > m_holes;

	QHash< QString, QVector< int > > m_qhVertexIntAttributes;
	QHash< QString, QVector< float > > m_qhVertexFloatAttributes;
};

#endif // MESH_2F_H
