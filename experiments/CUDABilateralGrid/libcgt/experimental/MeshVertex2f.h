#ifndef MESH_VERTEX_2F_H
#define MESH_VERTEX_2F_H

#include <QVector>
#include <vecmath/Vector2f.h>

class MeshFace2f;
class MeshHalfEdge2f;

class MeshVertex2f
{
public:

	MeshVertex2f( int index, const Vector2f& position );
	MeshVertex2f( MeshVertex2f* pCopy );

	// a vertex is a corner if it only has one incoming and one outgoing edge
	bool isCorner();

	int index();
	void setIndex( int newIndex );

	Vector2f position();
	void setPosition( const Vector2f& p );

	MeshVertex2f* predecessor();
	void setPredecessor( MeshVertex2f* predecessor );

	MeshVertex2f* successor();
	void setSuccessor( MeshVertex2f* successor );

	MeshHalfEdge2f* outgoingEdge();
	void setOutgoingEdge( MeshHalfEdge2f* e );

	// returns the list of incoming and outgoing edges
	// incident on this vertex
	// if this vertex is on a boundary,
	//     returns the indices of the incoming and outgoing boundary edges in the two vectors
	// else
	//     the indices are -1

	// TODO: figure out what to do with this
	// it's useful when the mesh is consistent
	void incidentEdges( QVector< MeshHalfEdge2f* >* pIncoming,
		QVector< MeshHalfEdge2f* >* pOutgoing,
		int* pIncomingBoundaryEdge,
		int* pOutgoingBoundaryEdge );

	QVector< MeshFace2f* > incidentFaces();

private:

	int m_iIndex;
	Vector2f m_f2Position;
	MeshHalfEdge2f* m_pOutgoingEdge;

	MeshVertex2f* m_predecessor;
	MeshVertex2f* m_successor;

};

#endif // MESH_VERTEX_2F_H
