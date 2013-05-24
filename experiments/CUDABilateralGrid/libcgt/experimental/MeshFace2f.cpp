#include "MeshFace2f.h"

#include <cassert>
#include <cfloat>
#include <geometry/BoundingBox2f.h>
#include <geometry/GeometryUtils.h>

#include "MeshHalfEdge2f.h"
#include "MeshVertex2f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

MeshFace2f::MeshFace2f( int index, MeshHalfEdge2f* pInitialEdge ) :

	m_iIndex( index ),
	m_pInitialEdge( pInitialEdge )

{

}

MeshFace2f::MeshFace2f( MeshFace2f* pCopy ) :

	m_iIndex( pCopy->m_iIndex ),
	m_pInitialEdge( pCopy->m_pInitialEdge )

{

}

QString MeshFace2f::toString()
{
	QString str = QString( "Face %1 = [ " ).arg( index() );

	QVector< MeshVertex2f* > qvVertices = vertices();
	foreach( MeshVertex2f* vertex, qvVertices )
	{
		str += QString( "%1 " ).arg( vertex->index() );
	}
	str += QString( "]" );

	return str;
}

int MeshFace2f::index()
{
	return m_iIndex;
}

void MeshFace2f::setIndex( int newIndex )
{
	m_iIndex = newIndex;
}

MeshHalfEdge2f* MeshFace2f::initialEdge()
{
	return m_pInitialEdge;
}

void MeshFace2f::setInitialEdge( MeshHalfEdge2f* pe )
{
	m_pInitialEdge = pe;
}

QVector< MeshHalfEdge2f* > MeshFace2f::edges()
{
	QVector< MeshHalfEdge2f* > qvEdges;
	qvEdges.reserve( 6 );

	MeshHalfEdge2f* e0 = initialEdge();
	qvEdges.append( e0 );

	MeshHalfEdge2f* e = e0->nextHalfEdgeOnFace();
	while( e != e0 )
	{
		qvEdges.append( e );
		e = e->nextHalfEdgeOnFace();
	}

	return qvEdges;
}

QVector< MeshVertex2f* > MeshFace2f::vertices()
{
	QVector< MeshHalfEdge2f* > qvEdges = edges();
	QVector< MeshVertex2f* > qvVertices;

	int nEdges = qvEdges.size();
	for( int i = 0; i < nEdges; ++i )
	{
		qvVertices.append( qvEdges[ i ]->destinationVertex() );
	}

	return qvVertices;
}

#if 0
QVector< MeshHalfEdge2f* > MeshFace2f::incidentEdges()
{
	QVector< MeshHalfEdge2f* > edges;
	edges.reserve( 6 );

	
}
#endif

BoundingBox2f MeshFace2f::boundingBox()
{
	QVector< MeshVertex2f* > qvVertices = vertices();
	
	float minX = FLT_MAX;
	float minY = FLT_MAX;
	float maxX = FLT_MIN;
	float maxY = FLT_MIN;
	
	foreach( MeshVertex2f* vertex, qvVertices )
	{
		Vector2f position = vertex->position();
		if( position.x() < minX )
		{
			minX = position.x();
		}
		if( position.y() < minY )
		{
			minY = position.y();
		}

		if( position.x() > maxX )
		{
			maxX = position.x();
		}
		if( position.y() > maxY )
		{
			maxY = position.y();
		}
	}

	return BoundingBox2f( minX, minY, maxX, maxY );
}

Vector2f MeshFace2f::pointInsidePolygon()
{
	QVector< MeshVertex2f* > qvVertices = vertices();
	int nVertices = qvVertices.size();
	assert( nVertices == 3 );

	return GeometryUtils::triangleCentroid( qvVertices[ 0 ]->position(),
		qvVertices[ 1 ]->position(),
		qvVertices[ 2 ]->position() );
}
