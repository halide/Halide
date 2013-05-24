#ifndef MESH_HALF_EDGE_2F_H
#define MESH_HALF_EDGE_2F_H

#include <QString>
#include "Mesh2f.h"

class MeshHalfEdge2f
{
public:

	MeshHalfEdge2f( int sourceVertexIndex = -1, int destinationVertexIndex = -1 );
	MeshHalfEdge2f( MeshHalfEdge2f* pCopy );

	QString toString();

	MeshEdgeKey key();
	void setKey( int sourceVertexIndex, int destinationVertexIndex );
	void setKey( MeshEdgeKey newKey );

	bool isBoundary();

	MeshVertex2f* destinationVertex();
	void setDestinationVertex( MeshVertex2f* pv );

	MeshHalfEdge2f* nextHalfEdgeOnFace();
	void setNextHalfEdgeOnFace( MeshHalfEdge2f* pe );

	MeshHalfEdge2f* previousHalfEdgeOnFace();
	void setPreviousHalfEdgeOnFace( MeshHalfEdge2f* pe );

	MeshHalfEdge2f* twinHalfEdge();
	void setTwinHalfEdge( MeshHalfEdge2f* pe );

	MeshFace2f* incidentFace(); // the face to my left, going counterclockwise
	void setIncidentFace( MeshFace2f* pFace );

	MeshVertex2f* sourceVertex(); // twinHalfEdge()->destinationVertex()
	MeshFace2f* adjacentFace(); // twinHalfEdge()->incidentFace()

	// calls { next / prev } HalfEdgeOnFace n times
	// n must be >= 0
	MeshHalfEdge2f* nextNthHalfEdgeOnFace( int n );
	MeshHalfEdge2f* previousNthHalfEdgeOnFace( int n );

private:

	MeshEdgeKey m_key;

	MeshVertex2f* m_pDestinationVertex;
	MeshHalfEdge2f* m_pTwinHalfEdge;
	MeshHalfEdge2f* m_pPreviousHalfEdge;
	MeshHalfEdge2f* m_pNextHalfEdge;
	MeshFace2f* m_pIncidentFace;

};

#endif // MESH_HALF_EDGE_2F_H
