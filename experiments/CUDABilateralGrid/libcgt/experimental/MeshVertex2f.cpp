#include "MeshVertex2f.h"

#include "MeshHalfEdge2f.h"

MeshVertex2f::MeshVertex2f( int index, const Vector2f& position ) :

	m_iIndex( index ),
	m_f2Position( position ),
	m_pOutgoingEdge( NULL ),

	m_predecessor( NULL ),
	m_successor( NULL )

{

}

MeshVertex2f::MeshVertex2f( MeshVertex2f* pCopy ) :

	m_iIndex( pCopy->m_iIndex ),
	m_f2Position( pCopy->m_f2Position ),
	m_pOutgoingEdge( pCopy->outgoingEdge() )

{
	
}

bool MeshVertex2f::isCorner()
{
	QVector< MeshHalfEdge2f* > incoming;
	QVector< MeshHalfEdge2f* > outgoing;
	int incomingIndex;
	int outgoingIndex;

	incidentEdges( &incoming, &outgoing, &incomingIndex, &outgoingIndex );

	return( incoming.size() == 1 && outgoing.size() == 1 );
}

int MeshVertex2f::index()
{
	return m_iIndex;
}

void MeshVertex2f::setIndex( int newIndex )
{
	m_iIndex = newIndex;
}

Vector2f MeshVertex2f::position()
{
	return m_f2Position;
}

void MeshVertex2f::setPosition( const Vector2f& p )
{
	m_f2Position = p;
}

MeshHalfEdge2f* MeshVertex2f::outgoingEdge()
{
	return m_pOutgoingEdge;
}

void MeshVertex2f::setOutgoingEdge( MeshHalfEdge2f* e )
{
	m_pOutgoingEdge = e;
}

MeshVertex2f* MeshVertex2f::predecessor()
{
	return m_predecessor;
}

void MeshVertex2f::setPredecessor( MeshVertex2f* predecessor )
{
	m_predecessor = predecessor;
}

MeshVertex2f* MeshVertex2f::successor()
{
	return m_successor;
}

void MeshVertex2f::setSuccessor( MeshVertex2f* successor )
{
	m_successor = successor;
}

void MeshVertex2f::incidentEdges( QVector< MeshHalfEdge2f* >* pIncoming,
								 QVector< MeshHalfEdge2f* >* pOutgoing,
								 int* pIncomingBoundaryEdge,
								 int* pOutgoingBoundaryEdge )
{
	pIncoming->clear();
	pOutgoing->clear();
	*pIncomingBoundaryEdge = -1;
	*pOutgoingBoundaryEdge = -1;
	
	MeshHalfEdge2f* e0 = outgoingEdge();
	
	// otherwise vertex is orphaned
	if( e0 != NULL )
	{
		pOutgoing->append( e0 );

		// sweep in forward direction until
		// 1. we reach e0 again, in which case we're done, or
		// 2. we reach a boundary edge, in which case we switch in reverse direction
		MeshHalfEdge2f* e = e0;
		while( !( e->isBoundary() ) )
		{
			MeshHalfEdge2f* pTwin = e->twinHalfEdge();
			MeshHalfEdge2f* pNext = pTwin->nextHalfEdgeOnFace();

			pIncoming->append( pTwin );

			if( pNext == e0 )
			{
				break;
			}
			else
			{
				pOutgoing->append( pNext );
				e = pNext;
			}
		}

		// if we hit a boundary (either the initial edge is)
		// or we've swept forward and found one
		// start reverse sweep until hitting a boundary
		if( e->isBoundary() )
		{
			*pOutgoingBoundaryEdge = pOutgoing->size() - 1;

			e = e0->previousHalfEdgeOnFace();
			pIncoming->append( e );

			while( !( e->isBoundary() ) )
			{
				MeshHalfEdge2f* pTwin = e->twinHalfEdge();
				MeshHalfEdge2f* pPrev = pTwin->previousHalfEdgeOnFace();

				pOutgoing->append( pTwin );
				pIncoming->append( pPrev );

				e = pPrev;
			}

			*pIncomingBoundaryEdge = pIncoming->size() - 1;
		}
	}
}

QVector< MeshFace2f* > MeshVertex2f::incidentFaces()
{
	QVector< MeshHalfEdge2f* > incomingEdges;
	QVector< MeshHalfEdge2f* > outgoingEdges;
	int incomingBoundaryIndex;
	int outgoingBoundaryIndex;

	incidentEdges( &incomingEdges, &outgoingEdges, &incomingBoundaryIndex, &outgoingBoundaryIndex );

	QVector< MeshFace2f* > incidentFaces;
	incidentFaces.reserve( outgoingEdges.size() );

	foreach( MeshHalfEdge2f* outgoingEdge, outgoingEdges )
	{
		incidentFaces.append( outgoingEdge->incidentFace() );			 
	}

	return incidentFaces;
}
