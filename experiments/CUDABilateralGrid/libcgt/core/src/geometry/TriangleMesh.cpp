#include "geometry/TriangleMesh.h"

#include "common/ProgressReporter.h"

#include <algorithm>
#include <numeric>
#include <stack>
#include <cstdio>

TriangleMesh::TriangleMesh()
{

}

TriangleMesh::TriangleMesh( std::shared_ptr< OBJData > pData )
{
	QVector< Vector3f >* pPositions = pData->getPositions();
	int nVertices = pPositions->size();
	m_positions = std::vector< Vector3f >( nVertices );
	for( int v = 0; v < nVertices; ++v )
	{
		m_positions[ v ] = pPositions->at( v );
	}

	QVector< Vector3f >* pNormals = pData->getNormals();
	int nNormals = pNormals->size();
	m_normals = std::vector< Vector3f >( nNormals );
	for( int n = 0; n < nNormals; ++n )
	{
		m_normals[ n ] = pNormals->at( n );
	}

	std::vector< Vector3i > normalIndices;

	QVector< OBJGroup* >* pGroups = pData->getGroups();
	for( int g = 0; g < pGroups->size(); ++g )
	{
		auto pGroup = pGroups->at( g );
		auto pFaces = pGroup->getFaces();
		for( int f = 0; f < pFaces->size(); ++f )
		{
			auto pFace = pFaces->at( f );
			int p0 = pFace.getPositionIndices()->at( 0 );
			int p1 = pFace.getPositionIndices()->at( 1 );
			int p2 = pFace.getPositionIndices()->at( 2 );

			int n0 = pFace.getNormalIndices()->at( 0 );
			int n1 = pFace.getNormalIndices()->at( 1 );
			int n2 = pFace.getNormalIndices()->at( 2 );

			m_faces.push_back( Vector3i( p0, p1, p2 ) );
			normalIndices.push_back( Vector3i( n0, n1, n2 ) );
		}
	}

	harmonizeNormalsWithPositions( normalIndices );
}

TriangleMesh::TriangleMesh( std::shared_ptr< OBJData > pData, int groupIndex, bool generatePerFaceNormalsIfNonExistent )
{
	auto pGroup = pData->getGroups()->at( groupIndex );

	QVector< Vector3f >* pPositions = pData->getPositions();
	int nVertices = pPositions->size();
	m_positions = std::vector< Vector3f >( nVertices );
	for( int v = 0; v < nVertices; ++v )
	{
		m_positions[ v ] = pPositions->at( v );
	}

	if( pGroup->hasNormals() )
	{
		QVector< Vector3f >* pNormals = pData->getNormals();
		int nNormals = pNormals->size();
		m_normals = std::vector< Vector3f >( nNormals );
		for( int n = 0; n < nNormals; ++n )
		{
			m_normals[ n ] = pNormals->at( n );
		}
	}

	std::vector< Vector3i > normalIndices;

	auto pFaces = pGroup->getFaces();
	for( int f = 0; f < pFaces->size(); ++f )
	{
		auto pFace = pFaces->at( f );

		int pi0 = pFace.getPositionIndices()->at( 0 );
		int pi1 = pFace.getPositionIndices()->at( 1 );
		int pi2 = pFace.getPositionIndices()->at( 2 );

		m_faces.push_back( Vector3i( pi0, pi1, pi2 ) );

		if( pGroup->hasNormals() )
		{
			int ni0 = pFace.getNormalIndices()->at( 0 );
			int ni1 = pFace.getNormalIndices()->at( 1 );
			int ni2 = pFace.getNormalIndices()->at( 2 );

			normalIndices.push_back( Vector3i( ni0, ni1, ni2 ) );
		}
		else if( generatePerFaceNormalsIfNonExistent )
		{
			Vector3f p0 = pPositions->at( pi0 );
			Vector3f p1 = pPositions->at( pi1 );
			Vector3f p2 = pPositions->at( pi2 );

			Vector3f normal = Vector3f::cross( p1 - p0, p2 - p0 ).normalized();
			m_normals.push_back( normal );
			int ni = m_normals.size() - 1;
			normalIndices.push_back( Vector3i( ni ) );
		}
	}

	harmonizeNormalsWithPositions( normalIndices );
}

float TriangleMesh::meanEdgeLength()
{
	float sum = 0;
	for( auto itr = m_edgeLengths.begin(); itr != m_edgeLengths.end(); ++itr )
	{
		float len = itr->second;
		sum += len;
	}
	return sum / m_edgeLengths.size();
}


float TriangleMesh::area( int faceIndex ) const
{
	return m_areas[ faceIndex ];
}

float TriangleMesh::totalArea() const
{
	float sum = 0;
	for( int f = 0; f < m_areas.size(); ++f )
	{
		sum += m_areas[f];
	}
	return sum;
}

TriangleMesh TriangleMesh::consolidate( const std::vector< int >& connectedComponent )
{
	TriangleMesh output;

	int nVertices = m_positions.size();
	std::vector< bool > touchedVertices( nVertices, false );

	// walk over all faces in the component
	// and mark all vertices that are used
	int nFaces = connectedComponent.size();
	for( int i = 0; i < nFaces; ++i )
	{
		int f = connectedComponent[ i ];
		Vector3i face = m_faces[f];

		touchedVertices[ face.x ] = true;
		touchedVertices[ face.y ] = true;
		touchedVertices[ face.z ] = true;
	}

	// walk over all used vertices and assign them new indices [0, nUsedVertices)
	// -1 is unused
	std::vector< int > oldVertexToNewVertexMap( nVertices, -1 );
	int nUsedVertices = 0;
	for( int i = 0; i < nVertices; ++i )
	{
		if( touchedVertices[i] )
		{
			oldVertexToNewVertexMap[i] = nUsedVertices;
			++nUsedVertices;
		}
	}

	// now that we know how many used vertices there are
	// resize output arrays
	output.m_positions.resize( nUsedVertices );
	output.m_normals.resize( nUsedVertices );
	output.m_faces.resize( nFaces );

	// walk over all used vertices and copy them onto the output
	for( int i = 0; i < nVertices; ++i )
	{
		if( touchedVertices[i] )
		{
			Vector3f p = m_positions[i];
			Vector3f n = m_normals[i];

			int j = oldVertexToNewVertexMap[i];			
			
			output.m_positions[j] = p;
			output.m_normals[j] = n;
		}
	}

	// walk over faces and assign them their new indices
	for( int i = 0; i < nFaces; ++i )
	{
		int f = connectedComponent[i];
		Vector3i face = m_faces[ f ];
		
		Vector3i newFace
		(
			oldVertexToNewVertexMap[ face.x ],
			oldVertexToNewVertexMap[ face.y ],
			oldVertexToNewVertexMap[ face.z ]
		);

		output.m_faces.push_back( newFace );
	}

	return output;
}

int TriangleMesh::pruneInvalidFaces( std::map< Vector2i, int >& edgeToFace )
{
	// walk over all faces
	// build for each edge (v0,v1)
	//   edgeToFace[ v0, v1 ] = face
	//   if it already exists, we have a problem
	//   and we will throw the face away

	int nFaces = m_faces.size();
	edgeToFace.clear();
	//edgeToFace.reserve( 3 * nFaces );
	std::vector< Vector3i > validFaces;
	validFaces.reserve( nFaces );

	//ProgressReporter pr( "Pruning invalid faces", nFaces );

	int nPruned = 0;
	for( int f = 0; f < nFaces; ++f )
	{
		Vector3i face = m_faces[ f ];

		Vector2i e0 = face.xy();
		Vector2i e1 = face.yz();
		Vector2i e2 = face.zx();

		if( edgeToFace.find( e0 ) == edgeToFace.end() &&
			edgeToFace.find( e1 ) == edgeToFace.end() &&
			edgeToFace.find( e2 ) == edgeToFace.end() )
		{
			edgeToFace[ e0 ] = f;
			edgeToFace[ e1 ] = f;
			edgeToFace[ e2 ] = f;
			validFaces.push_back( face );
		}
		else
		{
			++nPruned;

			fprintf( stderr, "Found invalid face: (%d, %d, %d)\n",
				face.x, face.y, face.z );
			
			if( edgeToFace.find( e0 ) != edgeToFace.end() )
			{
				Vector3i existingFace = m_faces[ edgeToFace[ e0 ] ];
				fprintf( stderr, "Existing face: (%d, %d, %d)\n",
					existingFace.x, existingFace.y, existingFace.z );
			}
			if( edgeToFace.find( e1 ) != edgeToFace.end() )
			{
				Vector3i existingFace = m_faces[ edgeToFace[ e1 ] ];
				fprintf( stderr, "Existing face: (%d, %d, %d)\n",
					existingFace.x, existingFace.y, existingFace.z );
			}
			if( edgeToFace.find( e2 ) != edgeToFace.end() )
			{
				Vector3i existingFace = m_faces[ edgeToFace[ e2 ] ];
				fprintf( stderr, "Existing face: (%d, %d, %d)\n",
					existingFace.x, existingFace.y, existingFace.z );
			}
		}

		//pr.notifyAndPrintProgressString();
	}
	if( nPruned > 0 )
	{
		fprintf( stderr, "Pruned %d faces\n", nPruned );
		m_faces = validFaces;
	}

	return nPruned;
}

void TriangleMesh::buildAdjacency()
{
	int nPruned = pruneInvalidFaces( m_edgeToFace );

	// walk over all faces
	// and build an adjacency map:
	// edge -> adjacent face
	// if nothing was pruned, then it's already valid
	if( nPruned != 0 )
	{
		int nFaces = m_faces.size();
		m_edgeToFace.clear();

		for( int f = 0; f < nFaces; ++f )
		{
			Vector3i face = m_faces[ f ];

			Vector2i e0 = face.xy();
			Vector2i e1 = face.yz();
			Vector2i e2 = face.zx();

			m_edgeToFace[ e0 ] = f;
			m_edgeToFace[ e1 ] = f;
			m_edgeToFace[ e2 ] = f;
		}
	}

	// build face to face adjacency
	// for each face:
	//    find 3 edges
	//    flip edge: if the flipped edge has an adjacent face
	//       add it as a neighbor of this face
	int nFaces = m_faces.size();
	m_faceToFace.clear();
	m_faceToFace.resize( nFaces );
	for( int f = 0; f < nFaces; ++f )
	{
		Vector3i face = m_faces[ f ];
		// get 3 edge twins
		Vector2i e0t = face.yx();
		Vector2i e1t = face.zy();
		Vector2i e2t = face.xz();

		if( m_edgeToFace.find( e0t ) != m_edgeToFace.end() )
		{
			m_faceToFace[ f ].push_back( m_edgeToFace[ e0t ] );
		}
		if( m_edgeToFace.find( e1t ) != m_edgeToFace.end() )
		{
			m_faceToFace[ f ].push_back( m_edgeToFace[ e1t ] );
		}
		if( m_edgeToFace.find( e2t ) != m_edgeToFace.end() )
		{
			m_faceToFace[ f ].push_back( m_edgeToFace[ e2t ] );
		}
	}
}

void TriangleMesh::computeConnectedComponents()
{
	m_connectedComponents.clear();

	// build a bit vector of length nFaces
	// set them all to true for now
	int nFaces = m_faces.size();
	std::vector< bool > remainingFaces( nFaces, true );

	// loop until out of faces
	auto rootItr = std::find( remainingFaces.begin(), remainingFaces.end(), true );
	while( rootItr != remainingFaces.end() )
	{
		int rootFaceIndex = static_cast< int >( rootItr - remainingFaces.begin() );
		std::vector< int > connectedComponent;
		
		// start with a root face and push it onto the stack
		// while the stack is not empty
		//    pop a face off the stack and add it to the component
		//    mark it as taken
		//    then add its adjacent faces onto the stack
		std::stack< int > adjStack;
		adjStack.push( rootFaceIndex );
		remainingFaces[ rootFaceIndex ] = false;
		while( !( adjStack.empty() ) )
		{
			int currentFaceIndex = adjStack.top();
			adjStack.pop();
			
			connectedComponent.push_back( currentFaceIndex );

			for( int i = 0; i < m_faceToFace[ currentFaceIndex ].size(); ++i )
			{
				int adjacentFaceIndex = m_faceToFace[ currentFaceIndex ][ i ];
				if( remainingFaces[ adjacentFaceIndex ] )
				{
					adjStack.push( adjacentFaceIndex );
					remainingFaces[ adjacentFaceIndex ] = false;
				}
			}
		}

		m_connectedComponents.push_back( connectedComponent );
		rootItr = std::find( remainingFaces.begin(), remainingFaces.end(), true );
	}
}

void TriangleMesh::computeAreas()
{
	int nFaces = m_faces.size();
	m_areas.resize( nFaces );

	for( int f = 0; f < nFaces; ++f )
	{
		Vector3i face = m_faces[ f ];
		Vector3f p0 = m_positions[ face.x ];
		Vector3f p1 = m_positions[ face.y ];
		Vector3f p2 = m_positions[ face.z ];

		Vector3f e0 = p1 - p0;
		Vector3f e1 = p2 - p0;

		float area = 0.5f * Vector3f::cross( e0, e1 ).abs();
		m_areas[ f ] = area;
	}
}

void TriangleMesh::computeEdgeLengths()
{
	for( auto itr = m_edgeToFace.begin(); itr != m_edgeToFace.end(); ++itr )
	{
		Vector2i vertexIndices = itr->first;

		Vector3f p0 = m_positions[ vertexIndices.x ];
		Vector3f p1 = m_positions[ vertexIndices.y ];

		m_edgeLengths[ vertexIndices ] = ( p1 - p0 ).abs();
	}
}

void TriangleMesh::harmonizeNormalsWithPositions( const std::vector< Vector3i >& normalIndices )
{
	std::vector< Vector3f > outputNormalIndices( m_positions.size() );

	int nFaces = m_faces.size();
	for( int f = 0; f < nFaces; ++f )
	{
		Vector3i pIndices = m_faces[ f ];
		Vector3i nIndices = normalIndices[ f ];

		for( int i = 0; i < 3; ++i )
		{
			int pIndex = pIndices[i];
			int nIndex = nIndices[i];

			Vector3f normal = m_normals[ nIndex ];
			outputNormalIndices[ pIndex ] = normal;
		}
	}

	m_normals = outputNormalIndices;
}

void TriangleMesh::saveOBJ( QString filename )
{
	FILE* fp = fopen( qPrintable( filename ), "w" );

	for( int i = 0; i < m_positions.size(); ++i )
	{
		Vector3f p = m_positions[i];
		fprintf( fp, "v %f %f %f\n", p.x, p.y, p.z );
	}

	for( int i = 0; i < m_normals.size(); ++i )
	{
		Vector3f n = m_normals[i];
		fprintf( fp, "vn %f %f %f\n", n.x, n.y, n.z );
	}

	for( int i = 0; i < m_faces.size(); ++i )
	{
		Vector3i f = m_faces[ i ];
		fprintf( fp, "f %d//%d %d//%d %d//%d\n",
			f.x + 1, f.x + 1, f.y + 1, f.y + 1, f.z + 1, f.z + 1 );
	}

	fclose( fp );
}
