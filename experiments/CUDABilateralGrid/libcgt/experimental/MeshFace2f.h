#ifndef MESH_FACE_2F_H
#define MESH_FACE_2F_H

#include <QString>
#include <QVector>
#include <geometry/BoundingBox2f.h>

class MeshHalfEdge2f;
class MeshVertex2f;

class MeshFace2f
{
public:

	MeshFace2f( int index, MeshHalfEdge2f* pInitialEdge );
	MeshFace2f( MeshFace2f* pCopy );

	QString toString();

	int index();
	void setIndex( int newIndex );

	// a single outgoing edge
	MeshHalfEdge2f* initialEdge();
	void setInitialEdge( MeshHalfEdge2f* pe );

	// utility
	QVector< MeshHalfEdge2f* > edges();
	QVector< MeshVertex2f* > vertices();
	// QVector< MeshHalfEdge2f* > MeshFace2f::incidentEdges()

	BoundingBox2f boundingBox();

	// TODO: make it work for non-triangles
	Vector2f pointInsidePolygon();

private:

	int m_iIndex;
	MeshHalfEdge2f* m_pInitialEdge;

};

#endif // MESH_FACE_2F_H
