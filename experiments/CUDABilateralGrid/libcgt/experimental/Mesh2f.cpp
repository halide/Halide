#include "Mesh2f.h"

#include <cassert>
#include <math/Arithmetic.h>
#include <QtDebug>

#include "MeshFace2f.h"
#include "MeshHalfEdge2f.h"
#include "MeshVertex2f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Mesh2f::Mesh2f( QVector< Vector2f > positions, QVector< QVector< int > > faces )
{
	// create vertex list
	int nVertices = positions.size();
	for( int i = 0; i < nVertices; ++i )
	{
		m_qvVertices.append( new MeshVertex2f( i, positions.at( i ) ) );
	}

	// create faces and set all pointers along each face
	int nFaces = faces.size();
	for( int f = 0; f < nFaces; ++f )
	{
		QVector< int > face = faces.at( f );

		// create its edges
		QVector< MeshHalfEdge2f* > newEdges;

		// create an edge for each vertex on the face
		// (nEdges = nVertices)
		int nFaceVertices = face.size();		
		int nEdges = nFaceVertices;
		
		newEdges.reserve( nEdges );
		for( int v = 0; v < nEdges; ++v )
		{
			MeshHalfEdge2f* pEdge = new MeshHalfEdge2f( -1, -1 );
			newEdges.append( pEdge );
		}

		// now connect the edges as its doubly linked list
		// and also set their destinations
		// and add each edge to the hash table so we can look it up by index

		// also set each vertex of the face
		// to set its initial outgoing edge to the new one
		for( int e = 0; e < nEdges; ++e )
		{			
			MeshHalfEdge2f* pe = newEdges[ e ];
			pe->setNextHalfEdgeOnFace( newEdges[ ( e + 1 ) % nEdges ] );
			pe->setPreviousHalfEdgeOnFace( newEdges[ Arithmetic::mod( e - 1, nEdges ) ] );

			int v0 = face[ e ];
			int v1 = face[ ( e + 1 ) % nFaceVertices ];

			pe->setDestinationVertex( m_qvVertices[ v1 ] );
			pe->setKey( v0, v1 );
			m_qvVertices[ v0 ]->setOutgoingEdge( pe );

			m_qhEdges[ qMakePair( v0, v1 ) ] = pe;
		}

		// create a new face and set edge 0 as the initial edge
		MeshFace2f* newFace = new MeshFace2f( f, newEdges[ 0 ] );
		m_qvFaces.append( newFace );

		// and set each edge to point to this face
		for( int e = 0; e < nEdges; ++e )
		{
			newEdges[ e ]->setIncidentFace( newFace );
		}
	}

	// now set twin edges
	foreach( MeshEdgeKey key, m_qhEdges.keys() )
	{
		MeshEdgeKey twinKey = qMakePair( key.second, key.first );
		if( m_qhEdges.contains( twinKey ) )
		{
			MeshHalfEdge2f* e0 = m_qhEdges[ key ];
			MeshHalfEdge2f* e1 = m_qhEdges[ twinKey ];

			e0->setTwinHalfEdge( e1 );
			// e1 will set e0 as twin when key becomes twinKey later
		}
	}
}

Mesh2f::Mesh2f( Reference< Mesh2f > pCopy ) :

	m_holes( pCopy->m_holes )

{
	int nVertices = pCopy->m_qvVertices.size();
	int nFaces = pCopy->m_qvFaces.size();
	int nEdges = pCopy->m_qhEdges.size();
	

	m_qvVertices.reserve( nVertices );
	m_qvFaces.reserve( nFaces );
	m_qhEdges.reserve( nEdges );

	for( int i = 0; i < nVertices; ++i )
	{
		MeshVertex2f* pVertex = NULL;
		if( pCopy->m_qvVertices[ i ] != NULL )
		{
			pVertex = new MeshVertex2f( pCopy->m_qvVertices[ i ] );
		}
		m_qvVertices.append( pVertex );
	}

	for( int i = 0; i < nFaces; ++i )
	{
		MeshFace2f* pFace = NULL;
		if( pCopy->m_qvFaces[ i ] != NULL )
		{
			pFace = new MeshFace2f( pCopy->m_qvFaces[ i ] );
		}
		m_qvFaces.append( pFace );
	}

	foreach( MeshEdgeKey key, pCopy->m_qhEdges.keys() )
	{
		MeshHalfEdge2f* pEdge = pCopy->m_qhEdges[ key ];
		
		assert( pEdge != NULL );
		m_qhEdges.insert( key, new MeshHalfEdge2f( pEdge ) );
	}

	for( int i = 0; i < nVertices; ++i )
	{
		MeshVertex2f* pVertex = m_qvVertices[ i ];

		if( pVertex != NULL )
		{			
			MeshHalfEdge2f* pOldEdge = pVertex->outgoingEdge();

			// if it's not an orphan
			if( pOldEdge != NULL )
			{
				MeshHalfEdge2f* pNewEdge = m_qhEdges[ pOldEdge->key() ];
				pVertex->setOutgoingEdge( pNewEdge );
			}
		}
	}

	for( int i = 0; i < nFaces; ++i )
	{
		MeshFace2f* pFace = m_qvFaces[ i ];
		if( pFace != NULL )
		{
			MeshHalfEdge2f* pOldEdge = pFace->initialEdge();
			MeshHalfEdge2f* pNewEdge = m_qhEdges[ pOldEdge->key() ];
			pFace->setInitialEdge( pNewEdge );
		}
	}

	foreach( MeshEdgeKey key, m_qhEdges.keys() )
	{
		MeshHalfEdge2f* pEdge = m_qhEdges[ key ];

		MeshVertex2f* pOldDestinationVertex = pEdge->destinationVertex();
		MeshFace2f* pOldIncidentFace = pEdge->incidentFace();
		MeshHalfEdge2f* pOldNextEdge = pEdge->nextHalfEdgeOnFace();
		MeshHalfEdge2f* pOldPrevEdge = pEdge->previousHalfEdgeOnFace();
		MeshHalfEdge2f* pOldTwinEdge = pEdge->twinHalfEdge();

		pEdge->setDestinationVertex( m_qvVertices[ pOldDestinationVertex->index() ] );
		pEdge->setIncidentFace( m_qvFaces[ pOldIncidentFace->index() ] );
		pEdge->setNextHalfEdgeOnFace( m_qhEdges[ pOldNextEdge->key() ] );
		pEdge->setPreviousHalfEdgeOnFace( m_qhEdges[ pOldPrevEdge->key() ] );

		if( pOldTwinEdge != NULL )
		{
			pEdge->setTwinHalfEdge( m_qhEdges[ pOldTwinEdge->key() ] );
		}
	}

	// copy attributes
	m_qhVertexIntAttributes = pCopy->m_qhVertexIntAttributes;
	m_qhVertexFloatAttributes = pCopy->m_qhVertexFloatAttributes;

	assert( checkIntegrity() );
}

bool Mesh2f::checkIntegrity()
{
	printf( "Checking that for each vertex v:\ne = (e0, e1), e0 == v and e1 < nVertices\n" );
	for( int i = 0; i < m_qvVertices.size(); ++i )
	{
		MeshVertex2f* pVertex = m_qvVertices[ i ];
		if( pVertex != NULL )
		{
			MeshHalfEdge2f* pEdge = pVertex->outgoingEdge();
			if( pEdge != NULL )
			{
				MeshEdgeKey key = pEdge->key();
				if( key.first != pVertex->index() ||
					key.second >= m_qvVertices.size() )
				{
					printf( "Vertex %d, e->key = (%d, %d)\n", i, key.first, key.second );
					return false;
				}
			}
		}
	}

	return true;
}

void Mesh2f::addVertexIntAttribute( QString name, QVector< int > values )
{
	assert( m_qvVertices.size() == values.size() );
	m_qhVertexIntAttributes[ name ] = values;
}

int Mesh2f::getVertexIntAttribute( QString name, MeshVertex2f* pVertex )
{
	assert( m_qhVertexIntAttributes.contains( name ) );
	return m_qhVertexIntAttributes[ name ][ pVertex->index() ];
}

void Mesh2f::setVertexIntAttribute( QString name, MeshVertex2f* pVertex, int value )
{
	assert( m_qhVertexIntAttributes.contains( name ) );
	m_qhVertexIntAttributes[ name ][ pVertex->index() ] = value;
}

void Mesh2f::addVertexFloatAttribute( QString name, QVector< float > values )
{
	assert( m_qvVertices.size() == values.size() );
	m_qhVertexFloatAttributes[ name ] = values;
}

int Mesh2f::getVertexFloatAttribute( QString name, MeshVertex2f* pVertex )
{
	assert( m_qhVertexFloatAttributes.contains( name ) );
	return m_qhVertexFloatAttributes[ name ][ pVertex->index() ];
}

void Mesh2f::setVertexFloatAttribute( QString name, MeshVertex2f* pVertex, float value )
{
	assert( m_qhVertexFloatAttributes.contains( name ) );
	m_qhVertexFloatAttributes[ name ][ pVertex->index() ] = value;
}

void Mesh2f::compact( QVector< int >* oldVertexIndexToNewVertexIndex,
					 QVector< int >* oldFaceIndexToNewFaceIndex )
{
	oldVertexIndexToNewVertexIndex->clear();
	oldFaceIndexToNewFaceIndex->clear();

	int nVertices = m_qvVertices.size();
	int nFaces = m_qvFaces.size();
	int nEdges = m_qhEdges.size();

	QVector< MeshVertex2f* > vertices;	
	vertices.reserve( nVertices );
	oldVertexIndexToNewVertexIndex->reserve( nVertices );

	QVector< MeshFace2f* > faces;
	faces.reserve( nFaces );
	oldFaceIndexToNewFaceIndex->reserve( nFaces );

	QHash< MeshEdgeKey, MeshHalfEdge2f* > edges;
	edges.reserve( nEdges );

	QHash< QString, QVector< int > > qhVertexIntAttributes;
	foreach( QString name, m_qhVertexIntAttributes.keys() )
	{
		qhVertexIntAttributes[ name ] = QVector< int >();
	}

	QHash< QString, QVector< float > > qhVertexFloatAttributes;
	foreach( QString name, m_qhVertexFloatAttributes.keys() )
	{
		qhVertexFloatAttributes[ name ] = QVector< float >();
	}	

	// compact vertices
	int nNonNullVertices = 0;
	for( int i = 0; i < nVertices; ++i )
	{
		MeshVertex2f* pVertex =  m_qvVertices[ i ];
		if( pVertex != NULL )
		{
			vertices.append( pVertex );
			oldVertexIndexToNewVertexIndex->append( nNonNullVertices );

			pVertex->setIndex( nNonNullVertices );
			++nNonNullVertices;

			foreach( QString name, m_qhVertexIntAttributes.keys() )
			{
				int value = m_qhVertexIntAttributes[ name ][ i ];
				qhVertexIntAttributes[ name ].append( value );
			}

			foreach( QString name, m_qhVertexFloatAttributes.keys() )
			{
				float value = m_qhVertexFloatAttributes[ name ][ i ];
				qhVertexFloatAttributes[ name ].append( value );
			}
		}
		else
		{
			oldVertexIndexToNewVertexIndex->append( -1 );
		}
	}

	// compact faces
	int nNonNullFaces = 0;
	for( int i = 0; i < nFaces; ++i )
	{
		MeshFace2f* pFace = m_qvFaces[ i ];
		if( pFace != NULL )
		{
			faces.append( pFace );
			oldFaceIndexToNewFaceIndex->append( nNonNullFaces );

			pFace->setIndex( nNonNullFaces );
			++nNonNullFaces;
		}
		else
		{
			oldFaceIndexToNewFaceIndex->append( -1 );
		}
	}

	foreach( MeshEdgeKey key, m_qhEdges.keys() )
	{
		MeshHalfEdge2f* pEdge = m_qhEdges[ key ];

		MeshEdgeKey newKey = qMakePair( oldVertexIndexToNewVertexIndex->at( key.first ), oldVertexIndexToNewVertexIndex->at( key.second ) );
		pEdge->setKey( newKey );

		edges.insert( newKey, pEdge );
	}
	
	// finally, copy over to member variables
	m_qvVertices = vertices;
	m_qvFaces = faces;
	m_qhEdges = edges;
}

MeshFace2f* Mesh2f::getFaceByIndex( int index )
{
	assert( index >= 0 );
	assert( index < m_qvFaces.size() );

	return m_qvFaces[ index ];
}

MeshVertex2f* Mesh2f::getVertexByIndex( int index )
{
	assert( index >= 0 );
	assert( index < m_qvVertices.size() );

	return m_qvVertices[ index ];
}

MeshHalfEdge2f* Mesh2f::getEdgeByIndex( int sourceVertexIndex, int destinationVertexIndex )
{
	MeshEdgeKey key = qMakePair( sourceVertexIndex, destinationVertexIndex );
	
	assert( m_qhEdges.contains( key ) );
	return m_qhEdges[ key ];
}

int Mesh2f::numVertices()
{
	return m_qvVertices.size();
}

int Mesh2f::numFaces()
{
	return m_qvFaces.size();
}

QVector< MeshFace2f* > Mesh2f::faces()
{
	return m_qvFaces;
}

QVector< MeshVertex2f* > Mesh2f::vertices()
{
	return m_qvVertices;
}

QHash< MeshEdgeKey, MeshHalfEdge2f* > Mesh2f::edges()
{
	return m_qhEdges;
}

QVector< Vector2f > Mesh2f::holes()
{
	return m_holes;
}

void Mesh2f::deleteVertex( MeshVertex2f* pVertex )
{
	QVector< MeshFace2f* > incidentFaces = pVertex->incidentFaces();

	foreach( MeshFace2f* face, incidentFaces )
	{
		QVector< MeshHalfEdge2f* > faceEdges = face->edges();
		
		foreach( MeshHalfEdge2f* edge, faceEdges )
		{
			m_qhEdges.remove( edge->key() );
			delete edge;
		}

		m_qvFaces[ face->index() ] = NULL;
		delete face;
	}

	m_qvVertices[ pVertex->index() ] = NULL;
	delete pVertex;
}

void Mesh2f::deleteVertices( QVector< int > vertexIndices )
{
	QHash< int, MeshFace2f* > facesToDelete;
	QHash< MeshEdgeKey, MeshHalfEdge2f* > edgesToDelete;

	foreach( int vertexIndex, vertexIndices )
	{
		MeshVertex2f* pVertex = getVertexByIndex( vertexIndex );
		
		foreach( MeshFace2f* incidentFace, pVertex->incidentFaces() )
		{
			facesToDelete[ incidentFace->index() ] = incidentFace;

			foreach( MeshHalfEdge2f* edge, incidentFace->edges() )
			{
				edgesToDelete[ edge->key() ] = edge;
			}
		}
	}

	foreach( int faceIndex, facesToDelete.keys() )
	{
		m_qvFaces[ faceIndex ] = NULL;
		delete facesToDelete[ faceIndex ];
	}

	foreach( MeshEdgeKey key, edgesToDelete.keys() )
	{
		m_qhEdges.remove( key );
		delete edgesToDelete[ key ];
	}

	foreach( int vertexIndex, vertexIndices )
	{
		delete m_qvVertices[ vertexIndex ];
		m_qvVertices[ vertexIndex ] = NULL;
	}
}

void Mesh2f::deleteFace( int faceIndex )
{
	MeshFace2f* face = getFaceByIndex( faceIndex );

	m_holes.append( face->pointInsidePolygon() );

	QVector< MeshVertex2f* > faceVertices = face->vertices();
	QVector< MeshHalfEdge2f* > faceEdges = face->edges();

	printf( "deleting face %d = [ ", face->index() );
	foreach( MeshVertex2f* v, faceVertices )
	{
		printf( "%d ", v->index() );
	}
	printf( "]\n" );


	// foreach vertex v on this face
	// if v->outgoing is on this face (v->outgoing->incident = f)
	// then set v->outgoing to something else
	// if there isn't one, delete the vertex
	foreach( MeshVertex2f* v, faceVertices )
	{
		if( v->outgoingEdge()->incidentFace() == face )
		{
			printf( "vertex %d has edge incident to face\n", v->index() );

			QVector< MeshHalfEdge2f* > outgoing = outgoingEdgesSlow( v );
			foreach( MeshHalfEdge2f* outgoingEdge, outgoing )
			{
				if( outgoingEdge->incidentFace() != face )
				{
					printf( "setting new outgoing edge: %d, %d\n", outgoingEdge->key().first, outgoingEdge->key().second );
					v->setOutgoingEdge( outgoingEdge );
					break;
				}
			}

			// if we didn't set anything, the incident face will still be this
			if( v->outgoingEdge()->incidentFace() == face )
			{
				printf( "deleting vertex: %d\n", v->index() );
				printf( "only outgoing edge was: %d, %d\n", v->outgoingEdge()->key().first, v->outgoingEdge()->key().second );
				m_qvVertices[ v->index() ] = NULL;
				delete v;
			}
		}
	}

	// foreach edge e on this face
	// set e->twin->twin = NULL to mark the new boundary
	foreach( MeshHalfEdge2f* e, face->edges() )
	{
		MeshHalfEdge2f* et = e->twinHalfEdge();

		if( et != NULL )
		{
			et->setTwinHalfEdge( NULL );
		}
	}

	// delete the edges
	foreach( MeshHalfEdge2f* e, face->edges() )
	{
		m_qhEdges.remove( e->key() );
		delete e;
	}

	// finally delete the face
	m_qvFaces[ faceIndex ] = NULL;
	delete face;
}

#if 0
void Mesh2f::deleteFaces( QVector< int > faceIndices )
{
	QVector< MeshHalfEdge2f* > edgesToDelete;

	foreach( int faceIndex, faceIndices )
	{
		MeshFace2f* face = getFaceByIndex( faceIndex );
		QVector< MeshHalfEdge2f* > faceEdges = face->edges();

		edgesToDelete += faceEdges;
	}

	foreach( MeshHalfEdge2f* edge, edgesToDelete )
	{
		m_qhEdges.remove( edge->key() );
		delete edge;
	}

	foreach( int faceIndex, faceIndices )
	{
		delete m_qvFaces[ faceIndex ];
		m_qvFaces[ faceIndex ] = NULL;
	}
}
#endif

#if 0
void Mesh2f::deleteVertex( MeshVertex2f* pVertex )
{
	QVector< MeshHalfEdge2f* > incoming;
	QVector< MeshHalfEdge2f* > outgoing;
	int incomingBoundaryIndex;
	int outgoingBoundaryIndex;

	printf( "deleting vertex: %d\n", pVertex->index() );
	pVertex->incidentEdges( &incoming, &outgoing, &incomingBoundaryIndex, &outgoingBoundaryIndex );

	// if it's not an orphan
	if( outgoing.size() > 0 )
	{
		// we deleted a boundary vertex
		// delete all the faces
		if( outgoingBoundaryIndex != -1 )
		{
			// take each outgoing edge e
			//
			// 1. set each neighboring vertex's outgoingEdge to point to something we're not going to delete
			//     let v = e->destination()
			//     if v->outgoingEdge() is going to be deleted (v->outgoingEdge()->destination() == pVertex)
			//         set it to e->next()
			//
			// 2. delete neighboring faces
			//     foreach incident face f of e
			//         foreach edge e of f		
			//             if e is not a boundary, then e->twin->setTwin( NULL ) // set new boundary
			//             delete e
			//             (incoming and outgoing edges are all included when deleting over each face)
			//         delete f
			//     

			int nOutgoingEdges = outgoing.size();
			for( int i = 0; i < nOutgoingEdges; ++i )
			{
				MeshHalfEdge2f* e = outgoing[ i ];
				MeshEdgeKey eKey = e->key();

				MeshFace2f* f = e->incidentFace();

				QVector< MeshHalfEdge2f* > faceEdges = f->edges();
				int nFaceEdges = faceEdges.size();

				QVector< MeshVertex2f* > faceVertices = f->vertices();
				int nFaceVertices = faceVertices.size();

				// for each vertex on the face
				// set its outgoing edge so that it's not incident on f (which we're deleting)
				// if there isn't one, then it is orphaned
				for( int j = 0; j < nFaceVertices; ++j )
				{
					MeshVertex2f* faceVertex = faceVertices[ j ];
					if( faceVertex != pVertex )
					{
						QVector< MeshHalfEdge2f* > faceVertexIncoming;
						QVector< MeshHalfEdge2f* > faceVertexOutgoing;
						int faceVertexIncomingBoundaryIndex;
						int faceVertexOutgoingBoundaryIndex;

						faceVertex->incidentEdges( &faceVertexIncoming, &faceVertexOutgoing, &faceVertexIncomingBoundaryIndex, &faceVertexOutgoingBoundaryIndex );

						MeshHalfEdge2f* outgoingEdgeNotIncidentOnF = NULL;
						for( int k = 0; k < faceVertexOutgoing.size(); ++k )
						{
							if( faceVertexOutgoing[ k ]->incidentFace() != f )
							{
								outgoingEdgeNotIncidentOnF = faceVertexOutgoing[ k ];
								break;
							}
						}
						if( faceVertex->index() == 414 )
						{
							printf( "foo 0\n" );
						}
						faceVertex->setOutgoingEdge( outgoingEdgeNotIncidentOnF );
					}
				}

				
				// remove each face edge from the hash table,
				// make their twins boundaries
				for( int j = 0; j < nFaceEdges; ++j )
				{
					MeshHalfEdge2f* faceEdge = faceEdges[ j ];

					m_qhEdges.remove( faceEdge->key() );

					// the edge is not a boundary
					if( !( faceEdge->isBoundary() ) )
					{
						faceEdge->twinHalfEdge()->setTwinHalfEdge( NULL );
					}
				}

				// and now delete each face
				for( int j = 0; j < nFaceEdges; ++j )
				{
					MeshHalfEdge2f* faceEdge = faceEdges[ j ];
					delete faceEdge;
				}

				// delete the face
				m_qvFaces[ f->index() ] = NULL;
				delete f;
			}

			m_qvVertices[ pVertex->index() ] = NULL;
			delete pVertex;
		}
		// deleted an interior vertex, that's ok
		// create a new face
		else
		{
			// save outgoing[ 0 ]->next, it's guaranteed to be on the new face
			MeshHalfEdge2f* edgeOnNewFace = outgoing[ 0 ]->nextHalfEdgeOnFace();

			// for each outgoing edge e:
			//
			// 1. set each vertex on the new face's outgoing edge to something that exists
			//    in case the outgoing edge is one we're about to delete
			//      let v = e->dest()
			//      en = e->next
			//      v->outgoingEdge() = en
			// 2. link the edges along the new face
			//      let et = e->twin
			//      let en = e->next		
			//      and etp = et->prev
			//      etp->next = en
			//      en->prev = etp
			// 3. delete e and et from the hash table
			// 4. finally delete e and et

			int nOutgoingEdges = outgoing.size();
			for( int i = 0; i < nOutgoingEdges; ++i )
			{
				MeshHalfEdge2f* e = outgoing[ i ];
				MeshHalfEdge2f* en = e->nextHalfEdgeOnFace();

				MeshVertex2f* v = e->destinationVertex();
				if( v->index() == 414 )
				{
					printf( "foo 2\n" );
					qDebug() << "e =" << e->toString();
					qDebug() << "en =" << en->toString();
				}
				v->setOutgoingEdge( en );

				MeshHalfEdge2f* et = e->twinHalfEdge();
				MeshHalfEdge2f* etp = et->previousHalfEdgeOnFace();

				etp->setNextHalfEdgeOnFace( en );
				en->setPreviousHalfEdgeOnFace( etp );
				
				// delete the face incident to e
				MeshFace2f* pIncidentFace = e->incidentFace();
				int incidentFaceIndex = pIncidentFace->index();				
				m_qvFaces[ incidentFaceIndex ] = NULL;
				delete pIncidentFace;

				// remove the edges from the hash table
				m_qhEdges.remove( e->key() );
				m_qhEdges.remove( et->key() );
			}

			for( int i = 0; i < nOutgoingEdges; ++i )
			{
				MeshHalfEdge2f* e = outgoing[ i ];
				MeshHalfEdge2f* et = e->twinHalfEdge();
				
				// and delete the edges themselves
				delete e;
				delete et;
			}

			// create a new face
			// set its index to be a new one at the end of the list
			int newFaceIndex = getNewFaceIndex();
			MeshFace2f* pFace = new MeshFace2f( newFaceIndex, edgeOnNewFace );
			m_qvFaces.append( pFace );

			// some of the face's edges still point to the deleted faces
			// have them point to the new face
			QVector< MeshHalfEdge2f* > newFaceEdges = pFace->edges();
			int nNewFaceEdges = newFaceEdges.size();
			foreach( MeshHalfEdge2f* edge, newFaceEdges )
			{
				edge->setIncidentFace( pFace );
			}

			// now go along the edges of the face
			// if any are sticking out (i.e. e->next == e->twin), delete it and orphan the vertex
			// this happens when you delete a vertex whose neighbor has only 2 outgoing edges
			for( int i = 0; i < nNewFaceEdges; ++i )
			{
				MeshHalfEdge2f* edge = newFaceEdges[ i ];
				MeshHalfEdge2f* nextEdge = edge->nextHalfEdgeOnFace();
				MeshHalfEdge2f* twinEdge = edge->twinHalfEdge();

				if( nextEdge == twinEdge )
				{
					MeshHalfEdge2f* prevEdge = edge->previousHalfEdgeOnFace();
					MeshHalfEdge2f* nextNextEdge = nextEdge->nextHalfEdgeOnFace();

					if( edge->destinationVertex()->index() == 414 )
					{
						printf( "foo 3\n" );
					}
					edge->destinationVertex()->setOutgoingEdge( NULL );

					prevEdge->setNextHalfEdgeOnFace( nextNextEdge );
					nextNextEdge->setPreviousHalfEdgeOnFace( prevEdge );

					pFace->setInitialEdge( nextNextEdge );

					m_qhEdges.remove( edge->key() );
					m_qhEdges.remove( nextEdge->key() );
					delete edge;
					delete nextEdge;

					i += 2;
				}
			}

			// finally, delete the vertex
			m_qvVertices[ pVertex->index() ] = NULL;
			delete pVertex;	
		}
	}
	// it's an orphan, just delete it
	else
	{
		// finally, delete the vertex
		m_qvVertices[ pVertex->index() ] = NULL;
		delete pVertex;
	}
}
#endif

QVector< QVector< Vector2f > > Mesh2f::flatPolygonList()
{
	int nFaces = m_qvFaces.size();

	QVector< QVector< Vector2f > > qvFlatPolygonList;
	qvFlatPolygonList.reserve( nFaces );

	for( int f = 0; f < nFaces; ++f )
	{
		qvFlatPolygonList.append( QVector< Vector2f >() );
		QVector< Vector2f >& positions = qvFlatPolygonList.last();

		MeshFace2f* pFace = m_qvFaces[ f ];
		if( pFace != NULL )
		{
			QVector< MeshVertex2f* > qvVertices = pFace->vertices();
			int nVertices = qvVertices.size();

			for( int v = 0; v < nVertices; ++v )
			{
				MeshVertex2f* pVertex = qvVertices[ v ];

				if( pVertex->position().x() < 0 )
				{
					printf( "bad vertex index = %d\n", pVertex->index() );
					assert( false );
				}

				assert( qvVertices[ v ]->position().y() > 0 );
				assert( qvVertices[ v ]->position().x() < 1000 );
				assert( qvVertices[ v ]->position().y() < 1000 );

				positions.append( qvVertices[ v ]->position() );
			}
		}
	}

	return qvFlatPolygonList;
}

QVector< MeshEdgeKey > Mesh2f::segmentList()
{
	// clone the edge hash table
	QHash< MeshEdgeKey, MeshHalfEdge2f* > qhEdgesCopy( m_qhEdges );

	// iterate over the original, and delete its twin in the copy if it exists
	foreach( MeshEdgeKey key, m_qhEdges.keys() )
	{
		MeshEdgeKey twinKey = qMakePair( key.second, key.first );

		if( qhEdgesCopy.contains( twinKey ) )
		{
			qhEdgesCopy.remove( twinKey );
		}
	}

	// now flatten the keys
	QVector< MeshEdgeKey > qvSegmentList;
	qvSegmentList.reserve( qhEdgesCopy.size() );

	foreach( MeshEdgeKey key, qhEdgesCopy.keys() )
	{
		qvSegmentList.append( key );
	}

	return qvSegmentList;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

int Mesh2f::getNewVertexIndex()
{
	return m_qvVertices.size();
}

int Mesh2f::getNewFaceIndex()
{
	return m_qvFaces.size();
}

QVector< MeshHalfEdge2f* > Mesh2f::outgoingEdgesSlow( MeshVertex2f* vertex )
{
	QVector< MeshHalfEdge2f* > output;

	int vertexIndex = vertex->index();

	foreach( MeshEdgeKey key, m_qhEdges.keys() )
	{
		if( key.first == vertexIndex )
		{
			output.append( m_qhEdges[ key ] );
		}
	}

	return output;
}

#if 0

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Mesh2f::Mesh2f( QVector< Vector2f > positions,
			   QVector< QVector< int > > faces ) :

	m_qvPositions( positions ),
	m_qvFaces( faces ),

	m_bFaceNeighborsValid( false ),
	m_bVertexNeighborsValid( false )
{

}

Mesh2f::Mesh2f( Reference< Mesh2f > copy ) :

	m_qvPositions( copy->m_qvPositions ),
	m_qvFaces( copy->m_qvFaces ),

	m_bFaceNeighborsValid( copy->m_bFaceNeighborsValid ),
	m_qvFaceNeighbors( copy->m_qvFaceNeighbors ),

	m_bVertexNeighborsValid( copy->m_bVertexNeighborsValid ),
	m_qvVertexNeighbors( copy->m_qvVertexNeighbors )
{

}
	
QVector< Vector2f > Mesh2f::positions()
{
	return m_qvPositions;
}

void Mesh2f::setPosition( int i, const Vector2f& p )
{
	m_qvPositions[ i ] = p;
}

QVector< QVector< int > > Mesh2f::faces()
{
	return m_qvFaces;
}

QVector< QVector< int > > Mesh2f::faceNeighbors()
{
	if( !m_bFaceNeighborsValid )
	{
		updateFaceNeighbors();
	}
	return m_qvFaceNeighbors;
}

QVector< QVector< int > > Mesh2f::vertexNeighbors()
{
	if( !m_bVertexNeighborsValid )
	{
		updateVertexNeighbors();
	}
	return m_qvVertexNeighbors;
}

void Mesh2f::deleteVertices( QVector< int > vertexIndices )
{
	QVector< Vector2f > newPositions; // compacted position list
	QVector< Vector3i > newFaces; // compacted face list
	QVector< int > newPositionIndices; // mapping from old position index to new position index
	QVector< int > newFaceIndices; // mapping from old face index to new face index

	QHash< QString, QVector< int > > newIntAttributes;
	foreach( QString name, m_qhIntAttributes.keys() )
	{
		newIntAttributes[ name ] = QVector< int >();
	}

	// copy the positions that aren't being deleted
	// and map the old index to the new index
	int nOriginalVertices = m_qvPositions.size();
	for( int i = 0; i < nOriginalVertices; ++i )
	{
		// if the i-th vertex is not a deleted one, then add it
		if( !( vertexIndices.contains( i ) ) )
		{
			newPositions.append( m_qvPositions[ i ] );

			// iterate over all values in the hash map
			// and compact those, too
			foreach( QString name, m_qhIntAttributes.keys() )
			{
				newIntAttributes[ name ].append( m_qhIntAttributes[ name ][ i ] );
			}

			// and its new index is the new index = size() - 1
			newPositionIndices.append( newPositions.size() - 1 );
		}
		else
		{
			// it's been deleted
			// so append -1
			newPositionIndices.append( -1 );
		}
	}

	// now update face list
	// make a list of all the faces neighbored a deleted vertex
	// compact the list
	// then add new faces


	int nOriginalTriangles = m_qvTriangleList.size();
	for( int i = 0; i < nOriginalTriangles; ++i )
	{
		// old triangle 
		Vector3i oldTriangle = m_qvTriangleList[ i ];

		int v0 = oldTriangle[ 0 ];
		int v1 = oldTriangle[ 1 ];
		int v2 = oldTriangle[ 2 ];

		// if it's not a deleted one
		if( !( vertexIndices.contains( v0 ) ||
			vertexIndices.contains( v1 ) ||
			vertexIndices.contains( v2 ) ) )
		{
			Vector3i newTriangle
				(			
				newPositionIndices[ v0 ],
				newPositionIndices[ v1 ],
				newPositionIndices[ v2 ]
			);

			// newTriangleList.append( newTriangleVertexIndices );

			// original triangle was kept, append its new index
			// (currently the last element in the list)
			newTriangleIndices.append( newTriangleList.size() - 1 );
		}
		else
		{
			// original triangle was deleted, so append -1
			newTriangleIndices.append( -1 );
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

void Mesh2f::updateFaceNeighbors()
{
	m_qvFaceNeighbors.clear();

	int nFaces = m_qvFaces.size();
	
	// for each face
	for( int f = 0; f < nFaces; ++f )
	{
		// neighbors for this face
		QVector< int > faceNeighbors;

		// get its vertex list
		QVector< int > face = m_qvFaces.at( f );
		int nVertices = face.size();

		// go over each edge
		for( int v = 0; v < nVertices; ++v )
		{
			int v0 = face[ v ];
			int v1 = face[ ( v + 1 ) % nVertices ];

			// find the face that has the edge v1 --> v0
			int neighboringFace = findFaceWithEdge( v1, v0 );
			faceNeighbors.append( neighboringFace );
		}

		m_qvFaces.append( faceNeighbors );
	}

	m_bFaceNeighborsValid = true;
}

void Mesh2f::updateVertexNeighbors()
{
	m_qvVertexNeighbors.clear();

	int nVertices = m_qvPositions.size();
	int nFaces = m_qvFaces.size();

	// for each vertex
	for( int v = 0; v < nVertices; ++v )
	{
		QVector< int > vertexNeighbors;

		// find all faces that contain this vertex
		for( int f = 0; f < nFaces; ++f )
		{
			QVector< int > face = m_qvFaces[ f ];
			
			int nFaceVertices = face.size();
			for( int i = 0; i < nFaceVertices; ++i )
			{
				if( face[ i ] == v )
				{
					vertexNeighbors.append( f );
					break;
				}
			}
		}

		m_qvVertexNeighbors.append( vertexNeighbors );
	}

	m_bVertexNeighborsValid = true;
}

int Mesh2f::findFaceWithEdge( int v0, int v1 )
{
	int nFaces = m_qvFaces.size();

	// for each face
	for( int f = 0; f < nFaces; ++f )
	{
		// get its vertex list
		QVector< int > face = m_qvFaces.at( f );
		int nVertices = face.size();

		// go over each edge
		for( int v = 0; v < nVertices; ++v )
		{
			if( face[ v ] == v0 &&
				face[ ( v + 1 ) % nVertices ] == v1 )
			{
				return f;
			}
		}
	}

	return -1;
}

#endif
